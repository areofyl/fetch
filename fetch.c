#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
#define HEIGHT 36
#define GAP 2
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
          if (has_num && ((num >= 30 && num <= 37) || (num >= 90 && num <= 97)))
            cur_color = num;
          if (has_num && num == 0)
            cur_color = 0;
          num = 0;
          has_num = 0;
        }
        i++;
      }
      if (has_num && ((num >= 30 && num <= 37) || (num >= 90 && num <= 97)))
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
        break;
      }
    }

    if (len == 0 && logo_rows == 0)
      continue;
    if (len == 0)
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

#define MAX_POINTS 16000
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

static void capture_fastfetch(void) {
  FILE *fp = popen("fastfetch --logo none --pipe false 2>/dev/null", "r");
  if (!fp)
    return;
  char buf[MAX_LINE_LEN];
  while (fetch_line_count < MAX_FETCH_LINES &&
         fgets(buf, sizeof(buf), fp) != NULL) {
    int len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
      buf[--len] = '\0';
    memcpy(fetch_lines[fetch_line_count], buf, len + 1);
    fetch_line_count++;
  }
  pclose(fp);
}

// Screen buffer: each cell holds one UTF-8 codepoint (up to 4 bytes + NUL)
static char screen[HEIGHT][ANIM_WIDTH][5];
static float zbuf[HEIGHT][ANIM_WIDTH];
static int colorbuf[HEIGHT][ANIM_WIDTH];

static void clear_buf(void) {
  for (int i = 0; i < HEIGHT; i++)
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
  const int Z_LAYERS = 6;

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

  int idx = 0;
  for (int row = 0; row < logo_rows; row++) {
    for (int col = 0; col < logo_cols; col++) {
      float h = hmap[row][col];
      if (h <= 0.0f)
        continue;

      float ox = (col - cx) * sx;
      float oy = (cy - row) * sy;
      float zr = h * zmax;

      for (int k = 0; k < Z_LAYERS; k++) {
        if (idx >= MAX_POINTS)
          break;
        float t = ((float)k / (Z_LAYERS - 1)) - 0.5f;
        PX[idx] = ox;
        PY[idx] = oy;
        PZ[idx] = t * 2.0f * zr;
        PWEIGHT[idx] = h;
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

int main(int argc, char **argv) {
  char distro[64] = "";
  const char *logo_name = NULL;
  int rotate_x = 1, rotate_y = 1;
  float speed = 1.0f;
  int show_info = 1;
  int use_color = 1;
  int max_frames = 2000;
  const char *shading = ".,-~:;=!*#$@";

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
             "  -h, --help                Show this help\n\n"
             "Config: ~/.config/fetch/logo.txt\n"
             "  Add '# distro: <name>' as the first line to set colors.\n"
             "  Supported color schemes:\n"
             "    gentoo, arch, ubuntu, debian, fedora, fedora-asahi-remix,\n"
             "    nixos, void, alpine, opensuse\n");
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
    }
  }

  // Parse shading ramp into codepoints
  parse_shading(shading);

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

  if (distro[0])
    set_distro_colors(distro);

  if (show_info)
    capture_fastfetch();
  build_points();
  compute_threshold();

  float A = 0.0f;
  float B = 0.0f;
  const float K1 = 37.0f;
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
    clear_buf();
    A += rotate_x ? 0.04f * speed : 0.0f;
    B += rotate_y ? 0.06f * speed : 0.0f;
    float cA = cosf(A), sA = sinf(A);
    float cB = cosf(B), sB = sinf(B);

    const float lx = 0.4082f, ly = 0.8165f, lz = -0.4082f;
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
      int ys = (int)((float)HEIGHT * 0.35f - K1 * y2 * ooz);
      if (xs < 0 || xs >= ANIM_WIDTH || ys < 0 || ys >= HEIGHT)
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
    for (int i = 0; i < HEIGHT; i++) {
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
