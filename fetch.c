#include <dirent.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <termios.h>
#include <unistd.h>

static struct termios orig_termios;
static int termios_saved = 0;

static void cleanup(void) {
  if (termios_saved)
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
  printf("\033[?25h");
  fflush(stdout);
}

static void handle_signal(int sig) {
  (void)sig;
  cleanup();
  _exit(0);
}

#define ANIM_WIDTH 60
#define MAX_HEIGHT 200
#define GAP 2

static int render_height = 36;
#define PI 3.14159265f

// --- UTF-8 helpers ---

// Returns byte length of a UTF-8 sequence from its leading byte
static int utf8_char_len(unsigned char c) {
  if (c < 0x80) return 1;
  if ((c & 0xE0) == 0xC0) return 2;
  if ((c & 0xF0) == 0xE0) return 3;
  if ((c & 0xF8) == 0xF0) return 4;
  return 1; // invalid, treat as 1
}

// Skip past an ANSI escape sequence (ESC [ ... letter)
// Returns number of bytes to skip, or 0 if not an escape
static int skip_ansi(const char *p) {
  if (p[0] != '\033' || p[1] != '[')
    return 0;
  int i = 2;
  while (p[i] && ((p[i] >= '0' && p[i] <= '9') || p[i] == ';'))
    i++;
  if (p[i])
    i++; // skip the final letter
  return i;
}

// Parse a UTF-8 string into individual codepoints
#define MAX_SHADING 64
static char shading_chars[MAX_SHADING][5];
static int shading_count = 0;

static void parse_shading(const char *str) {
  shading_count = 0;
  const char *p = str;
  while (*p && shading_count < MAX_SHADING) {
    int len = utf8_char_len((unsigned char)*p);
    if (len > 4) len = 4;
    // Make sure we don't read past end of string
    int actual = 0;
    while (actual < len && p[actual])
      actual++;
    memcpy(shading_chars[shading_count], p, actual);
    shading_chars[shading_count][actual] = '\0';
    shading_count++;
    p += actual;
  }
  if (shading_count == 0) {
    // Fallback
    strcpy(shading_chars[0], ".");
    shading_count = 1;
  }
}

// --- Logo storage (codepoint-aware) ---

#define MAX_LOGO_ROWS 64
#define MAX_LOGO_COLS 128
// Raw byte data
static char logo_data[MAX_LOGO_ROWS][512];
// Parsed per-cell codepoints
static char logo_cells[MAX_LOGO_ROWS][MAX_LOGO_COLS][5];
static int logo_cell_color[MAX_LOGO_ROWS][MAX_LOGO_COLS]; // ANSI fg color per cell
static int logo_cell_counts[MAX_LOGO_ROWS];
static int logo_rows = 0;
static int logo_cols = 0;
static int logo_has_ansi = 0;

// Process a logo row: split into codepoints, extracting ANSI colors
static void process_logo_row(int row) {
  const char *p = logo_data[row];
  int col = 0;
  int cur_color = 0;
  while (*p && col < MAX_LOGO_COLS) {
    // Parse ANSI escapes for color info
    if (p[0] == '\033' && p[1] == '[') {
      int i = 2;
      // Extract foreground color from SGR params
      int num = 0, has_num = 0;
      while (p[i] && ((p[i] >= '0' && p[i] <= '9') || p[i] == ';')) {
        if (p[i] >= '0' && p[i] <= '9') {
          num = num * 10 + (p[i] - '0');
          has_num = 1;
        } else if (p[i] == ';') {
          if (has_num && ((num >= 30 && num <= 37) || num == 39 || (num >= 90 && num <= 97)))
            cur_color = num;
          if (has_num && num == 0)
            cur_color = 0;
          num = 0;
          has_num = 0;
        }
        i++;
      }
      if (has_num && ((num >= 30 && num <= 37) || num == 39 || (num >= 90 && num <= 97)))
        cur_color = num;
      if (has_num && num == 0)
        cur_color = 0;
      if (p[i])
        i++;
      if (cur_color > 0)
        logo_has_ansi = 1;
      p += i;
      continue;
    }
    int len = utf8_char_len((unsigned char)*p);
    int actual = 0;
    while (actual < len && p[actual])
      actual++;
    memcpy(logo_cells[row][col], p, actual);
    logo_cells[row][col][actual] = '\0';
    logo_cell_color[row][col] = cur_color;
    col++;
    p += actual;
  }
  logo_cell_counts[row] = col;
  if (col > logo_cols)
    logo_cols = col;
}

// Process all loaded logo rows
static void process_logo(void) {
  logo_cols = 0;
  for (int r = 0; r < logo_rows; r++)
    process_logo_row(r);
}

// --- char_weight for UTF-8 codepoints ---

static float char_weight_utf8(const char *ch) {
  // Single-byte ASCII
  if ((unsigned char)ch[0] < 0x80) {
    switch (ch[0]) {
    case 'M': return 1.00f;
    case 'N': return 0.88f;
    case 'm': return 0.76f;
    case 'd': return 0.66f;
    case 'h': return 0.56f;
    case 'b': return 0.56f;
    case 'y': return 0.46f;
    case 'o': return 0.38f;
    case 'n': return 0.38f;
    case 's': return 0.30f;
    case '+': return 0.22f;
    case ':': return 0.18f;
    case '=': return 0.22f;
    case '-': return 0.14f;
    case '`': return 0.08f;
    case '.': return 0.10f;
    case '/': return 0.12f;
    case '\'': return 0.06f;
    case ' ': return 0.0f;
    default:
      // Generic: uppercase heavy, lowercase medium, punct light
      if (ch[0] >= 'A' && ch[0] <= 'Z') return 0.80f;
      if (ch[0] >= 'a' && ch[0] <= 'z') return 0.50f;
      if (ch[0] >= '0' && ch[0] <= '9') return 0.40f;
      return 0.15f;
    }
  }

  // Multi-byte UTF-8: compare raw bytes for common block elements
  // Full block U+2588: E2 96 88
  if (memcmp(ch, "\xe2\x96\x88", 3) == 0) return 1.00f;
  // Dark shade U+2593: E2 96 93
  if (memcmp(ch, "\xe2\x96\x93", 3) == 0) return 0.75f;
  // Medium shade U+2592: E2 96 92
  if (memcmp(ch, "\xe2\x96\x92", 3) == 0) return 0.50f;
  // Light shade U+2591: E2 96 91
  if (memcmp(ch, "\xe2\x96\x91", 3) == 0) return 0.25f;

  // Half blocks (U+2580-258F)
  // Upper half U+2580: E2 96 80
  if (memcmp(ch, "\xe2\x96\x80", 3) == 0) return 0.50f;
  // Lower half U+2584: E2 96 84
  if (memcmp(ch, "\xe2\x96\x84", 3) == 0) return 0.50f;
  // Left half U+258C: E2 96 8C
  if (memcmp(ch, "\xe2\x96\x8c", 3) == 0) return 0.50f;
  // Right half U+2590: E2 96 90
  if (memcmp(ch, "\xe2\x96\x90", 3) == 0) return 0.50f;

  // 3/4 blocks
  // U+259B ▛: E2 96 9B
  if (memcmp(ch, "\xe2\x96\x9b", 3) == 0) return 0.75f;
  // U+259C ▜: E2 96 9C
  if (memcmp(ch, "\xe2\x96\x9c", 3) == 0) return 0.75f;
  // U+2599 ▙: E2 96 99
  if (memcmp(ch, "\xe2\x96\x99", 3) == 0) return 0.75f;
  // U+259F ▟: E2 96 9F
  if (memcmp(ch, "\xe2\x96\x9f", 3) == 0) return 0.75f;

  // 1/4 blocks
  // U+2596 ▖: E2 96 96
  if (memcmp(ch, "\xe2\x96\x96", 3) == 0) return 0.25f;
  // U+2597 ▗: E2 96 97
  if (memcmp(ch, "\xe2\x96\x97", 3) == 0) return 0.25f;
  // U+2598 ▘: E2 96 98
  if (memcmp(ch, "\xe2\x96\x98", 3) == 0) return 0.25f;
  // U+259D ▝: E2 96 9D
  if (memcmp(ch, "\xe2\x96\x9d", 3) == 0) return 0.25f;

  // Box drawing chars (U+2500-257F): E2 94 xx / E2 95 xx
  if ((unsigned char)ch[0] == 0xe2 &&
      ((unsigned char)ch[1] == 0x94 || (unsigned char)ch[1] == 0x95))
    return 0.20f;

  // Braille (U+2800-28FF): E2 A0-A3 xx
  if ((unsigned char)ch[0] == 0xe2 &&
      (unsigned char)ch[1] >= 0xa0 && (unsigned char)ch[1] <= 0xa3) {
    // Weight by number of dots (popcount of last byte)
    unsigned char b = (unsigned char)ch[2];
    int dots = 0;
    while (b) { dots += b & 1; b >>= 1; }
    return dots / 8.0f;
  }

  // Default for unknown multi-byte: treat as medium fill
  return 0.30f;
}

static char file_distro[64] = "";
static char detected_distro[64] = ""; // filled in main, used by {icon} expansion

static int load_logo_file(void) {
  char path[512];
  const char *home = getenv("HOME");
  if (!home)
    return 0;
  snprintf(path, sizeof(path), "%s/.config/fetch/logo.txt", home);
  FILE *fp = fopen(path, "r");
  if (!fp)
    return 0;

  char buf[512];
  while (logo_rows < MAX_LOGO_ROWS && fgets(buf, sizeof(buf), fp)) {
    int len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
      buf[--len] = '\0';
    if (logo_rows == 0 && strncmp(buf, "# distro:", 9) == 0) {
      char *val = buf + 9;
      while (*val == ' ')
        val++;
      strncpy(file_distro, val, sizeof(file_distro) - 1);
      continue;
    }
    if (len == 0 && logo_rows == 0)
      continue;
    memcpy(logo_data[logo_rows], buf, len + 1);
    logo_rows++;
  }
  fclose(fp);
  while (logo_rows > 0 && logo_data[logo_rows - 1][0] == '\0')
    logo_rows--;
  return logo_rows > 0;
}

// Check if an ANSI escape is a cursor movement (not a color/SGR escape)
static int is_cursor_escape(const char *p) {
  if (p[0] != '\033' || p[1] != '[')
    return 0;
  int i = 2;
  while (p[i] && ((p[i] >= '0' && p[i] <= '9') || p[i] == ';'))
    i++;
  return (p[i] && p[i] != 'm');
}

static int load_logo_fastfetch(const char *name) {
  char cmd[256];
  snprintf(cmd, sizeof(cmd),
           "fastfetch -l %s --structure \"\" --pipe false 2>/dev/null", name);
  FILE *fp = popen(cmd, "r");
  if (!fp)
    return 0;

  char buf[512];
  while (logo_rows < MAX_LOGO_ROWS && fgets(buf, sizeof(buf), fp)) {
    int len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
      buf[--len] = '\0';

    // Truncate at first cursor movement escape (marks end of logo content)
    int truncated = 0;
    for (int i = 0; i < len - 2; i++) {
      if (is_cursor_escape(&buf[i])) {
        // Strip preceding \033[m or \033[0m reset too
        int cut = i;
        if (cut >= 3 && buf[cut - 1] == 'm' && buf[cut - 2] == '[' &&
            buf[cut - 3] == '\033')
          cut -= 3;
        else if (cut >= 4 && buf[cut - 1] == 'm' && buf[cut - 2] == '0' &&
                 buf[cut - 3] == '[' && buf[cut - 4] == '\033')
          cut -= 4;
        buf[cut] = '\0';
        len = cut;
        truncated = 1;
        break;
      }
    }

    if (len == 0 && logo_rows == 0)
      continue;
    // Only stop on empty lines caused by cursor escapes (system info),
    // not blank lines that are part of the logo
    if (len == 0 && truncated)
      break;

    memcpy(logo_data[logo_rows], buf, len + 1);
    logo_rows++;
  }
  pclose(fp);

  while (logo_rows > 0 && logo_data[logo_rows - 1][0] == '\0')
    logo_rows--;
  return logo_rows > 0;
}

// Parse a value from os-release, stripping quotes and newlines
static int parse_os_release_val(const char *buf, int prefix_len, char *out,
                                int maxlen) {
  int len = strlen(buf);
  char tmp[256];
  if (len - prefix_len >= (int)sizeof(tmp))
    return 0;
  memcpy(tmp, buf + prefix_len, len - prefix_len + 1);
  len = strlen(tmp);
  while (len > 0 && (tmp[len - 1] == '\n' || tmp[len - 1] == '\r'))
    tmp[--len] = '\0';
  char *val = tmp;
  if (*val == '"')
    val++;
  len = strlen(val);
  if (len > 0 && val[len - 1] == '"')
    val[--len] = '\0';
  if (len > 0 && len < maxlen) {
    memcpy(out, val, len + 1);
    return 1;
  }
  return 0;
}

// Try to detect distro using fastfetch --json first (it's smarter than
// os-release, e.g. it detects Proxmox even though ID=debian).
// Falls back to /etc/os-release if fastfetch isn't available.
static char distro_id_like[64] = "";

static int detect_distro_fastfetch(char *out, int maxlen) {
  FILE *fp = popen("fastfetch --json 2>/dev/null", "r");
  if (!fp)
    return 0;
  char buf[1024];
  int found_os = 0;
  while (fgets(buf, sizeof(buf), fp)) {
    // Look for "id": "..." after "type": "OS"
    if (strstr(buf, "\"OS\""))
      found_os = 1;
    if (found_os) {
      char *id_pos = strstr(buf, "\"id\"");
      if (id_pos) {
        // Extract value: "id": "gentoo"
        char *colon = strchr(id_pos, ':');
        if (colon) {
          char *q1 = strchr(colon, '"');
          if (q1) {
            q1++;
            char *q2 = strchr(q1, '"');
            if (q2 && q2 - q1 > 0 && q2 - q1 < maxlen) {
              memcpy(out, q1, q2 - q1);
              out[q2 - q1] = '\0';
              pclose(fp);
              return 1;
            }
          }
        }
      }
      // Also grab idLike
      char *like_pos = strstr(buf, "\"idLike\"");
      if (like_pos) {
        char *colon = strchr(like_pos, ':');
        if (colon) {
          char *q1 = strchr(colon, '"');
          if (q1) {
            q1++;
            char *q2 = strchr(q1, '"');
            if (q2 && q2 - q1 > 0 && q2 - q1 < (int)sizeof(distro_id_like)) {
              memcpy(distro_id_like, q1, q2 - q1);
              distro_id_like[q2 - q1] = '\0';
            }
          }
        }
      }
    }
  }
  pclose(fp);
  return 0;
}

static int detect_distro_os_release(char *out, int maxlen) {
  FILE *fp = fopen("/etc/os-release", "r");
  if (!fp)
    return 0;
  char buf[256];
  int found_id = 0;
  while (fgets(buf, sizeof(buf), fp)) {
    if (!found_id && strncmp(buf, "ID=", 3) == 0) {
      found_id = parse_os_release_val(buf, 3, out, maxlen);
    } else if (strncmp(buf, "ID_LIKE=", 8) == 0) {
      parse_os_release_val(buf, 8, distro_id_like, sizeof(distro_id_like));
    }
  }
  fclose(fp);
  return found_id;
}

static int detect_distro(char *out, int maxlen) {
  if (detect_distro_fastfetch(out, maxlen))
    return 1;
  return detect_distro_os_release(out, maxlen);
}

static void load_default_logo(void) {
  static const char *gentoo[] = {
      "         -/oyddmdhs+:.            ",
      "     -odNMMMMMMMMNNmhy+-`         ",
      "   -yNMMMMMMMMMMMNNNmmdhy+-       ",
      " `omMMMMMMMMMMMMNmdmmmmddhhy/`    ",
      " omMMMMMMMMMMMNhhyyyohmdddhhhdo`  ",
      ".ydMMMMMMMMMMdhs++so/smdddhhhhdm+`",
      " oyhdmNMMMMMMMNdyooydMddddhhhhyhNd.",
      "  :oyhhdNNMMMMMMMNNMMMdddhhhhhyymMh",
      "    .:+sydNMMMMMNNMMMMdddhhhhhhmMmy",
      "       /mMMMMMMNNNMMMdddhhhhhmMNhs:",
      "    `oNMMMMMMMNNNMMMddddhhdmMNhs+` ",
      "  `sNMMMMMMMMNNNMMMdddddmNMmhs/.   ",
      " /NMMMMMMMMNNNNMMMdddmNMNdso:`     ",
      "+MMMMMMMNNNNNMMMMdMNMNdso/-        ",
      "yMMNNNNNNNMMMMMNNMmhs+/-`          ",
      "/hMMNNNNNNNNMNdhs++/-`             ",
      "`/ohdmmddhys+++/:.`                ",
      "  `-//////:--.                     ",
  };
  logo_rows = 18;
  for (int i = 0; i < logo_rows; i++) {
    int len = strlen(gentoo[i]);
    memcpy(logo_data[i], gentoo[i], len + 1);
  }
}

#define MAX_POINTS 80000
static float PX[MAX_POINTS], PY[MAX_POINTS], PZ[MAX_POINTS];
static float NX[MAX_POINTS], NY[MAX_POINTS], NZ[MAX_POINTS];
static float PWEIGHT[MAX_POINTS];
static int PCOLOR[MAX_POINTS];
static int POINT_COUNT = 0;

// Fastfetch output storage
#define MAX_FETCH_LINES 32
#define MAX_LINE_LEN 512
static char fetch_lines[MAX_FETCH_LINES][MAX_LINE_LEN];
static int fetch_line_count = 0;

// --- Config ---
enum {
  F_OS, F_HOST, F_KERNEL, F_UPTIME, F_PACKAGES, F_SHELL, F_DISPLAY, F_WM,
  F_THEME, F_ICONS, F_FONT, F_TERMINAL, F_CPU, F_GPU, F_MEMORY, F_SWAP,
  F_DISK, F_IP, F_BATTERY, F_LOCALE, F_COLORS,
  F_TITLE, F_SEPARATOR, F_BREAK,
  F_DE, F_LM, F_BRIGHTNESS, F_POWERADAPTER,
  F_SOUND, F_BLUETOOTH, F_WIFI, F_PLAYER, F_MEDIA,
  F_COUNT
};

#define MAX_FIELD_SEQ 64

static int field_enabled[F_COUNT];
static int field_order[MAX_FIELD_SEQ];
static int field_line[F_COUNT]; // line index for each field (-1 if not shown)
static int current_field = -1;  // which field is currently being gathered
static int field_count = 0;
static char label_color[16] = "35"; // default magenta
static int config_height = 0;     // 0 = auto (match info lines)
static float size_scale = 1.0f;
static float config_speed = 0.0f;    // 0 = use flag/default
static int config_spin_x = -1;       // -1 = use flag/default
static int config_spin_y = -1;
static char config_shading[128] = "";
static char config_separator[8] = "-";
// Per-sequence-entry overrides (applies to both normal and custom modules).
static char seq_label_override[MAX_FIELD_SEQ][64];
static char seq_color_override[MAX_FIELD_SEQ][16];
static char seq_custom_format[MAX_FIELD_SEQ][512];
static char seq_custom_color[MAX_FIELD_SEQ][32];
static char seq_format[MAX_FIELD_SEQ][256]; // per-module `format` string
static int current_seq_idx = -1;      // sequence slot for the in-flight gather
static int field_seq_idx[F_COUNT];    // maps a field id to its first seq slot
static int is_refresh_pass = 0;        // 1 during the animation-loop refresh gather

static int ff_mode_active = 0;       // 1 when fastfetch config drives ordering
static char config_ff_setting[512] = ""; // fetch config: "", "auto", "off", or path

#define MAX_FF_CONSTANTS 8
#define MAX_FF_CONSTANT_LEN 256
static char ff_separator_str[32] = ": ";
static char ff_constants[MAX_FF_CONSTANTS][MAX_FF_CONSTANT_LEN];
static int ff_constants_count = 0;

// Light direction presets
static float light_x = 0.4082f, light_y = 0.8165f, light_z = -0.4082f;

static const struct {
  const char *name;
  int id;
} field_map[] = {
    {"os", F_OS},           {"host", F_HOST},
    {"kernel", F_KERNEL},   {"uptime", F_UPTIME},
    {"packages", F_PACKAGES}, {"shell", F_SHELL},
    {"display", F_DISPLAY}, {"wm", F_WM},
    {"theme", F_THEME},     {"icons", F_ICONS},
    {"font", F_FONT},       {"terminal", F_TERMINAL},
    {"cpu", F_CPU},         {"gpu", F_GPU},
    {"memory", F_MEMORY},   {"swap", F_SWAP},
    {"disk", F_DISK},       {"ip", F_IP},
    {"battery", F_BATTERY}, {"locale", F_LOCALE},
    {"colors", F_COLORS},   {NULL, 0}
};

static void config_defaults(void) {
  // Default order
  int defaults[] = {F_OS, F_HOST, F_KERNEL, F_UPTIME, F_PACKAGES, F_SHELL,
                    F_DISPLAY, F_WM, F_THEME, F_ICONS, F_FONT, F_TERMINAL,
                    F_CPU, F_GPU, F_MEMORY, F_SWAP, F_DISK, F_IP, F_BATTERY,
                    F_LOCALE, F_COLORS};
  field_count = sizeof(defaults) / sizeof(defaults[0]);
  for (int i = 0; i < field_count; i++) {
    field_order[i] = defaults[i];
    field_enabled[defaults[i]] = 1;
  }
}

static void load_config(void) {
  const char *home = getenv("HOME");
  if (!home)
    return;
  char path[512];
  snprintf(path, sizeof(path), "%s/.config/fetch/config", home);
  FILE *fp = fopen(path, "r");
  if (!fp)
    return;

  // Only reset defaults when the file actually lists fields.
  // A config with only directives (e.g., fastfetch_config=) leaves defaults
  // in place so the user gets fetch's normal field set.
  int cleared = 0;

  char buf[256];
  while (fgets(buf, sizeof(buf), fp)) {
    int len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' ||
                       buf[len - 1] == ' '))
      buf[--len] = '\0';
    // Skip comments and empty lines
    char *line = buf;
    while (*line == ' ' || *line == '\t')
      line++;
    if (*line == '#' || *line == '\0')
      continue;

    // Check for key=value settings
    if (strncmp(line, "label_color=", 12) == 0) {
      char *val = line + 12;
      // Accept color names or numbers
      if (strcmp(val, "red") == 0) strcpy(label_color, "31");
      else if (strcmp(val, "green") == 0) strcpy(label_color, "32");
      else if (strcmp(val, "yellow") == 0) strcpy(label_color, "33");
      else if (strcmp(val, "blue") == 0) strcpy(label_color, "34");
      else if (strcmp(val, "magenta") == 0) strcpy(label_color, "35");
      else if (strcmp(val, "cyan") == 0) strcpy(label_color, "36");
      else if (strcmp(val, "white") == 0) strcpy(label_color, "37");
      else strncpy(label_color, val, sizeof(label_color) - 1);
      continue;
    }
    if (strncmp(line, "height=", 7) == 0) {
      config_height = atoi(line + 7);
      if (config_height > MAX_HEIGHT)
        config_height = MAX_HEIGHT;
      continue;
    }
    if (strncmp(line, "size=", 5) == 0) {
      size_scale = atof(line + 5);
      if (size_scale < 0.5f) size_scale = 0.5f;
      if (size_scale > 5.0f) size_scale = 5.0f;
      continue;
    }
    if (strncmp(line, "speed=", 6) == 0) {
      config_speed = atof(line + 6);
      continue;
    }
    if (strncmp(line, "spin=", 5) == 0) {
      char *val = line + 5;
      config_spin_x = (strchr(val, 'x') || strchr(val, 'X')) ? 1 : 0;
      config_spin_y = (strchr(val, 'y') || strchr(val, 'Y')) ? 1 : 0;
      continue;
    }
    if (strncmp(line, "shading=", 8) == 0) {
      strncpy(config_shading, line + 8, sizeof(config_shading) - 1);
      continue;
    }
    if (strncmp(line, "separator=", 10) == 0) {
      strncpy(config_separator, line + 10, sizeof(config_separator) - 1);
      continue;
    }
    if (strncmp(line, "fastfetch_config=", 17) == 0) {
      strncpy(config_ff_setting, line + 17, sizeof(config_ff_setting) - 1);
      config_ff_setting[sizeof(config_ff_setting) - 1] = '\0';
      continue;
    }
    if (strncmp(line, "light=", 6) == 0) {
      char *val = line + 6;
      if (strcmp(val, "top-left") == 0) {
        light_x = 0.41f; light_y = 0.82f; light_z = -0.41f;
      } else if (strcmp(val, "top-right") == 0) {
        light_x = -0.41f; light_y = 0.82f; light_z = -0.41f;
      } else if (strcmp(val, "top") == 0) {
        light_x = 0.0f; light_y = 0.89f; light_z = -0.45f;
      } else if (strcmp(val, "left") == 0) {
        light_x = 0.82f; light_y = 0.41f; light_z = -0.41f;
      } else if (strcmp(val, "right") == 0) {
        light_x = -0.82f; light_y = 0.41f; light_z = -0.41f;
      } else if (strcmp(val, "front") == 0) {
        light_x = 0.0f; light_y = 0.0f; light_z = -1.0f;
      } else if (strcmp(val, "bottom-left") == 0) {
        light_x = 0.41f; light_y = -0.82f; light_z = -0.41f;
      } else if (strcmp(val, "bottom-right") == 0) {
        light_x = -0.41f; light_y = -0.82f; light_z = -0.41f;
      }
      continue;
    }

    // Match field name
    for (int i = 0; field_map[i].name; i++) {
      if (strcasecmp(line, field_map[i].name) == 0) {
        if (!cleared) {
          for (int k = 0; k < F_COUNT; k++) field_enabled[k] = 0;
          field_count = 0;
          cleared = 1;
        }
        int id = field_map[i].id;
        if (!field_enabled[id] && field_count < MAX_FIELD_SEQ) {
          field_enabled[id] = 1;
          field_order[field_count++] = id;
        }
        break;
      }
    }
  }
  fclose(fp);
}

static void add_line(const char *line) {
  if (fetch_line_count >= MAX_FETCH_LINES)
    return;
  strncpy(fetch_lines[fetch_line_count], line, MAX_LINE_LEN - 1);
  fetch_lines[fetch_line_count][MAX_LINE_LEN - 1] = '\0';
  fetch_line_count++;
}

static const char *effective_label_color(void) {
  if (current_seq_idx >= 0 && current_seq_idx < MAX_FIELD_SEQ &&
      seq_color_override[current_seq_idx][0])
    return seq_color_override[current_seq_idx];
  return label_color;
}

static const char *ff_distro_glyph(const char *distro) {
  if (!distro || !*distro) return "\uf17c";
  struct { const char *id; const char *g; } map[] = {
      {"gentoo", "\uf08d"},    {"arch", "\uf303"},
      {"archlinux", "\uf303"}, {"nixos", "\uf313"},
      {"fedora", "\uf30a"},    {"debian", "\uf306"},
      {"ubuntu", "\uf31b"},    {"manjaro", "\uf312"},
      {"opensuse", "\uf314"},  {"void", "\uf32e"},
      {"alpine", "\uf300"},    {"pop", "\uf32a"},
      {"linuxmint", "\uf30e"}, {"mint", "\uf30e"},
      {"endeavouros", "\uf322"}, {"asahi", "\uf302"},
      {"fedora-asahi-remix", "\uf302"}, {NULL, NULL}};
  for (int i = 0; map[i].id; i++)
    if (strcasecmp(distro, map[i].id) == 0) return map[i].g;
  return "\uf17c";
}

static char effective_label_buf[128];

static const char *effective_label(const char *fallback) {
  if (current_seq_idx < 0 || current_seq_idx >= MAX_FIELD_SEQ ||
      !seq_label_override[current_seq_idx][0])
    return fallback;
  const char *ov = seq_label_override[current_seq_idx];
  const char *icon = strstr(ov, "{icon}");
  if (!icon) return ov;
  const char *glyph = ff_distro_glyph(detected_distro);
  int prefix = (int)(icon - ov);
  snprintf(effective_label_buf, sizeof(effective_label_buf), "%.*s%s%s",
           prefix, ov, glyph, icon + 6);
  return effective_label_buf;
}

// Format a labeled info line using the configured label color.
// If the current field already has a line index, update it in place.
static void add_info(const char *label, const char *fmt, ...) {
  char val[MAX_LINE_LEN];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(val, sizeof(val), fmt, ap);
  va_end(ap);

  char line[MAX_LINE_LEN];
  const char *sep = ff_mode_active ? ff_separator_str : ": ";
  snprintf(line, sizeof(line), "\033[1;%sm%s\033[0m%s%s",
           effective_label_color(), effective_label(label), sep, val);

  // Refresh pass: update the same field's line in place (for uptime/memory/swap ticks)
  if (is_refresh_pass && current_field >= 0 && field_line[current_field] >= 0) {
    int idx = field_line[current_field];
    strncpy(fetch_lines[idx], line, MAX_LINE_LEN - 1);
    fetch_lines[idx][MAX_LINE_LEN - 1] = '\0';
    return;
  }
  // Initial pass: always append a new line; track its index for refreshable fields
  if (current_field >= 0)
    field_line[current_field] = fetch_line_count;
  add_line(line);
}

static int title_len_cached = 20;

// Substitute {N} tokens in fmt with tokens[N-1]. Writes up to outlen-1 bytes.
static void ff_apply_tokens(const char *fmt, const char **tokens, int ntokens,
                            char *out, int outlen) {
  int oi = 0;
  for (int i = 0; fmt[i] && oi + 1 < outlen;) {
    if (fmt[i] == '{' && fmt[i + 1] >= '0' && fmt[i + 1] <= '9') {
      int j = i + 1;
      int num = 0;
      while (fmt[j] >= '0' && fmt[j] <= '9') {
        num = num * 10 + (fmt[j] - '0');
        j++;
      }
      if (fmt[j] == '}' && num >= 1 && num <= ntokens && tokens[num - 1]) {
        int len = (int)strlen(tokens[num - 1]);
        if (oi + len >= outlen) len = outlen - oi - 1;
        memcpy(out + oi, tokens[num - 1], len);
        oi += len;
        i = j + 1;
        continue;
      }
    }
    out[oi++] = fmt[i++];
  }
  out[oi] = '\0';
}

// Emit an info line using the per-module format (if set by a fastfetch config)
// or the provided default format. Both are token-based: {1}, {2}, ...
static void add_info_toks(const char *label, const char **tokens, int ntokens,
                          const char *default_fmt) {
  const char *fmt = (current_seq_idx >= 0 && current_seq_idx < MAX_FIELD_SEQ &&
                     seq_format[current_seq_idx][0])
                        ? seq_format[current_seq_idx]
                        : default_fmt;
  char buf[MAX_LINE_LEN];
  ff_apply_tokens(fmt, tokens, ntokens, buf, sizeof(buf));
  add_info(label, "%s", buf);
}

static void gather_title_line(void) {
  char user[64] = "";
  char host[64] = "";
  char *login = getlogin();
  if (login)
    strncpy(user, login, sizeof(user) - 1);
  else {
    char *env = getenv("USER");
    if (env)
      strncpy(user, env, sizeof(user) - 1);
  }
  gethostname(host, sizeof(host));

  const char *color = effective_label_color();
  char line[MAX_LINE_LEN];
  snprintf(line, sizeof(line), "\033[1;%sm%s\033[0m@\033[1;%sm%s\033[0m",
           color, user, color, host);
  add_line(line);
  title_len_cached = strlen(user) + 1 + strlen(host);
}

static void gather_separator_line(void) {
  char sep[MAX_LINE_LEN];
  int sep_char_len = strlen(config_separator);
  if (sep_char_len == 0) sep_char_len = 1;
  int pos = 0;
  for (int i = 0; i < title_len_cached && pos + sep_char_len < MAX_LINE_LEN; i++) {
    memcpy(sep + pos, config_separator, sep_char_len);
    pos += sep_char_len;
  }
  sep[pos] = '\0';
  add_line(sep);
}

static void gather_os(void) {
  char pretty[128] = "";
  FILE *fp = fopen("/etc/os-release", "r");
  if (fp) {
    char buf[256];
    while (fgets(buf, sizeof(buf), fp)) {
      if (strncmp(buf, "PRETTY_NAME=", 12) == 0) {
        char *val = buf + 12;
        int len = strlen(val);
        while (len > 0 && (val[len - 1] == '\n' || val[len - 1] == '\r'))
          val[--len] = '\0';
        if (*val == '\'' || *val == '"')
          val++;
        len = strlen(val);
        if (len > 0 && (val[len - 1] == '\'' || val[len - 1] == '"'))
          val[--len] = '\0';
        strncpy(pretty, val, sizeof(pretty) - 1);
        break;
      }
    }
    fclose(fp);
  }
  if (!pretty[0])
    strcpy(pretty, "Linux");

  struct utsname u;
  uname(&u);

  add_info("OS", "%s %s", pretty, u.machine);
}

static void gather_host(void) {
  char model[128] = "";
  // Try device-tree first (ARM/Apple Silicon), then DMI (x86)
  FILE *fp = fopen("/proc/device-tree/model", "r");
  if (!fp)
    fp = fopen("/sys/class/dmi/id/product_name", "r");
  if (fp) {
    if (fgets(model, sizeof(model), fp)) {
      int len = strlen(model);
      while (len > 0 && (model[len - 1] == '\n' || model[len - 1] == '\r' ||
                         model[len - 1] == '\0'))
        len--;
      model[len] = '\0';
    }
    fclose(fp);
  }
  if (model[0])
    add_info("Host", "%s", model);
}

static void gather_kernel(void) {
  struct utsname u;
  uname(&u);
  add_info("Kernel", "%s %s", u.sysname, u.release);
}

static void gather_uptime(void) {
  FILE *fp = fopen("/proc/uptime", "r");
  if (!fp)
    return;
  double secs = 0;
  if (fscanf(fp, "%lf", &secs) != 1)
    secs = 0;
  fclose(fp);

  int total = (int)secs;
  int days = total / 86400;
  int hours = (total % 86400) / 3600;
  int mins = (total % 3600) / 60;

  char val[128];
  if (days > 0)
    snprintf(val, sizeof(val), "%d day%s, %d hour%s, %d min%s", days,
             days == 1 ? "" : "s", hours, hours == 1 ? "" : "s", mins,
             mins == 1 ? "" : "s");
  else if (hours > 0)
    snprintf(val, sizeof(val), "%d hour%s, %d min%s", hours,
             hours == 1 ? "" : "s", mins, mins == 1 ? "" : "s");
  else
    snprintf(val, sizeof(val), "%d min%s", mins, mins == 1 ? "" : "s");

  add_info("Uptime", "%s", val);
}

static int count_dir_entries(const char *path) {
  int count = 0;
  FILE *fp = popen(path, "r");
  if (!fp)
    return 0;
  char buf[512];
  while (fgets(buf, sizeof(buf), fp))
    count++;
  pclose(fp);
  return count;
}

static void gather_packages(void) {
  char val[128] = "";
  int n;

  // emerge (Gentoo)
  n = count_dir_entries("ls -d /var/db/pkg/*/* 2>/dev/null");
  if (n > 0) {
    snprintf(val, sizeof(val), "%d (emerge)", n);
  }
  // pacman (Arch)
  if (!val[0]) {
    n = count_dir_entries(
        "ls -d /var/lib/pacman/local/*-* 2>/dev/null");
    if (n > 0)
      snprintf(val, sizeof(val), "%d (pacman)", n);
  }
  // dpkg (Debian/Ubuntu)
  if (!val[0]) {
    n = count_dir_entries("dpkg-query -f '.\n' -W 2>/dev/null");
    if (n > 0)
      snprintf(val, sizeof(val), "%d (dpkg)", n);
  }
  // rpm (Fedora/RHEL)
  if (!val[0]) {
    n = count_dir_entries("rpm -qa 2>/dev/null");
    if (n > 0)
      snprintf(val, sizeof(val), "%d (rpm)", n);
  }
  // xbps (Void)
  if (!val[0]) {
    n = count_dir_entries("xbps-query -l 2>/dev/null");
    if (n > 0)
      snprintf(val, sizeof(val), "%d (xbps)", n);
  }
  // apk (Alpine)
  if (!val[0]) {
    n = count_dir_entries("apk list --installed 2>/dev/null");
    if (n > 0)
      snprintf(val, sizeof(val), "%d (apk)", n);
  }

  if (val[0])
    add_info("Packages", "%s", val);
}

static void gather_shell(void) {
  char *shell = getenv("SHELL");
  if (!shell)
    return;

  // Get just the basename
  char *name = strrchr(shell, '/');
  name = name ? name + 1 : shell;

  // Try to get version
  char version[128] = "";
  char cmd[256];
  snprintf(cmd, sizeof(cmd), "%s --version 2>/dev/null", shell);
  FILE *fp = popen(cmd, "r");
  if (fp) {
    char buf[256];
    if (fgets(buf, sizeof(buf), fp)) {
      // Extract version number from first line
      // e.g. "zsh 5.9.0.3-test (aarch64...)" or "bash 5.2.26(1)-release"
      char *p = buf;
      // Find the version part after the shell name
      char *ver = strstr(buf, name);
      if (ver) {
        ver += strlen(name);
        while (*ver == ' ')
          ver++;
      } else {
        // Try to find first digit
        ver = buf;
        while (*ver && !(*ver >= '0' && *ver <= '9'))
          ver++;
      }
      if (*ver) {
        int len = 0;
        while (ver[len] && ver[len] != ' ' && ver[len] != '(' &&
               ver[len] != '\n' && len < 30)
          len++;
        memcpy(version, ver, len);
        version[len] = '\0';
      }
    }
    pclose(fp);
  }

  if (version[0])
    add_info("Shell", "%s %s", name, version);
  else
    add_info("Shell", "%s", name);
}

static void gather_display(void) {
  DIR *d = opendir("/sys/class/drm");
  if (!d) return;
  struct dirent *ent;
  int emitted = 0;
  while ((ent = readdir(d))) {
    // Pattern: cardN-CONNECTOR (skip bare cardN and renderD*)
    if (strncmp(ent->d_name, "card", 4) != 0) continue;
    const char *dash = strchr(ent->d_name + 4, '-');
    if (!dash) continue;

    char path[256];
    snprintf(path, sizeof(path), "/sys/class/drm/%s/status", ent->d_name);
    FILE *fp = fopen(path, "r");
    if (!fp) continue;
    char status[32] = "";
    if (fgets(status, sizeof(status), fp)) {
      int l = strlen(status);
      while (l > 0 && (status[l - 1] == '\n' || status[l - 1] == '\r'))
        status[--l] = '\0';
    }
    fclose(fp);
    if (strcmp(status, "connected") != 0) continue;

    snprintf(path, sizeof(path), "/sys/class/drm/%s/modes", ent->d_name);
    fp = fopen(path, "r");
    if (!fp) continue;
    char mode[32] = "";
    if (fgets(mode, sizeof(mode), fp)) {
      int l = strlen(mode);
      while (l > 0 && (mode[l - 1] == '\n' || mode[l - 1] == '\r'))
        mode[--l] = '\0';
    }
    fclose(fp);
    if (!mode[0]) continue;

    add_info("Display", "%s @ %s", dash + 1, mode);
    emitted++;
  }
  closedir(d);

  // Fallback for drivers that don't expose per-connector modes (rare on Linux).
  if (!emitted) {
    FILE *fp = popen("cat /sys/class/drm/card*/modes 2>/dev/null", "r");
    if (!fp) return;
    char buf[64] = "";
    if (fgets(buf, sizeof(buf), fp)) {
      int l = strlen(buf);
      while (l > 0 && (buf[l - 1] == '\n' || buf[l - 1] == '\r'))
        buf[--l] = '\0';
      if (buf[0]) add_info("Display", "%s", buf);
    }
    pclose(fp);
  }
}

static const char *de_to_wm(const char *desktop) {
  if (!desktop || !desktop[0]) return NULL;
  // XDG_CURRENT_DESKTOP may be colon-separated (e.g. "KDE:Plasma"); match first token
  char first[32];
  int i = 0;
  while (desktop[i] && desktop[i] != ':' && i < (int)sizeof(first) - 1) {
    first[i] = desktop[i];
    i++;
  }
  first[i] = '\0';
  if (strcasecmp(first, "KDE") == 0) return "KWin";
  if (strcasecmp(first, "GNOME") == 0) return "Mutter";
  if (strcasecmp(first, "XFCE") == 0) return "xfwm4";
  if (strcasecmp(first, "Cinnamon") == 0) return "Muffin";
  if (strcasecmp(first, "MATE") == 0) return "Marco";
  if (strcasecmp(first, "LXQt") == 0) return "Openbox";
  if (strcasecmp(first, "Budgie") == 0) return "Mutter";
  if (strcasecmp(first, "Deepin") == 0) return "KWin";
  return NULL;
}

static void gather_wm(void) {
  // Check WAYLAND_DISPLAY or XDG_SESSION_TYPE to determine session type
  char *wayland = getenv("WAYLAND_DISPLAY");
  char *session = getenv("XDG_SESSION_TYPE");
  char *desktop = getenv("XDG_CURRENT_DESKTOP");
  int is_wayland = (wayland && wayland[0]) ||
                   (session && strcmp(session, "wayland") == 0);

  // Try to figure out the WM name
  char wm[64] = "";
  const char *mapped = de_to_wm(desktop);
  if (mapped) {
    strncpy(wm, mapped, sizeof(wm) - 1);
  } else if (desktop && desktop[0]) {
    strncpy(wm, desktop, sizeof(wm) - 1);
  } else {
    // Try common WM env vars / process detection
    char *swaysock = getenv("SWAYSOCK");
    char *hyprland = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (swaysock)
      strcpy(wm, "sway");
    else if (hyprland)
      strcpy(wm, "Hyprland");
    else {
      // Try process list for known WMs
      FILE *fp = popen("ps -e -o comm= 2>/dev/null", "r");
      if (fp) {
        char buf[64];
        while (fgets(buf, sizeof(buf), fp)) {
          int len = strlen(buf);
          while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
            buf[--len] = '\0';
          if (strcmp(buf, "dwl") == 0 || strcmp(buf, "sway") == 0 ||
              strcmp(buf, "river") == 0 || strcmp(buf, "labwc") == 0 ||
              strcmp(buf, "weston") == 0 || strcmp(buf, "i3") == 0 ||
              strcmp(buf, "bspwm") == 0 || strcmp(buf, "openbox") == 0 ||
              strcmp(buf, "awesome") == 0 || strcmp(buf, "dwm") == 0) {
            strncpy(wm, buf, sizeof(wm) - 1);
            break;
          }
        }
        pclose(fp);
      }
    }
  }

  if (wm[0])
    add_info("WM", "%s%s", wm, is_wayland ? " (Wayland)" : "");
}

static void gather_de(void) {
  char *desktop = getenv("XDG_CURRENT_DESKTOP");
  if (!desktop || !desktop[0]) return;

  // Normalise name (first token of colon list)
  char name[64] = "";
  int i = 0;
  while (desktop[i] && desktop[i] != ':' && i < (int)sizeof(name) - 1) {
    name[i] = desktop[i];
    i++;
  }
  name[i] = '\0';

  // Pretty-print common variants
  const char *pretty = name;
  if (strcasecmp(name, "KDE") == 0) pretty = "KDE Plasma";
  else if (strcasecmp(name, "GNOME") == 0) pretty = "GNOME";

  // Try to get a version
  char version[64] = "";
  const char *vcmd = NULL;
  if (strcasecmp(name, "KDE") == 0)
    vcmd = "plasmashell --version 2>/dev/null";
  else if (strcasecmp(name, "GNOME") == 0)
    vcmd = "gnome-shell --version 2>/dev/null";
  else if (strcasecmp(name, "XFCE") == 0)
    vcmd = "xfce4-session --version 2>/dev/null";
  else if (strcasecmp(name, "Cinnamon") == 0)
    vcmd = "cinnamon --version 2>/dev/null";
  else if (strcasecmp(name, "MATE") == 0)
    vcmd = "mate-session --version 2>/dev/null";

  if (vcmd) {
    FILE *fp = popen(vcmd, "r");
    if (fp) {
      char buf[128];
      if (fgets(buf, sizeof(buf), fp)) {
        char *v = buf;
        while (*v && !(*v >= '0' && *v <= '9')) v++;
        if (*v) {
          int n = 0;
          while (v[n] && v[n] != ' ' && v[n] != '\n' && v[n] != '\r' &&
                 n < (int)sizeof(version) - 1) {
            version[n] = v[n];
            n++;
          }
          version[n] = '\0';
        }
      }
      pclose(fp);
    }
  }

  if (version[0])
    add_info("DE", "%s %s", pretty, version);
  else
    add_info("DE", "%s", pretty);
}

static void gather_lm(void) {
  // /etc/systemd/system/display-manager.service is a symlink to the DM unit
  char buf[256];
  ssize_t n = readlink("/etc/systemd/system/display-manager.service", buf,
                       sizeof(buf) - 1);
  char name[64] = "";
  if (n > 0) {
    buf[n] = '\0';
    const char *base = strrchr(buf, '/');
    base = base ? base + 1 : buf;
    // Strip .service suffix
    strncpy(name, base, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    char *dot = strstr(name, ".service");
    if (dot) *dot = '\0';
  }
  // Fallback: scan process list for common DMs
  if (!name[0]) {
    FILE *fp = popen("ps -e -o comm= 2>/dev/null", "r");
    if (fp) {
      char line[64];
      while (fgets(line, sizeof(line), fp)) {
        int l = strlen(line);
        while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r'))
          line[--l] = '\0';
        if (!strcmp(line, "sddm") || !strcmp(line, "gdm") ||
            !strcmp(line, "gdm3") || !strcmp(line, "lightdm") ||
            !strcmp(line, "lxdm") || !strcmp(line, "ly")) {
          strncpy(name, line, sizeof(name) - 1);
          name[sizeof(name) - 1] = '\0';
          break;
        }
      }
      pclose(fp);
    }
  }
  if (!name[0]) return;

  char *session = getenv("XDG_SESSION_TYPE");
  if (session && session[0])
    add_info("LM", "%s (%s)", name, (strcmp(session, "wayland") == 0) ? "Wayland" :
                                    (strcmp(session, "x11") == 0) ? "X11" : session);
  else
    add_info("LM", "%s", name);
}

static void gather_brightness(void) {
  DIR *d = opendir("/sys/class/backlight");
  if (!d) return;
  struct dirent *ent;
  while ((ent = readdir(d))) {
    if (ent->d_name[0] == '.') continue;
    char path[320];
    long cur = -1, max = -1;

    snprintf(path, sizeof(path), "/sys/class/backlight/%s/brightness", ent->d_name);
    FILE *fp = fopen(path, "r");
    if (fp) {
      char buf[32];
      if (fgets(buf, sizeof(buf), fp)) cur = atol(buf);
      fclose(fp);
    }
    snprintf(path, sizeof(path), "/sys/class/backlight/%s/max_brightness", ent->d_name);
    fp = fopen(path, "r");
    if (fp) {
      char buf[32];
      if (fgets(buf, sizeof(buf), fp)) max = atol(buf);
      fclose(fp);
    }
    if (cur < 0 || max <= 0) continue;
    int pct = (int)((cur * 100 + max / 2) / max);
    add_info("Brightness", "%d%% [%s]", pct, ent->d_name);
  }
  closedir(d);
}

static void gather_poweradapter(void) {
  DIR *d = opendir("/sys/class/power_supply");
  if (!d) return;
  struct dirent *ent;
  while ((ent = readdir(d))) {
    if (ent->d_name[0] == '.') continue;
    char path[320];
    snprintf(path, sizeof(path), "/sys/class/power_supply/%s/type", ent->d_name);
    FILE *fp = fopen(path, "r");
    if (!fp) continue;
    char type[32] = "";
    if (fgets(type, sizeof(type), fp)) {
      int l = strlen(type);
      while (l > 0 && (type[l - 1] == '\n' || type[l - 1] == '\r'))
        type[--l] = '\0';
    }
    fclose(fp);
    if (strcmp(type, "Mains") != 0) continue;

    snprintf(path, sizeof(path), "/sys/class/power_supply/%s/online", ent->d_name);
    fp = fopen(path, "r");
    int online = -1;
    if (fp) {
      char buf[8];
      if (fgets(buf, sizeof(buf), fp)) online = atoi(buf);
      fclose(fp);
    }
    if (online == 1) add_info("Power Adapter", "AC Connected");
    else if (online == 0) add_info("Power Adapter", "Disconnected");
    break;
  }
  closedir(d);
}

static int read_first_line(const char *cmd, char *out, int outlen) {
  FILE *fp = popen(cmd, "r");
  if (!fp) return 0;
  int ok = 0;
  if (fgets(out, outlen, fp)) {
    int l = strlen(out);
    while (l > 0 && (out[l - 1] == '\n' || out[l - 1] == '\r'))
      out[--l] = '\0';
    ok = (l > 0);
  } else {
    out[0] = '\0';
  }
  pclose(fp);
  return ok;
}

static void gather_sound(void) {
  char desc[192] = "";
  FILE *fp = popen("wpctl inspect @DEFAULT_SINK@ 2>/dev/null", "r");
  if (fp) {
    char buf[256];
    while (fgets(buf, sizeof(buf), fp)) {
      char *p = strstr(buf, "node.description");
      if (!p) continue;
      char *eq = strchr(p, '=');
      if (!eq) continue;
      char *q = strchr(eq, '"');
      if (!q) continue;
      q++;
      char *q2 = strchr(q, '"');
      if (!q2) continue;
      int n = (int)(q2 - q);
      if (n >= (int)sizeof(desc)) n = sizeof(desc) - 1;
      memcpy(desc, q, n);
      desc[n] = '\0';
      break;
    }
    pclose(fp);
  }
  int volpct = -1;
  char vbuf[64];
  if (read_first_line("wpctl get-volume @DEFAULT_SINK@ 2>/dev/null", vbuf,
                      sizeof(vbuf))) {
    float v = 0;
    if (sscanf(vbuf, "Volume: %f", &v) == 1)
      volpct = (int)(v * 100 + 0.5f);
  }
  if (!desc[0]) {
    // Fallback: just the default sink name
    if (!read_first_line("pactl get-default-sink 2>/dev/null", desc, sizeof(desc)))
      return;
  }
  if (volpct >= 0)
    add_info("Sound", "%s (%d%%)", desc, volpct);
  else if (desc[0])
    add_info("Sound", "%s", desc);
}

static void gather_bluetooth(void) {
  FILE *fp = popen("bluetoothctl devices Connected 2>/dev/null", "r");
  if (!fp) return;
  char buf[256], out[512] = "";
  int count = 0;
  while (fgets(buf, sizeof(buf), fp)) {
    // "Device AA:BB:CC:DD:EE:FF Name Of Device"
    char *sp1 = strchr(buf, ' ');
    if (!sp1) continue;
    char *sp2 = strchr(sp1 + 1, ' ');
    if (!sp2) continue;
    char *name = sp2 + 1;
    int l = strlen(name);
    while (l > 0 && (name[l - 1] == '\n' || name[l - 1] == '\r'))
      name[--l] = '\0';
    if (!name[0]) continue;
    if (count > 0) strncat(out, ", ", sizeof(out) - strlen(out) - 1);
    strncat(out, name, sizeof(out) - strlen(out) - 1);
    count++;
  }
  pclose(fp);
  if (count > 0) add_info("Bluetooth", "%s", out);
}

static void gather_wifi(void) {
  // Prefer nmcli
  FILE *fp = popen(
      "nmcli -t -f ACTIVE,SSID,SIGNAL device wifi 2>/dev/null | grep '^yes:'",
      "r");
  if (fp) {
    char buf[256];
    if (fgets(buf, sizeof(buf), fp)) {
      int l = strlen(buf);
      while (l > 0 && (buf[l - 1] == '\n' || buf[l - 1] == '\r'))
        buf[--l] = '\0';
      // yes:SSID:SIGNAL
      char *c1 = strchr(buf, ':');
      if (c1) {
        char *ssid = c1 + 1;
        char *c2 = strchr(ssid, ':');
        if (c2) {
          *c2 = '\0';
          int sig = atoi(c2 + 1);
          if (ssid[0]) {
            if (sig > 0) add_info("Wi-Fi", "%s (%d%%)", ssid, sig);
            else add_info("Wi-Fi", "%s", ssid);
          }
          pclose(fp);
          return;
        }
      }
    }
    pclose(fp);
  }
  // Fallback: iw dev link (if iw is installed)
  fp = popen("iw dev 2>/dev/null | awk '/Interface/{i=$2} /ssid/{print i\":\"$2; exit}'", "r");
  if (fp) {
    char buf[128];
    if (fgets(buf, sizeof(buf), fp)) {
      int l = strlen(buf);
      while (l > 0 && (buf[l - 1] == '\n' || buf[l - 1] == '\r'))
        buf[--l] = '\0';
      char *c = strchr(buf, ':');
      if (c) {
        *c = '\0';
        add_info("Wi-Fi", "%s (%s)", c + 1, buf);
      }
    }
    pclose(fp);
  }
}

// List running MPRIS players. Tries `playerctl -l`; falls back to scanning
// the session bus for active org.mpris.MediaPlayer2.* services via busctl.
static void gather_player(void) {
  char out[256] = "";
  int count = 0;

  FILE *fp = popen("playerctl -l 2>/dev/null", "r");
  if (fp) {
    char buf[128];
    while (fgets(buf, sizeof(buf), fp)) {
      int l = strlen(buf);
      while (l > 0 && (buf[l - 1] == '\n' || buf[l - 1] == '\r')) buf[--l] = '\0';
      if (!buf[0]) continue;
      if (count > 0) strncat(out, ", ", sizeof(out) - strlen(out) - 1);
      strncat(out, buf, sizeof(out) - strlen(out) - 1);
      count++;
    }
    pclose(fp);
  }
  if (!count) {
    // Fallback via busctl: list session-bus names, filter MPRIS, drop activatable.
    fp = popen(
        "busctl --user list --no-legend --acquired 2>/dev/null "
        "| awk '$1 ~ /^org\\.mpris\\.MediaPlayer2\\./ "
        "{ sub(\"org.mpris.MediaPlayer2.\", \"\", $1); print $1 }'",
        "r");
    if (fp) {
      char buf[128];
      while (fgets(buf, sizeof(buf), fp)) {
        int l = strlen(buf);
        while (l > 0 && (buf[l - 1] == '\n' || buf[l - 1] == '\r')) buf[--l] = '\0';
        if (!buf[0]) continue;
        if (count > 0) strncat(out, ", ", sizeof(out) - strlen(out) - 1);
        strncat(out, buf, sizeof(out) - strlen(out) - 1);
        count++;
      }
      pclose(fp);
    }
  }
  if (count > 0) add_info("Media Player", "%s", out);
}

// Current MPRIS "Now Playing" via playerctl metadata.
static void gather_media(void) {
  char buf[256];
  if (!read_first_line(
          "playerctl metadata --format '{{artist}} - {{title}}' 2>/dev/null",
          buf, sizeof(buf)))
    return;
  // Skip empty/placeholder output when nothing is loaded.
  if (!buf[0] || !strcmp(buf, " - ") || !strcmp(buf, "-")) return;
  add_info("Media", "%s", buf);
}

static void gather_cpu(void) {
  char name[128] = "";
  int cores = 0;
  float max_ghz = 0;

  // Try x86 model name first
  FILE *fp = fopen("/proc/cpuinfo", "r");
  if (fp) {
    char buf[256];
    while (fgets(buf, sizeof(buf), fp)) {
      if (!name[0] && strncmp(buf, "model name", 10) == 0) {
        char *val = strchr(buf, ':');
        if (val) {
          val++;
          while (*val == ' ')
            val++;
          int len = strlen(val);
          while (len > 0 && (val[len - 1] == '\n' || val[len - 1] == '\r'))
            len--;
          if (len > 0 && len < (int)sizeof(name)) {
            memcpy(name, val, len);
            name[len] = '\0';
          }
        }
      }
      if (strncmp(buf, "processor", 9) == 0)
        cores++;
    }
    fclose(fp);
  }

  // ARM/Apple Silicon: extract chip from device-tree model
  if (!name[0]) {
    char model[128] = "";
    fp = fopen("/proc/device-tree/model", "r");
    if (fp) {
      if (fgets(model, sizeof(model), fp)) {
        int len = strlen(model);
        while (len > 0 && (model[len - 1] == '\n' || model[len - 1] == '\0'))
          len--;
        model[len] = '\0';
      }
      fclose(fp);
    }
    // Extract chip name like "M1" from "Apple MacBook Air (M1, 2020)"
    char *paren = strchr(model, '(');
    if (paren) {
      paren++;
      char *comma = strchr(paren, ',');
      char *end = comma ? comma : strchr(paren, ')');
      if (end) {
        snprintf(name, sizeof(name), "Apple %.*s", (int)(end - paren), paren);
      }
    }
  }

  // Get max frequency from cpufreq
  fp = popen(
      "cat /sys/devices/system/cpu/cpufreq/policy*/cpuinfo_max_freq "
      "2>/dev/null | sort -rn | head -1",
      "r");
  if (fp) {
    char buf[32];
    if (fgets(buf, sizeof(buf), fp)) {
      long khz = atol(buf);
      if (khz > 0)
        max_ghz = khz / 1000000.0f;
    }
    pclose(fp);
  }

  if (name[0]) {
    if (cores > 0 && max_ghz > 0)
      add_info("CPU", "%s (%d) @ %.2f GHz", name, cores, max_ghz);
    else if (cores > 0)
      add_info("CPU", "%s (%d)", name, cores);
    else
      add_info("CPU", "%s", name);
  }
}

// Extract a human GPU name from `lspci -d VVVV:DDDD` output.
// Typical line: "01:00.0 VGA compatible controller: NVIDIA Corporation ..."
static int gpu_lookup_lspci(const char *pci_id, char *out, int outlen) {
  if (!pci_id || !pci_id[0]) return 0;
  char cmd[128];
  snprintf(cmd, sizeof(cmd), "lspci -d %s 2>/dev/null", pci_id);
  FILE *fp = popen(cmd, "r");
  if (!fp) return 0;
  char line[256];
  int ok = 0;
  if (fgets(line, sizeof(line), fp)) {
    int l = strlen(line);
    while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r'))
      line[--l] = '\0';
    // Find the second ":" which separates class from name
    char *c1 = strchr(line, ':');
    if (c1) {
      char *c2 = strchr(c1 + 1, ':');
      if (c2) {
        char *nm = c2 + 1;
        while (*nm == ' ') nm++;
        // Strip trailing " (rev XX)"
        char *rev = strstr(nm, " (rev ");
        if (rev) *rev = '\0';
        strncpy(out, nm, outlen - 1);
        out[outlen - 1] = '\0';
        ok = 1;
      }
    }
  }
  pclose(fp);
  return ok;
}

static void gpu_pci_vendor(const char *pci_id, char *out, int outlen) {
  out[0] = '\0';
  if (!pci_id || strlen(pci_id) < 4) return;
  char v[5] = {0};
  memcpy(v, pci_id, 4);
  if (strcasecmp(v, "10de") == 0) strncpy(out, "NVIDIA", outlen - 1);
  else if (strcasecmp(v, "8086") == 0) strncpy(out, "Intel", outlen - 1);
  else if (strcasecmp(v, "1002") == 0) strncpy(out, "AMD", outlen - 1);
  else if (strcasecmp(v, "106b") == 0) strncpy(out, "Apple", outlen - 1);
  out[outlen - 1] = '\0';
}

static void gather_gpu(void) {
  DIR *d = opendir("/sys/class/drm");
  if (!d) return;
  struct dirent *ent;
  while ((ent = readdir(d))) {
    if (strncmp(ent->d_name, "card", 4) != 0) continue;
    int all_digits = 1;
    for (int i = 4; ent->d_name[i]; i++) {
      if (ent->d_name[i] < '0' || ent->d_name[i] > '9') { all_digits = 0; break; }
    }
    if (!all_digits) continue;

    char path[256];
    snprintf(path, sizeof(path), "/sys/class/drm/%s/device/uevent", ent->d_name);
    FILE *fp = fopen(path, "r");
    if (!fp) continue;
    char driver[32] = "", pci_id[16] = "", compat[64] = "";
    char buf[256];
    while (fgets(buf, sizeof(buf), fp)) {
      if (strncmp(buf, "DRIVER=", 7) == 0) {
        char *v = buf + 7;
        int l = strlen(v);
        while (l > 0 && (v[l - 1] == '\n' || v[l - 1] == '\r')) v[--l] = '\0';
        strncpy(driver, v, sizeof(driver) - 1);
      } else if (strncmp(buf, "PCI_ID=", 7) == 0) {
        char *v = buf + 7;
        int l = strlen(v);
        while (l > 0 && (v[l - 1] == '\n' || v[l - 1] == '\r')) v[--l] = '\0';
        strncpy(pci_id, v, sizeof(pci_id) - 1);
      } else if (strncmp(buf, "OF_COMPATIBLE_0=", 16) == 0) {
        char *v = buf + 16;
        int l = strlen(v);
        while (l > 0 && (v[l - 1] == '\n' || v[l - 1] == '\r')) v[--l] = '\0';
        strncpy(compat, v, sizeof(compat) - 1);
      }
    }
    fclose(fp);

    char vendor[32] = "";
    char name[160] = "";
    const char *type = "";

    if (strncmp(compat, "apple,agx", 9) == 0) {
      strcpy(vendor, "Apple");
      char cpu[64] = "";
      FILE *mfp = fopen("/proc/device-tree/model", "r");
      if (mfp) {
        char model[128];
        if (fgets(model, sizeof(model), mfp)) {
          char *paren = strchr(model, '(');
          if (paren) {
            paren++;
            char *comma = strchr(paren, ',');
            char *end = comma ? comma : strchr(paren, ')');
            if (end)
              snprintf(cpu, sizeof(cpu), "%.*s", (int)(end - paren), paren);
          }
        }
        fclose(mfp);
      }
      if (cpu[0]) strncpy(name, cpu, sizeof(name) - 1);
      else strcpy(name, "Apple GPU");
      type = "Integrated";
    } else {
      gpu_pci_vendor(pci_id, vendor, sizeof(vendor));
      if (!vendor[0]) {
        if (!strcmp(driver, "nvidia") || !strcmp(driver, "nouveau")) strcpy(vendor, "NVIDIA");
        else if (!strcmp(driver, "i915") || !strcmp(driver, "xe")) strcpy(vendor, "Intel");
        else if (!strcmp(driver, "amdgpu") || !strcmp(driver, "radeon")) strcpy(vendor, "AMD");
      }
      char lspci_line[160] = "";
      if (pci_id[0] && gpu_lookup_lspci(pci_id, lspci_line, sizeof(lspci_line))) {
        // Prefer bracket content (the human product name)
        char *lb = strrchr(lspci_line, '[');
        char *rb = strrchr(lspci_line, ']');
        if (lb && rb && rb > lb) {
          *rb = '\0';
          strncpy(name, lb + 1, sizeof(name) - 1);
        } else {
          // No bracket: use the part after " Corporation " / " Corp " if present
          char *corp = strstr(lspci_line, " Corporation ");
          int skip = corp ? 13 : 0;
          if (!corp) { corp = strstr(lspci_line, " Corp "); if (corp) skip = 6; }
          const char *src = corp ? (corp + skip) : lspci_line;
          strncpy(name, src, sizeof(name) - 1);
        }
        name[sizeof(name) - 1] = '\0';
      }
      if (!name[0]) {
        if (vendor[0]) snprintf(name, sizeof(name), "%s Graphics", vendor);
        else if (driver[0]) strncpy(name, driver, sizeof(name) - 1);
      }
      if (!type[0]) {
        if (!strcmp(driver, "i915") || !strcmp(driver, "xe")) type = "Integrated";
        else if (!strcmp(driver, "nvidia") || !strcmp(driver, "nouveau")) type = "Discrete";
      }
    }

    if (!name[0]) continue;

    const char *toks[7] = {
        vendor, name, driver, "", "", type, ""};
    const char *default_fmt = type[0] ? "{2} [{6}]" : "{2}";
    add_info_toks("GPU", toks, 7, default_fmt);
  }
  closedir(d);
}

static void gather_memory(void) {
  long total = 0, avail = 0;
  FILE *fp = fopen("/proc/meminfo", "r");
  if (!fp)
    return;
  char buf[128];
  while (fgets(buf, sizeof(buf), fp)) {
    if (strncmp(buf, "MemTotal:", 9) == 0)
      sscanf(buf + 9, " %ld", &total);
    else if (strncmp(buf, "MemAvailable:", 13) == 0)
      sscanf(buf + 13, " %ld", &avail);
  }
  fclose(fp);
  if (total <= 0)
    return;

  float used_gib = (total - avail) / 1048576.0f;
  float total_gib = total / 1048576.0f;
  int pct = (int)((total - avail) * 100 / total);

  // Color: green <50%, yellow 50-79%, red 80%+
  const char *color = pct >= 80 ? "31" : pct >= 50 ? "93" : "32";

  add_info("Memory", "%.2f GiB / %.2f GiB (\033[%sm%d%%\033[0m)", used_gib,
           total_gib, color, pct);
}

static void gather_swap(void) {
  long total = 0, free_s = 0;
  FILE *fp = fopen("/proc/meminfo", "r");
  if (!fp)
    return;
  char buf[128];
  while (fgets(buf, sizeof(buf), fp)) {
    if (strncmp(buf, "SwapTotal:", 10) == 0)
      sscanf(buf + 10, " %ld", &total);
    else if (strncmp(buf, "SwapFree:", 9) == 0)
      sscanf(buf + 9, " %ld", &free_s);
  }
  fclose(fp);
  if (total <= 0)
    return;

  long used = total - free_s;
  int pct = (int)(used * 100 / total);
  const char *color = pct >= 80 ? "31" : pct >= 50 ? "93" : "32";

  if (total >= 1048576)
    add_info("Swap", "%.2f GiB / %.2f GiB (\033[%sm%d%%\033[0m)",
             used / 1048576.0f, total / 1048576.0f, color, pct);
  else
    add_info("Swap", "%.2f MiB / %.2f MiB (\033[%sm%d%%\033[0m)",
             used / 1024.0f, total / 1024.0f, color, pct);
}

static void gather_disk(void) {
  struct statvfs st;
  if (statvfs("/", &st) != 0)
    return;

  float total_gib = (float)st.f_blocks * st.f_frsize / (1024 * 1024 * 1024);
  float free_gib = (float)st.f_bfree * st.f_frsize / (1024 * 1024 * 1024);
  float used_gib = total_gib - free_gib;
  int pct = (int)(used_gib * 100 / total_gib);
  const char *color = pct >= 80 ? "31" : pct >= 50 ? "93" : "32";

  // Get filesystem type from df
  char fstype[32] = "";
  FILE *fp = popen("df -T / 2>/dev/null | tail -1", "r");
  if (fp) {
    char buf[256];
    if (fgets(buf, sizeof(buf), fp)) {
      // Format: /dev/xxx ext4 ...
      char *p = buf;
      while (*p && *p != ' ')
        p++; // skip device
      while (*p == ' ')
        p++;
      char *end = p;
      while (*end && *end != ' ')
        end++;
      int len = end - p;
      if (len > 0 && len < (int)sizeof(fstype)) {
        memcpy(fstype, p, len);
        fstype[len] = '\0';
      }
    }
    pclose(fp);
  }

  if (fstype[0])
    add_info("Disk (/)", "%.2f GiB / %.2f GiB (\033[%sm%d%%\033[0m) - %s",
             used_gib, total_gib, color, pct, fstype);
  else
    add_info("Disk (/)", "%.2f GiB / %.2f GiB (\033[%sm%d%%\033[0m)",
             used_gib, total_gib, color, pct);
}

static void gather_battery(void) {
  // Find first battery in /sys/class/power_supply
  FILE *fp = popen("ls /sys/class/power_supply/ 2>/dev/null", "r");
  if (!fp)
    return;
  char bat_name[64] = "";
  char buf[64];
  while (fgets(buf, sizeof(buf), fp)) {
    int len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
      buf[--len] = '\0';
    // Check if it's a battery (has capacity file)
    char path[256];
    snprintf(path, sizeof(path), "/sys/class/power_supply/%s/capacity", buf);
    FILE *test = fopen(path, "r");
    if (test) {
      fclose(test);
      strncpy(bat_name, buf, sizeof(bat_name) - 1);
      break;
    }
  }
  pclose(fp);
  if (!bat_name[0])
    return;

  char path[256];
  int capacity = -1;
  char status[32] = "";

  // Prefer energy_now/energy_full for accurate percentage
  long energy_now = 0, energy_full = 0;
  snprintf(path, sizeof(path), "/sys/class/power_supply/%s/energy_now",
           bat_name);
  fp = fopen(path, "r");
  if (fp) {
    if (fscanf(fp, "%ld", &energy_now) != 1)
      energy_now = 0;
    fclose(fp);
  }
  snprintf(path, sizeof(path), "/sys/class/power_supply/%s/energy_full",
           bat_name);
  fp = fopen(path, "r");
  if (fp) {
    if (fscanf(fp, "%ld", &energy_full) != 1)
      energy_full = 0;
    fclose(fp);
  }
  if (energy_full > 0)
    capacity = (int)(energy_now * 100 / energy_full);

  // Fall back to charge_now/charge_full
  if (capacity < 0) {
    long charge_now = 0, charge_full = 0;
    snprintf(path, sizeof(path), "/sys/class/power_supply/%s/charge_now",
             bat_name);
    fp = fopen(path, "r");
    if (fp) {
      if (fscanf(fp, "%ld", &charge_now) != 1)
        charge_now = 0;
      fclose(fp);
    }
    snprintf(path, sizeof(path), "/sys/class/power_supply/%s/charge_full",
             bat_name);
    fp = fopen(path, "r");
    if (fp) {
      if (fscanf(fp, "%ld", &charge_full) != 1)
        charge_full = 0;
      fclose(fp);
    }
    if (charge_full > 0)
      capacity = (int)(charge_now * 100 / charge_full);
  }

  // Last resort: capacity file
  if (capacity < 0) {
    snprintf(path, sizeof(path), "/sys/class/power_supply/%s/capacity",
             bat_name);
    fp = fopen(path, "r");
    if (fp) {
      if (fscanf(fp, "%d", &capacity) != 1)
        capacity = -1;
      fclose(fp);
    }
  }

  snprintf(path, sizeof(path), "/sys/class/power_supply/%s/status", bat_name);
  fp = fopen(path, "r");
  if (fp) {
    if (fgets(status, sizeof(status), fp)) {
      int len = strlen(status);
      while (len > 0 && (status[len - 1] == '\n' || status[len - 1] == '\r'))
        status[--len] = '\0';
    }
    fclose(fp);
  }

  // Get time remaining estimate from power_now
  char time_str[64] = "";
  if (energy_now > 0) {
    long power_now = 0;
    snprintf(path, sizeof(path), "/sys/class/power_supply/%s/power_now",
             bat_name);
    fp = fopen(path, "r");
    if (fp) {
      if (fscanf(fp, "%ld", &power_now) != 1)
        power_now = 0;
      fclose(fp);
    }
    // power_now is negative when discharging on some systems
    if (power_now < 0)
      power_now = -power_now;
    if (power_now > 0) {
      int mins_left = (int)((float)energy_now / power_now * 60);
      int hours = mins_left / 60;
      int mins = mins_left % 60;
      if (hours > 0)
        snprintf(time_str, sizeof(time_str), "%d hour%s, %d min%s remaining",
                 hours, hours == 1 ? "" : "s", mins, mins == 1 ? "" : "s");
      else
        snprintf(time_str, sizeof(time_str), "%d min%s remaining", mins,
                 mins == 1 ? "" : "s");
    }
  }

  if (capacity >= 0) {
    const char *color = capacity >= 50 ? "32" : capacity >= 20 ? "93" : "31";
    if (time_str[0] && status[0])
      add_info("Battery", "\033[%sm%d%%\033[0m (%s) [%s]", color, capacity,
               time_str, status);
    else if (status[0])
      add_info("Battery", "\033[%sm%d%%\033[0m [%s]", color, capacity, status);
    else
      add_info("Battery", "\033[%sm%d%%\033[0m", color, capacity);
  }
}

static void gather_terminal(void) {
  char term[64] = "";
  // Try TERM_PROGRAM first, then walk up the process tree
  char *tp = getenv("TERM_PROGRAM");
  if (tp && tp[0]) {
    strncpy(term, tp, sizeof(term) - 1);
  } else if (getenv("KITTY_WINDOW_ID")) {
    strcpy(term, "kitty");
  } else {
    // Get parent process name (the terminal)
    FILE *fp = popen("ps -o comm= -p $(ps -o ppid= -p $$) 2>/dev/null", "r");
    if (fp) {
      if (fgets(term, sizeof(term), fp)) {
        int len = strlen(term);
        while (len > 0 && (term[len - 1] == '\n' || term[len - 1] == '\r'))
          term[--len] = '\0';
      }
      pclose(fp);
    }
  }
  if (term[0])
    add_info("Terminal", "%s", term);
}

static void gather_ip(void) {
  FILE *fp = popen("ip -4 -o addr show scope global 2>/dev/null | head -1",
                   "r");
  if (!fp)
    return;
  char buf[256];
  if (fgets(buf, sizeof(buf), fp)) {
    // Format: "2: wld0    inet 192.168.1.160/24 ..."
    char iface[32] = "", addr[64] = "";
    char *inet = strstr(buf, "inet ");
    if (inet) {
      inet += 5;
      char *space = strchr(inet, ' ');
      if (space) {
        int len = space - inet;
        if (len < (int)sizeof(addr)) {
          memcpy(addr, inet, len);
          addr[len] = '\0';
        }
      }
      // Get interface name (second field)
      char *p = buf;
      // Skip index
      while (*p && *p != ' ')
        p++;
      while (*p == ' ')
        p++;
      char *end = p;
      while (*end && *end != ' ')
        end++;
      int ilen = end - p;
      if (ilen < (int)sizeof(iface)) {
        memcpy(iface, p, ilen);
        iface[ilen] = '\0';
      }
    }
    if (addr[0]) {
      if (iface[0]) {
        char lbl[64];
        snprintf(lbl, sizeof(lbl), "Local IP (%s)", iface);
        add_info(lbl, "%s", addr);
      } else {
        add_info("Local IP", "%s", addr);
      }
    }
  }
  pclose(fp);
}

static void gather_locale(void) {
  char *lang = getenv("LANG");
  if (lang && lang[0])
    add_info("Locale", "%s", lang);
}

static void read_gtk_setting(const char *key, char *out, int maxlen) {
  char path[512];
  const char *home = getenv("HOME");
  if (!home)
    return;
  snprintf(path, sizeof(path), "%s/.config/gtk-3.0/settings.ini", home);
  FILE *fp = fopen(path, "r");
  if (!fp)
    return;
  char buf[256];
  int keylen = strlen(key);
  while (fgets(buf, sizeof(buf), fp)) {
    if (strncmp(buf, key, keylen) == 0 && buf[keylen] == '=') {
      char *val = buf + keylen + 1;
      int len = strlen(val);
      while (len > 0 && (val[len - 1] == '\n' || val[len - 1] == '\r'))
        val[--len] = '\0';
      if (len > 0 && len < maxlen) {
        memcpy(out, val, len + 1);
      }
      break;
    }
  }
  fclose(fp);
}

static void gather_theme(void) {
  char theme[64] = "";
  read_gtk_setting("gtk-theme-name", theme, sizeof(theme));
  if (!theme[0]) return;
  const char *toks[2] = { theme, "GTK3" };
  add_info_toks("Theme", toks, 2, "{1} [{2}]");
}

static void gather_icons(void) {
  char icons[64] = "";
  read_gtk_setting("gtk-icon-theme-name", icons, sizeof(icons));
  if (!icons[0]) return;
  const char *toks[2] = { icons, "GTK3" };
  add_info_toks("Icons", toks, 2, "{1} [{2}]");
}

static void gather_font(void) {
  char font[128] = "";
  read_gtk_setting("gtk-font-name", font, sizeof(font));
  if (!font[0]) return;
  const char *toks[2] = { font, "GTK3" };
  add_info_toks("Font", toks, 2, "{1} [{2}]");
}

// Screen buffer: each cell holds one UTF-8 codepoint (up to 4 bytes + NUL)
static char screen[MAX_HEIGHT][ANIM_WIDTH][5];
static float zbuf[MAX_HEIGHT][ANIM_WIDTH];
static int colorbuf[MAX_HEIGHT][ANIM_WIDTH];

static void clear_buf(void) {
  for (int i = 0; i < render_height; i++)
    for (int j = 0; j < ANIM_WIDTH; j++) {
      screen[i][j][0] = ' ';
      screen[i][j][1] = '\0';
      zbuf[i][j] = -1e9f;
      colorbuf[i][j] = 0;
    }
}

static void build_points(void) {
  const float sx = 0.07f;
  const float sy = 0.14f;
  const float cx = (logo_cols - 1) * 0.5f;
  const float cy = (logo_rows - 1) * 0.5f;
  const float zmax = 0.18f;
  int Z_LAYERS = (int)(6 * size_scale);
  if (Z_LAYERS < 6)
    Z_LAYERS = 6;

  float hmap[MAX_LOGO_ROWS][MAX_LOGO_COLS];
  for (int r = 0; r < logo_rows; r++) {
    for (int c = 0; c < logo_cols; c++) {
      if (c < logo_cell_counts[r])
        hmap[r][c] = char_weight_utf8(logo_cells[r][c]);
      else
        hmap[r][c] = 0.0f;
    }
  }

  float gnx[MAX_LOGO_ROWS][MAX_LOGO_COLS];
  float gny[MAX_LOGO_ROWS][MAX_LOGO_COLS];
  float gnz[MAX_LOGO_ROWS][MAX_LOGO_COLS];
  for (int r = 0; r < logo_rows; r++) {
    for (int c = 0; c < logo_cols; c++) {
      if (hmap[r][c] <= 0.0f) {
        gnx[r][c] = gny[r][c] = 0;
        gnz[r][c] = 1;
        continue;
      }
      float dhdx = 0, dhdy = 0;
      if (c > 0 && c < logo_cols - 1)
        dhdx = (hmap[r][c + 1] - hmap[r][c - 1]) * 0.5f;
      else if (c == 0)
        dhdx = hmap[r][c + 1] - hmap[r][c];
      else
        dhdx = hmap[r][c] - hmap[r][c - 1];

      if (r > 0 && r < logo_rows - 1)
        dhdy = (hmap[r + 1][c] - hmap[r - 1][c]) * 0.5f;
      else if (r == 0)
        dhdy = hmap[r + 1][c] - hmap[r][c];
      else
        dhdy = hmap[r][c] - hmap[r - 1][c];

      dhdx /= sx;
      dhdy /= sy;

      float nnx = -dhdx;
      float nny = dhdy;
      float nnz = 1.0f;
      float l = sqrtf(nnx * nnx + nny * nny + nnz * nnz);
      gnx[r][c] = nnx / l;
      gny[r][c] = nny / l;
      gnz[r][c] = nnz / l;
    }
  }

  // Subdivide grid for larger sizes to avoid gaps
  int subdiv = (int)size_scale;
  if (subdiv < 1)
    subdiv = 1;

  int idx = 0;
  for (int row = 0; row < logo_rows; row++) {
    for (int col = 0; col < logo_cols; col++) {
      float h = hmap[row][col];
      if (h <= 0.0f)
        continue;

      for (int sr = 0; sr < subdiv; sr++) {
        for (int sc = 0; sc < subdiv; sc++) {
          float frow = row + (float)sr / subdiv;
          float fcol = col + (float)sc / subdiv;

          // Interpolate height from neighbors
          float ih = h;
          if (sr > 0 || sc > 0) {
            float fr = (float)sr / subdiv;
            float fc = (float)sc / subdiv;
            int nr = row + (sr > 0 ? 1 : 0);
            int nc = col + (sc > 0 ? 1 : 0);
            if (nr >= logo_rows) nr = logo_rows - 1;
            if (nc >= logo_cols) nc = logo_cols - 1;
            float h00 = hmap[row][col];
            float h10 = hmap[nr][col];
            float h01 = hmap[row][nc];
            float h11 = hmap[nr][nc];
            ih = h00 * (1 - fr) * (1 - fc) + h10 * fr * (1 - fc) +
                 h01 * (1 - fr) * fc + h11 * fr * fc;
            if (ih <= 0.0f)
              continue;
          }

      float ox = (fcol - cx) * sx;
      float oy = (cy - frow) * sy;
      float zr = ih * zmax;

      for (int k = 0; k < Z_LAYERS; k++) {
        if (idx >= MAX_POINTS)
          break;
        float t = ((float)k / (Z_LAYERS - 1)) - 0.5f;
        PX[idx] = ox;
        PY[idx] = oy;
        PZ[idx] = t * 2.0f * zr;
        PWEIGHT[idx] = ih;
        PCOLOR[idx] = logo_cell_color[row][col];

        if (k == 0) {
          NX[idx] = gnx[row][col];
          NY[idx] = gny[row][col];
          NZ[idx] = -gnz[row][col];
        } else if (k == Z_LAYERS - 1) {
          NX[idx] = gnx[row][col];
          NY[idx] = gny[row][col];
          NZ[idx] = gnz[row][col];
        } else {
          float ex = 0, ey = 0;
          for (int dr = -1; dr <= 1; dr++) {
            for (int dc = -1; dc <= 1; dc++) {
              if (dr == 0 && dc == 0)
                continue;
              int nr = row + dr, nc = col + dc;
              float nh = 0;
              if (nr >= 0 && nr < logo_rows && nc >= 0 && nc < logo_cols)
                nh = hmap[nr][nc];
              if (nh < h) {
                ex += (float)dc;
                ey += (float)(-dr);
              }
            }
          }
          float el = sqrtf(ex * ex + ey * ey);
          if (el > 1e-6f) {
            ex /= el;
            ey /= el;
          }
          float tn = ((float)k / (Z_LAYERS - 1)) * 2.0f - 1.0f;
          float side = sqrtf(1.0f - tn * tn);
          NX[idx] = ex * side;
          NY[idx] = ey * side;
          NZ[idx] = tn;
        }
        idx++;
      }
        }
      }
    }
  }
  POINT_COUNT = idx;
}

static float color_threshold = 0.5f;

static int float_cmp(const void *a, const void *b) {
  float fa = *(const float *)a, fb = *(const float *)b;
  return (fa > fb) - (fa < fb);
}

static void compute_threshold(void) {
  if (POINT_COUNT == 0)
    return;
  float *sorted = malloc(POINT_COUNT * sizeof(float));
  if (!sorted)
    return;
  memcpy(sorted, PWEIGHT, POINT_COUNT * sizeof(float));
  qsort(sorted, POINT_COUNT, sizeof(float), float_cmp);
  color_threshold = sorted[POINT_COUNT / 2];
  free(sorted);
}

// Default colors: bold magenta (outer) + bold white (inner)
static const char *color_inner = "\033[1;37m";
static const char *color_outer = "\033[1;35m";

static void set_distro_colors(const char *distro) {
  if (strcasecmp(distro, "gentoo") == 0) {
    color_outer = "\033[1;35m";
    color_inner = "\033[1;37m";
  } else if (strcasecmp(distro, "arch") == 0) {
    color_outer = "\033[1;36m";
    color_inner = "\033[1;36m";
  } else if (strcasecmp(distro, "ubuntu") == 0) {
    color_outer = "\033[1;31m";
    color_inner = "\033[1;37m";
  } else if (strcasecmp(distro, "debian") == 0) {
    color_outer = "\033[1;31m";
    color_inner = "\033[1;37m";
  } else if (strcasecmp(distro, "asahi") == 0 ||
             strcasecmp(distro, "asahi2") == 0 ||
             strcasecmp(distro, "fedora-asahi-remix") == 0) {
    color_outer = "\033[1;31m"; // bold red
    color_inner = "\033[1;37m"; // bold white
  } else if (strcasecmp(distro, "fedora") == 0 ||
             strncasecmp(distro, "fedora-", 7) == 0) {
    color_outer = "\033[1;34m";
    color_inner = "\033[1;37m";
  } else if (strcasecmp(distro, "nixos") == 0) {
    color_outer = "\033[1;34m";
    color_inner = "\033[1;36m";
  } else if (strcasecmp(distro, "void") == 0) {
    color_outer = "\033[1;32m";
    color_inner = "\033[1;32m";
  } else if (strcasecmp(distro, "alpine") == 0) {
    color_outer = "\033[1;34m";
    color_inner = "\033[1;37m";
  } else if (strcasecmp(distro, "opensuse-tumbleweed") == 0 ||
             strcasecmp(distro, "opensuse-leap") == 0 ||
             strcasecmp(distro, "opensuse") == 0) {
    color_outer = "\033[1;32m";
    color_inner = "\033[1;37m";
  }
}

// --- fastfetch config support ---
// Parse fastfetch's JSONC config and use only its `modules` array to drive
// fetch's field order and per-field label/color overrides. Data is still
// gathered natively by fetch; only presentation is borrowed.

enum { F_CUSTOM_MARKER = -2 }; // special value returned by module mapper

static int ff_module_to_field(const char *type) {
  struct { const char *t; int id; } map[] = {
      {"os", F_OS},             {"host", F_HOST},
      {"kernel", F_KERNEL},     {"uptime", F_UPTIME},
      {"packages", F_PACKAGES}, {"shell", F_SHELL},
      {"display", F_DISPLAY},   {"wm", F_WM},
      {"wmtheme", F_THEME},     {"theme", F_THEME},
      {"icons", F_ICONS},       {"font", F_FONT},
      {"terminalfont", F_FONT}, {"terminal", F_TERMINAL},
      {"cpu", F_CPU},           {"gpu", F_GPU},
      {"memory", F_MEMORY},     {"swap", F_SWAP},
      {"disk", F_DISK},         {"localip", F_IP},
      {"ip", F_IP},             {"battery", F_BATTERY},
      {"locale", F_LOCALE},     {"colors", F_COLORS},
      {"title", F_TITLE},       {"separator", F_SEPARATOR},
      {"break", F_BREAK},       {"de", F_DE},
      {"lm", F_LM},             {"brightness", F_BRIGHTNESS},
      {"poweradapter", F_POWERADAPTER}, {"sound", F_SOUND},
      {"bluetooth", F_BLUETOOTH}, {"wifi", F_WIFI},
      {"player", F_PLAYER}, {"media", F_MEDIA},
      {NULL, -1}};
  if (strcasecmp(type, "custom") == 0)
    return F_CUSTOM_MARKER;
  for (int i = 0; map[i].t; i++)
    if (strcasecmp(type, map[i].t) == 0)
      return map[i].id;
  return -1;
}

// Render a fastfetch `custom` module format string as a single line.
// Handles {$N} (display.constants[N-1]) and {#...} (ANSI SGR escapes).
// Wraps the whole line in outputColor if present.
static void ff_render_custom(const char *fmt, const char *outputColor) {
  char out[MAX_LINE_LEN];
  int oi = 0;

  if (outputColor && outputColor[0]) {
    int n = snprintf(out + oi, sizeof(out) - oi, "\033[%sm", outputColor);
    if (n > 0) oi += n;
  }

  for (int i = 0; fmt[i] && oi + 8 < (int)sizeof(out);) {
    if (fmt[i] == '{' && fmt[i + 1] == '$') {
      int j = i + 2;
      int num = 0;
      while (fmt[j] >= '0' && fmt[j] <= '9') { num = num * 10 + (fmt[j] - '0'); j++; }
      if (fmt[j] == '}' && num >= 1 && num <= ff_constants_count) {
        int len = (int)strlen(ff_constants[num - 1]);
        if (oi + len < (int)sizeof(out)) {
          memcpy(out + oi, ff_constants[num - 1], len);
          oi += len;
        }
        i = j + 1;
        continue;
      }
    }
    if (fmt[i] == '{' && fmt[i + 1] == '#') {
      int j = i + 2;
      while ((fmt[j] >= '0' && fmt[j] <= '9') || fmt[j] == ';') j++;
      if (fmt[j] == '}') {
        int plen = j - (i + 2);
        char esc[32];
        if (plen == 0) strcpy(esc, "\033[0m");
        else if (plen < 29) snprintf(esc, sizeof(esc), "\033[%.*sm", plen, fmt + i + 2);
        else esc[0] = '\0';
        int elen = (int)strlen(esc);
        if (oi + elen < (int)sizeof(out)) {
          memcpy(out + oi, esc, elen);
          oi += elen;
        }
        i = j + 1;
        continue;
      }
    }
    out[oi++] = fmt[i++];
  }

  if (outputColor && outputColor[0] && oi + 5 < (int)sizeof(out)) {
    memcpy(out + oi, "\033[0m", 4);
    oi += 4;
  }

  out[oi] = '\0';
  add_line(out);
}

static void ff_color_to_ansi(const char *in, char *out, int outlen) {
  struct { const char *name; const char *code; } nm[] = {
      {"black", "30"},        {"red", "31"},         {"green", "32"},
      {"yellow", "33"},       {"blue", "34"},        {"magenta", "35"},
      {"cyan", "36"},         {"white", "37"},       {"default", "39"},
      {"light_black", "90"},  {"light_red", "91"},   {"light_green", "92"},
      {"light_yellow", "93"}, {"light_blue", "94"},  {"light_magenta", "95"},
      {"light_cyan", "96"},   {"light_white", "97"}, {NULL, NULL}};
  for (int i = 0; nm[i].name; i++) {
    if (strcasecmp(in, nm[i].name) == 0) {
      strncpy(out, nm[i].code, outlen - 1);
      out[outlen - 1] = '\0';
      return;
    }
  }
  strncpy(out, in, outlen - 1);
  out[outlen - 1] = '\0';
}

static int strip_jsonc_comments(char *s, int len) {
  char *r = s, *w = s, *end = s + len;
  int in_str = 0, esc = 0;
  while (r < end) {
    char c = *r;
    if (in_str) {
      *w++ = c;
      if (esc) esc = 0;
      else if (c == '\\') esc = 1;
      else if (c == '"') in_str = 0;
      r++;
      continue;
    }
    if (c == '"') { in_str = 1; *w++ = c; r++; continue; }
    if (c == '/' && r + 1 < end) {
      if (r[1] == '/') {
        r += 2;
        while (r < end && *r != '\n') r++;
        continue;
      }
      if (r[1] == '*') {
        r += 2;
        while (r + 1 < end && !(r[0] == '*' && r[1] == '/')) r++;
        if (r + 1 < end) r += 2;
        continue;
      }
    }
    *w++ = c;
    r++;
  }
  return (int)(w - s);
}

static const char *ff_skip_ws(const char *p) {
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
    p++;
  return p;
}

static const char *ff_parse_string(const char *p, char *out, int outlen) {
  if (*p != '"') return NULL;
  p++;
  int n = 0;
  while (*p && *p != '"') {
    if (*p == '\\' && p[1]) {
      char c = p[1];
      if (c == 'n') { if (n + 1 < outlen) out[n++] = '\n'; }
      else if (c == 't') { if (n + 1 < outlen) out[n++] = '\t'; }
      else if (c == 'r') { if (n + 1 < outlen) out[n++] = '\r'; }
      else if (c == '\\' || c == '"' || c == '/') {
        if (n + 1 < outlen) out[n++] = c;
      } else if (c == 'u' && p[2] && p[3] && p[4] && p[5]) {
        char hex[5];
        memcpy(hex, p + 2, 4);
        hex[4] = '\0';
        unsigned int cp = (unsigned int)strtoul(hex, NULL, 16);
        if (cp < 0x80) {
          if (n + 1 < outlen) out[n++] = (char)cp;
        } else if (cp < 0x800) {
          if (n + 2 < outlen) {
            out[n++] = (char)(0xC0 | (cp >> 6));
            out[n++] = (char)(0x80 | (cp & 0x3F));
          }
        } else {
          if (n + 3 < outlen) {
            out[n++] = (char)(0xE0 | (cp >> 12));
            out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[n++] = (char)(0x80 | (cp & 0x3F));
          }
        }
        p += 6;
        continue;
      } else {
        if (n + 1 < outlen) out[n++] = c;
      }
      p += 2;
    } else {
      if (n + 1 < outlen) out[n++] = *p;
      p++;
    }
  }
  if (*p != '"') return NULL;
  if (n < outlen) out[n] = '\0';
  else out[outlen - 1] = '\0';
  return p + 1;
}

static const char *ff_skip_value(const char *p) {
  p = ff_skip_ws(p);
  if (*p == '"') {
    char tmp[4];
    return ff_parse_string(p, tmp, sizeof(tmp));
  }
  if (*p == '{' || *p == '[') {
    char open = *p, close = (open == '{') ? '}' : ']';
    int depth = 0;
    while (*p) {
      if (*p == '"') {
        char tmp[4];
        const char *n = ff_parse_string(p, tmp, sizeof(tmp));
        if (!n) return NULL;
        p = n;
        continue;
      }
      if (*p == open) depth++;
      else if (*p == close) {
        depth--;
        if (depth == 0) return p + 1;
      }
      p++;
    }
    return NULL;
  }
  while (*p && *p != ',' && *p != '}' && *p != ']' && *p != ' ' &&
         *p != '\t' && *p != '\n' && *p != '\r')
    p++;
  return p;
}

static const char *ff_parse_display(const char *p) {
  if (*p != '{') return NULL;
  p++;
  p = ff_skip_ws(p);
  while (*p && *p != '}') {
    if (*p != '"') return NULL;
    char k[32];
    const char *n = ff_parse_string(p, k, sizeof(k));
    if (!n) return NULL;
    p = ff_skip_ws(n);
    if (*p != ':') return NULL;
    p = ff_skip_ws(p + 1);
    if (strcmp(k, "separator") == 0 && *p == '"') {
      p = ff_parse_string(p, ff_separator_str, sizeof(ff_separator_str));
      if (!p) return NULL;
    } else if (strcmp(k, "constants") == 0 && *p == '[') {
      p++;
      p = ff_skip_ws(p);
      ff_constants_count = 0;
      while (*p && *p != ']') {
        if (*p == '"') {
          char slot[MAX_FF_CONSTANT_LEN];
          const char *after = ff_parse_string(p, slot, sizeof(slot));
          if (!after) return NULL;
          if (ff_constants_count < MAX_FF_CONSTANTS) {
            strncpy(ff_constants[ff_constants_count], slot,
                    MAX_FF_CONSTANT_LEN - 1);
            ff_constants[ff_constants_count][MAX_FF_CONSTANT_LEN - 1] = '\0';
            ff_constants_count++;
          }
          p = after;
        } else {
          p = ff_skip_value(p);
          if (!p) return NULL;
        }
        p = ff_skip_ws(p);
        if (*p == ',') { p++; p = ff_skip_ws(p); }
      }
      if (*p == ']') p++;
    } else {
      p = ff_skip_value(p);
      if (!p) return NULL;
    }
    p = ff_skip_ws(p);
    if (*p == ',') { p++; p = ff_skip_ws(p); }
  }
  if (*p == '}') return p + 1;
  return NULL;
}

// Walk top-level object, extract display block, return pointer to the opening
// '[' of the modules array (or NULL if not found).
static const char *ff_walk_top(const char *p) {
  p = ff_skip_ws(p);
  if (*p != '{') return NULL;
  p++;
  p = ff_skip_ws(p);
  const char *modules_p = NULL;
  while (*p && *p != '}') {
    if (*p != '"') return NULL;
    char key[64];
    const char *next = ff_parse_string(p, key, sizeof(key));
    if (!next) return NULL;
    p = ff_skip_ws(next);
    if (*p != ':') return NULL;
    p = ff_skip_ws(p + 1);
    if (strcmp(key, "modules") == 0 && *p == '[') {
      modules_p = p;
      p = ff_skip_value(p);
      if (!p) return NULL;
    } else if (strcmp(key, "display") == 0 && *p == '{') {
      p = ff_parse_display(p);
      if (!p) return NULL;
    } else {
      p = ff_skip_value(p);
      if (!p) return NULL;
    }
    p = ff_skip_ws(p);
    if (*p == ',') { p++; p = ff_skip_ws(p); }
  }
  return modules_p;
}

typedef struct {
  char type[32];
  char key[64];
  char keyColor[32];
  char format[512];
  char outputColor[32];
} ff_mod_t;

static const char *ff_parse_module_obj(const char *p, ff_mod_t *m) {
  if (*p != '{') return NULL;
  p++;
  p = ff_skip_ws(p);
  m->type[0] = m->key[0] = m->keyColor[0] = m->format[0] = m->outputColor[0] = '\0';
  while (*p && *p != '}') {
    if (*p != '"') return NULL;
    char kname[32];
    const char *n = ff_parse_string(p, kname, sizeof(kname));
    if (!n) return NULL;
    p = ff_skip_ws(n);
    if (*p != ':') return NULL;
    p = ff_skip_ws(p + 1);
    if (strcmp(kname, "type") == 0 && *p == '"') {
      p = ff_parse_string(p, m->type, sizeof(m->type));
    } else if (strcmp(kname, "key") == 0 && *p == '"') {
      p = ff_parse_string(p, m->key, sizeof(m->key));
    } else if (strcmp(kname, "keyColor") == 0 && *p == '"') {
      p = ff_parse_string(p, m->keyColor, sizeof(m->keyColor));
    } else if (strcmp(kname, "format") == 0 && *p == '"') {
      p = ff_parse_string(p, m->format, sizeof(m->format));
    } else if (strcmp(kname, "outputColor") == 0 && *p == '"') {
      p = ff_parse_string(p, m->outputColor, sizeof(m->outputColor));
    } else {
      p = ff_skip_value(p);
    }
    if (!p) return NULL;
    p = ff_skip_ws(p);
    if (*p == ',') { p++; p = ff_skip_ws(p); }
  }
  if (*p == '}') return p + 1;
  return NULL;
}

static int load_fastfetch_config(const char *path) {
  FILE *fp = fopen(path, "r");
  if (!fp) {
    fprintf(stderr, "fetch: cannot open fastfetch config: %s\n", path);
    return 0;
  }
  if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return 0; }
  long sz = ftell(fp);
  if (sz <= 0 || sz > (1L << 20)) { fclose(fp); return 0; }
  if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return 0; }
  char *buf = (char *)malloc((size_t)sz + 1);
  if (!buf) { fclose(fp); return 0; }
  size_t got = fread(buf, 1, (size_t)sz, fp);
  fclose(fp);
  if (got != (size_t)sz) { free(buf); return 0; }
  buf[sz] = '\0';

  int len = strip_jsonc_comments(buf, (int)sz);
  buf[len] = '\0';

  const char *p = ff_walk_top(buf);
  if (!p) {
    fprintf(stderr, "fetch: no \"modules\" array in %s\n", path);
    free(buf);
    return 0;
  }
  p++; // past '['
  p = ff_skip_ws(p);

  for (int i = 0; i < F_COUNT; i++) field_enabled[i] = 0;
  field_count = 0;

  while (*p && *p != ']' && field_count < MAX_FIELD_SEQ) {
    ff_mod_t m;
    m.type[0] = m.key[0] = m.keyColor[0] = m.format[0] = m.outputColor[0] = '\0';
    if (*p == '"') {
      p = ff_parse_string(p, m.type, sizeof(m.type));
      if (!p) break;
    } else if (*p == '{') {
      p = ff_parse_module_obj(p, &m);
      if (!p) break;
    } else {
      break;
    }
    int id = ff_module_to_field(m.type);
    int si = field_count;
    if (id == F_CUSTOM_MARKER) {
      strncpy(seq_custom_format[si], m.format, sizeof(seq_custom_format[si]) - 1);
      seq_custom_format[si][sizeof(seq_custom_format[si]) - 1] = '\0';
      if (m.outputColor[0])
        ff_color_to_ansi(m.outputColor, seq_custom_color[si],
                         (int)sizeof(seq_custom_color[si]));
      else
        seq_custom_color[si][0] = '\0';
      field_order[field_count++] = F_CUSTOM_MARKER;
    } else if (id >= 0) {
      field_order[field_count++] = id;
      if (id < F_COUNT) field_enabled[id] = 1;
      if (m.key[0]) {
        strncpy(seq_label_override[si], m.key,
                sizeof(seq_label_override[si]) - 1);
        seq_label_override[si][sizeof(seq_label_override[si]) - 1] = '\0';
      }
      if (m.keyColor[0]) {
        ff_color_to_ansi(m.keyColor, seq_color_override[si],
                         (int)sizeof(seq_color_override[si]));
      }
      if (m.format[0]) {
        strncpy(seq_format[si], m.format, sizeof(seq_format[si]) - 1);
        seq_format[si][sizeof(seq_format[si]) - 1] = '\0';
      }
    }
    p = ff_skip_ws(p);
    if (*p == ',') { p++; p = ff_skip_ws(p); }
  }

  free(buf);
  ff_mode_active = 1;
  return 1;
}

static int resolve_and_load_fastfetch(const char *explicit_path) {
  char tmp[512];
  if (explicit_path && explicit_path[0]) {
    return load_fastfetch_config(explicit_path);
  }
  const char *xdg = getenv("XDG_CONFIG_HOME");
  if (xdg && *xdg) {
    snprintf(tmp, sizeof(tmp), "%s/fastfetch/config.jsonc", xdg);
    if (access(tmp, R_OK) == 0) return load_fastfetch_config(tmp);
  }
  const char *home = getenv("HOME");
  if (home && *home) {
    snprintf(tmp, sizeof(tmp), "%s/.config/fastfetch/config.jsonc", home);
    if (access(tmp, R_OK) == 0) return load_fastfetch_config(tmp);
    snprintf(tmp, sizeof(tmp), "%s/.config/fastfetch/config.conf", home);
    if (access(tmp, R_OK) == 0) return load_fastfetch_config(tmp);
  }
  fprintf(stderr, "fetch: no fastfetch config found in XDG/HOME\n");
  return 0;
}

int main(int argc, char **argv) {
  char distro[64] = "";
  const char *logo_name = NULL;
  int rotate_x = 1, rotate_y = 1;
  float speed = 1.0f;
  int show_info = 1;
  int use_color = 1;
  int max_frames = 2000;
  const char *shading = ".,-~:;=!*#$@";
  int ff_requested = 0; // 1 = enable, -1 = force off, 0 = use config
  char ff_cli_path[512] = "";

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      printf("Usage: fetch [options]\n\n"
             "Options:\n"
             "  -l, --logo <name>         Use a logo from fastfetch by name\n"
             "                            Any logo fastfetch supports works, e.g.:\n"
             "                              gentoo, arch, nixos, debian, ubuntu,\n"
             "                              fedora, void, alpine, opensuse, manjaro,\n"
             "                              proxmox, pop, linuxmint, endeavouros...\n"
             "                            Run 'fastfetch --list-logos' to see all\n"
             "  --rotate-x                Lock rotation to X axis only\n"
             "  --rotate-y                Lock rotation to Y axis only\n"
             "  -s, --speed <float>       Speed multiplier (default 1.0)\n"
             "  --no-info                 Just the logo, no system info\n"
             "  --no-color                Disable logo coloring\n"
             "  --frames <n>              Stop after n frames (default 2000)\n"
             "  --infinite                Run forever (keypress or Ctrl-C to exit)\n"
             "  --shading-chars <str>     Custom shading ramp, supports UTF-8\n"
             "                            Default: .,-~:;=!*#$@\n"
             "                            Example: ' ░▒▓█'\n"
             "  --ff-config [path]        Use fastfetch's config for the info\n"
             "                            layout (module order, key labels,\n"
             "                            keyColor). If no path given, looks up\n"
             "                            $XDG_CONFIG_HOME/fastfetch/config.jsonc\n"
             "                            and ~/.config/fastfetch/config.jsonc.\n"
             "                            Also accepts --ff-config=<path>.\n"
             "                            Note: fastfetch's logo/display blocks\n"
             "                            are IGNORED; fetch's 3D spinning logo\n"
             "                            always wins. Only the `modules` array\n"
             "                            is consulted, and only type/key/keyColor\n"
             "                            per module.\n"
             "  --no-ff-config            Force-disable fastfetch config (even if\n"
             "                            enabled via fetch's own config file)\n"
             "  -h, --help                Show this help\n\n"
             "Config: ~/.config/fetch/logo.txt\n"
             "  Add '# distro: <name>' as the first line to set colors.\n"
             "  Supported color schemes:\n"
             "    gentoo, arch, ubuntu, debian, fedora, fedora-asahi-remix,\n"
             "    nixos, void, alpine, opensuse\n\n"
             "Config: ~/.config/fetch/config\n"
             "  fastfetch_config=auto     Use auto-discovered fastfetch config\n"
             "  fastfetch_config=<path>   Use explicit path\n"
             "  fastfetch_config=off      Disable (default)\n");
      return 0;
    } else if ((strcmp(argv[i], "--logo") == 0 || strcmp(argv[i], "-l") == 0) &&
        i + 1 < argc) {
      logo_name = argv[++i];
    } else if (strcmp(argv[i], "--rotate-x") == 0) {
      rotate_x = 1;
      rotate_y = 0;
    } else if (strcmp(argv[i], "--rotate-y") == 0) {
      rotate_x = 0;
      rotate_y = 1;
    } else if ((strcmp(argv[i], "--speed") == 0 || strcmp(argv[i], "-s") == 0) &&
               i + 1 < argc) {
      speed = atof(argv[++i]);
    } else if (strcmp(argv[i], "--no-info") == 0) {
      show_info = 0;
    } else if (strcmp(argv[i], "--no-color") == 0) {
      use_color = 0;
    } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
      max_frames = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--infinite") == 0) {
      max_frames = 0;
    } else if (strcmp(argv[i], "--shading-chars") == 0 && i + 1 < argc) {
      shading = argv[++i];
    } else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
      config_height = atoi(argv[++i]);
      if (config_height > MAX_HEIGHT)
        config_height = MAX_HEIGHT;
    } else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
      size_scale = atof(argv[++i]);
      if (size_scale < 0.5f)
        size_scale = 0.5f;
      if (size_scale > 5.0f)
        size_scale = 5.0f;
    } else if (strcmp(argv[i], "--ff-config") == 0) {
      ff_requested = 1;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        strncpy(ff_cli_path, argv[++i], sizeof(ff_cli_path) - 1);
        ff_cli_path[sizeof(ff_cli_path) - 1] = '\0';
      }
    } else if (strncmp(argv[i], "--ff-config=", 12) == 0) {
      ff_requested = 1;
      strncpy(ff_cli_path, argv[i] + 12, sizeof(ff_cli_path) - 1);
      ff_cli_path[sizeof(ff_cli_path) - 1] = '\0';
    } else if (strcmp(argv[i], "--no-ff-config") == 0) {
      ff_requested = -1;
    }
  }

  // Parse shading ramp into codepoints
  parse_shading(shading);
  config_defaults();
  load_config();

  // Decide whether to activate fastfetch config mode.
  // CLI flags (--ff-config / --no-ff-config) take priority over fetch's
  // own config directive (fastfetch_config=...). Default is off.
  {
    int ff_on = 0;
    const char *ff_path = NULL;
    if (ff_requested == -1) {
      ff_on = 0;
    } else if (ff_requested == 1) {
      ff_on = 1;
      if (ff_cli_path[0]) ff_path = ff_cli_path;
    } else if (config_ff_setting[0] && strcasecmp(config_ff_setting, "off") != 0) {
      ff_on = 1;
      if (strcasecmp(config_ff_setting, "auto") != 0)
        ff_path = config_ff_setting;
    }
    if (ff_on)
      resolve_and_load_fastfetch(ff_path);
  }

  // Config overrides for shading, speed, spin (CLI flags take priority)
  if (config_shading[0])
    parse_shading(config_shading);
  if (config_speed > 0 && speed == 1.0f)
    speed = config_speed;
  if (config_spin_x >= 0 && rotate_x == 1 && rotate_y == 1) {
    rotate_x = config_spin_x;
    rotate_y = config_spin_y;
  }

  if (logo_name) {
    if (!load_logo_fastfetch(logo_name))
      load_default_logo();
    strncpy(distro, logo_name, sizeof(distro) - 1);
  } else {
    // Try logo.txt first for distro hint, then detect
    load_logo_file();
    if (file_distro[0])
      strncpy(distro, file_distro, sizeof(distro) - 1);
    else
      detect_distro(distro, sizeof(distro));

    // Try fastfetch for colored logo (prefer over plain logo.txt)
    int got_logo = 0;
    if (distro[0]) {
      // Reset logo state to try fastfetch
      int saved_rows = logo_rows;
      char saved_data[MAX_LOGO_ROWS][512];
      for (int i = 0; i < saved_rows; i++)
        memcpy(saved_data[i], logo_data[i], 512);
      logo_rows = 0;
      logo_cols = 0;

      got_logo = load_logo_fastfetch(distro);
      if (!got_logo && distro_id_like[0]) {
        char like_copy[64];
        strncpy(like_copy, distro_id_like, sizeof(like_copy) - 1);
        like_copy[sizeof(like_copy) - 1] = '\0';
        char *tok = strtok(like_copy, " ");
        while (tok && !got_logo) {
          got_logo = load_logo_fastfetch(tok);
          if (got_logo)
            strncpy(distro, tok, sizeof(distro) - 1);
          tok = strtok(NULL, " ");
        }
      }
      if (!got_logo) {
        // Restore logo.txt data
        logo_rows = saved_rows;
        for (int i = 0; i < saved_rows; i++)
          memcpy(logo_data[i], saved_data[i], 512);
      }
    }
    if (!got_logo && logo_rows == 0)
      load_default_logo();
  }

  // Process logo into codepoint cells
  process_logo();

  if (distro[0]) {
    set_distro_colors(distro);
    strncpy(detected_distro, distro, sizeof(detected_distro) - 1);
    detected_distro[sizeof(detected_distro) - 1] = '\0';
  }

  typedef void (*gather_fn)(void);
  gather_fn fns[F_COUNT] = {
      [F_OS] = gather_os,
      [F_HOST] = gather_host,
      [F_KERNEL] = gather_kernel,
      [F_UPTIME] = gather_uptime,
      [F_PACKAGES] = gather_packages,
      [F_SHELL] = gather_shell,
      [F_DISPLAY] = gather_display,
      [F_WM] = gather_wm,
      [F_THEME] = gather_theme,
      [F_ICONS] = gather_icons,
      [F_FONT] = gather_font,
      [F_TERMINAL] = gather_terminal,
      [F_CPU] = gather_cpu,
      [F_GPU] = gather_gpu,
      [F_MEMORY] = gather_memory,
      [F_SWAP] = gather_swap,
      [F_DISK] = gather_disk,
      [F_IP] = gather_ip,
      [F_BATTERY] = gather_battery,
      [F_LOCALE] = gather_locale,
      [F_COLORS] = NULL,
      [F_DE] = gather_de,
      [F_LM] = gather_lm,
      [F_BRIGHTNESS] = gather_brightness,
      [F_POWERADAPTER] = gather_poweradapter,
      [F_SOUND] = gather_sound,
      [F_BLUETOOTH] = gather_bluetooth,
      [F_WIFI] = gather_wifi,
      [F_PLAYER] = gather_player,
      [F_MEDIA] = gather_media,
  };

  for (int i = 0; i < F_COUNT; i++)
    field_line[i] = -1;

  for (int i = 0; i < F_COUNT; i++) field_seq_idx[i] = -1;

  if (show_info) {
    if (!ff_mode_active) {
      current_field = F_TITLE;
      current_seq_idx = -1;
      gather_title_line();
      current_field = F_SEPARATOR;
      gather_separator_line();
      current_field = -1;
    }
    for (int i = 0; i < field_count; i++) {
      int id = field_order[i];
      current_seq_idx = i;
      if (id == F_CUSTOM_MARKER) {
        ff_render_custom(seq_custom_format[i], seq_custom_color[i]);
      } else if (id == F_TITLE) {
        current_field = F_TITLE;
        gather_title_line();
      } else if (id == F_SEPARATOR) {
        current_field = F_SEPARATOR;
        gather_separator_line();
      } else if (id == F_BREAK) {
        add_line("");
      } else if (id == F_COLORS) {
        add_line("");
        add_line("\033[40m   \033[41m   \033[42m   \033[43m   "
                 "\033[44m   \033[45m   \033[46m   \033[47m   \033[0m");
        add_line("\033[100m   \033[101m   \033[102m   \033[103m   "
                 "\033[104m   \033[105m   \033[106m   \033[107m   \033[0m");
      } else if (id >= 0 && id < F_COUNT && fns[id]) {
        current_field = id;
        if (field_seq_idx[id] < 0) field_seq_idx[id] = i;
        fns[id]();
      }
    }
    current_field = -1;
    current_seq_idx = -1;
  }
  // Set render height: config/flag override > auto-fit to info lines > default
  if (config_height > 0) {
    render_height = config_height;
  } else if (show_info && fetch_line_count > 0) {
    // Use whichever is taller: info lines or default logo size
    int info_height = fetch_line_count + 2;
    render_height = info_height > 36 ? info_height : 36;
  }
  // Apply size scale
  render_height = (int)(render_height * size_scale);
  if (render_height < 20)
    render_height = 20;
  if (render_height > MAX_HEIGHT)
    render_height = MAX_HEIGHT;

  build_points();
  compute_threshold();

  float A = 0.0f;
  float B = 0.0f;
  const float K1 = 37.0f * render_height / 36.0f;
  const float K2 = 5.5f;

  signal(SIGINT, handle_signal);
  signal(SIGTERM, handle_signal);
  atexit(cleanup);

  int fetch_start = 1;

  if (tcgetattr(STDIN_FILENO, &orig_termios) == 0) {
    termios_saved = 1;
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
  }

  printf("\033[?25l\033[2J");
  fflush(stdout);

  for (int frame = 0; max_frames == 0 || frame < max_frames; frame++) {
    struct pollfd pfd = {.fd = STDIN_FILENO, .events = POLLIN};
    if (poll(&pfd, 1, 0) > 0)
      break;
    // Refresh fast dynamic fields every ~1 second (20 frames).
    // Only uptime/memory/swap — they're pure /proc reads, no popen,
    // so they don't hitch the animation. Battery/disk/ip use popen and
    // stay static (user can restart to refresh those).
    if (show_info && frame > 0 && frame % 20 == 0) {
      is_refresh_pass = 1;
      if (field_line[F_UPTIME] >= 0) {
        current_field = F_UPTIME;
        current_seq_idx = field_seq_idx[F_UPTIME];
        gather_uptime();
      }
      if (field_line[F_MEMORY] >= 0) {
        current_field = F_MEMORY;
        current_seq_idx = field_seq_idx[F_MEMORY];
        gather_memory();
      }
      if (field_line[F_SWAP] >= 0) {
        current_field = F_SWAP;
        current_seq_idx = field_seq_idx[F_SWAP];
        gather_swap();
      }
      current_field = -1;
      current_seq_idx = -1;
      is_refresh_pass = 0;
    }

    clear_buf();
    A += rotate_x ? 0.04f * speed : 0.0f;
    B += rotate_y ? 0.06f * speed : 0.0f;
    float cA = cosf(A), sA = sinf(A);
    float cB = cosf(B), sB = sinf(B);

    const float lx = light_x, ly = light_y, lz = light_z;
    const float vx = 0.0f, vy = 0.0f, vz = -1.0f;
    float hx = lx + vx, hy = ly + vy, hz = lz + vz;
    float hl = sqrtf(hx * hx + hy * hy + hz * hz);
    hx /= hl;
    hy /= hl;
    hz /= hl;

    for (int i = 0; i < POINT_COUNT; i++) {
      float px = PX[i], py = PY[i], pz = PZ[i];
      float nx = NX[i], ny = NY[i], nz = NZ[i];

      float y1 = py * cA - pz * sA;
      float z1 = py * sA + pz * cA;
      float x2 = px * cB + z1 * sB;
      float z2 = -px * sB + z1 * cB;
      float y2 = y1;

      float ny1 = ny * cA - nz * sA;
      float nz1 = ny * sA + nz * cA;
      float nx2 = nx * cB + nz1 * sB;
      float nz2 = -nx * sB + nz1 * cB;
      float ny2 = ny1;

      float zc = z2 + K2;
      if (zc < 0.1f)
        continue;
      float ooz = 1.0f / zc;
      int xs = (int)((float)ANIM_WIDTH * 0.5f + K1 * 2.0f * x2 * ooz);
      float y_center = fetch_line_count > 0
                           ? fetch_start + fetch_line_count / 2.0f
                           : render_height * 0.5f;
      int ys = (int)(y_center - K1 * y2 * ooz);
      if (xs < 0 || xs >= ANIM_WIDTH || ys < 0 || ys >= render_height)
        continue;

      if (ooz > zbuf[ys][xs]) {
        float diff = nx2 * lx + ny2 * ly + nz2 * lz;
        if (diff < 0)
          diff = 0;

        float spec_dot = nx2 * hx + ny2 * hy + nz2 * hz;
        if (spec_dot < 0)
          spec_dot = 0;
        float spec = spec_dot * spec_dot;
        spec = spec * spec;
        spec = spec * spec;

        float L = 0.08f + 0.62f * diff + 0.30f * spec;
        if (L > 1.0f)
          L = 1.0f;

        zbuf[ys][xs] = ooz;
        int ci = (int)(L * (shading_count - 1));
        if (ci < 0)
          ci = 0;
        if (ci >= shading_count)
          ci = shading_count - 1;
        memcpy(screen[ys][xs], shading_chars[ci], 5);
        colorbuf[ys][xs] = logo_has_ansi ? PCOLOR[i]
                            : ((PWEIGHT[i] >= color_threshold) ? 1 : 0);
      }
    }

    printf("\033[H");
    for (int i = 0; i < render_height; i++) {
      if (!use_color) {
        for (int j = 0; j < ANIM_WIDTH; j++)
          fputs(screen[i][j], stdout);
      } else {
        int prev_color = -1;
        for (int j = 0; j < ANIM_WIDTH; j++) {
          if (screen[i][j][0] == ' ' && screen[i][j][1] == '\0') {
            if (prev_color != -1) {
              printf("\033[0m");
              prev_color = -1;
            }
            fputc(' ', stdout);
          } else {
            int c = colorbuf[i][j];
            if (c != prev_color) {
              if (logo_has_ansi && c > 0)
                printf("\033[1;%dm", c);
              else
                printf("%s", c == 1 ? color_inner : color_outer);
              prev_color = c;
            }
            fputs(screen[i][j], stdout);
          }
        }
        if (prev_color != -1)
          printf("\033[0m");
      }

      int fi = i - fetch_start;
      if (fi >= 0 && fi < fetch_line_count) {
        printf("%*s%s", GAP, "", fetch_lines[fi]);
      }

      printf("\033[K\n");
    }
    fflush(stdout);
    usleep(50000);
  }

  printf("\033[?25h");
  fflush(stdout);
  return 0;
}
