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

// Dynamic logo storage
#define MAX_LOGO_ROWS 64
#define MAX_LOGO_COLS 128
static char logo_data[MAX_LOGO_ROWS][MAX_LOGO_COLS];
static int logo_rows = 0;
static int logo_cols = 0;

// Try loading logo from ~/.config/fetch/logo.txt
// First line can be "# distro: <name>" to set color scheme
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

  char buf[MAX_LOGO_COLS];
  while (logo_rows < MAX_LOGO_ROWS && fgets(buf, sizeof(buf), fp)) {
    int len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
      buf[--len] = '\0';
    // Parse "# distro: <name>" header
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
    if (len > logo_cols)
      logo_cols = len;
    logo_rows++;
  }
  fclose(fp);
  while (logo_rows > 0 && logo_data[logo_rows - 1][0] == '\0')
    logo_rows--;
  return logo_rows > 0;
}

// Extract logo from fastfetch for a given distro name
static int load_logo_fastfetch(const char *name) {
  char cmd[256];
  snprintf(cmd, sizeof(cmd),
           "fastfetch -l %s --logo-type builtin --pipe false 2>/dev/null", name);
  FILE *fp = popen(cmd, "r");
  if (!fp)
    return 0;

  // fastfetch prints the logo on the left side of the output
  // We need to extract just the logo portion (before the info text)
  // Simpler: use --print-logos to get the raw logo
  pclose(fp);

  snprintf(cmd, sizeof(cmd),
           "fastfetch --print-logos 2>/dev/null");
  fp = popen(cmd, "r");
  if (!fp)
    return 0;

  char buf[MAX_LOGO_COLS];
  int found = 0;
  int name_len = strlen(name);

  while (fgets(buf, sizeof(buf), fp)) {
    int len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
      buf[--len] = '\0';

    if (!found) {
      // Look for "Name:" line
      if (len > 0 && len <= name_len + 1 && buf[len - 1] == ':') {
        buf[len - 1] = '\0';
        if (strcasecmp(buf, name) == 0)
          found = 1;
      }
      continue;
    }

    // We're inside the logo — stop at next "Name:" or empty after content
    if (len > 0 && buf[len - 1] == ':' && logo_rows > 0) {
      // Check if this looks like a new logo header (no spaces before colon)
      int is_header = 1;
      for (int i = 0; i < len - 1; i++) {
        if (buf[i] == ' ') {
          is_header = 0;
          break;
        }
      }
      if (is_header)
        break;
    }

    if (logo_rows >= MAX_LOGO_ROWS)
      break;

    memcpy(logo_data[logo_rows], buf, len + 1);
    if (len > logo_cols)
      logo_cols = len;
    logo_rows++;
  }
  pclose(fp);

  // Trim trailing blank rows
  while (logo_rows > 0 && logo_data[logo_rows - 1][0] == '\0')
    logo_rows--;
  return logo_rows > 0;
}

// Detect distro name from /etc/os-release
static int detect_distro(char *out, int maxlen) {
  FILE *fp = fopen("/etc/os-release", "r");
  if (!fp)
    return 0;
  char buf[256];
  while (fgets(buf, sizeof(buf), fp)) {
    if (strncmp(buf, "ID=", 3) == 0) {
      int len = strlen(buf);
      while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
        buf[--len] = '\0';
      char *val = buf + 3;
      // Strip quotes
      if (*val == '"')
        val++;
      len = strlen(val);
      if (len > 0 && val[len - 1] == '"')
        val[--len] = '\0';
      if (len > 0 && len < maxlen) {
        memcpy(out, val, len + 1);
        fclose(fp);
        return 1;
      }
    }
  }
  fclose(fp);
  return 0;
}

// Default Gentoo logo fallback
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
  logo_cols = 34;
  for (int i = 0; i < logo_rows; i++) {
    int len = strlen(gentoo[i]);
    memcpy(logo_data[i], gentoo[i], len + 1);
    if (len > logo_cols)
      logo_cols = len;
  }
}

#define MAX_POINTS 16000
static float PX[MAX_POINTS], PY[MAX_POINTS], PZ[MAX_POINTS];
static float NX[MAX_POINTS], NY[MAX_POINTS], NZ[MAX_POINTS];
static float PWEIGHT[MAX_POINTS];
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

static float char_weight(char ch) {
  switch (ch) {
  case 'M':
    return 1.00f;
  case 'N':
    return 0.88f;
  case 'm':
    return 0.76f;
  case 'd':
    return 0.66f;
  case 'h':
    return 0.56f;
  case 'b':
    return 0.56f;
  case 'y':
    return 0.46f;
  case 'o':
    return 0.38f;
  case 'n':
    return 0.38f;
  case 's':
    return 0.30f;
  case '+':
    return 0.22f;
  case ':':
    return 0.18f;
  case '=':
    return 0.22f;
  case '-':
    return 0.14f;
  case '`':
    return 0.08f;
  case '.':
    return 0.10f;
  case '/':
    return 0.12f;
  case '\'':
    return 0.06f;
  default:
    return 0.0f;
  }
}

static char screen[HEIGHT][ANIM_WIDTH];
static float zbuf[HEIGHT][ANIM_WIDTH];
static int colorbuf[HEIGHT][ANIM_WIDTH];

static void clear_buf(void) {
  for (int i = 0; i < HEIGHT; i++)
    for (int j = 0; j < ANIM_WIDTH; j++) {
      screen[i][j] = ' ';
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
    int len = strlen(logo_data[r]);
    for (int c = 0; c < logo_cols; c++) {
      char ch = (c < len) ? logo_data[r][c] : ' ';
      hmap[r][c] = char_weight(ch);
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
  // Sort a copy of weights to find the median
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

// Distro color schemes: [outer, inner]
static void set_distro_colors(const char *distro) {
  if (strcasecmp(distro, "gentoo") == 0) {
    color_outer = "\033[1;35m"; // bold magenta
    color_inner = "\033[1;37m"; // bold white
  } else if (strcasecmp(distro, "arch") == 0) {
    color_outer = "\033[1;36m"; // bold cyan
    color_inner = "\033[1;36m"; // bold cyan
  } else if (strcasecmp(distro, "ubuntu") == 0) {
    color_outer = "\033[1;31m"; // bold red
    color_inner = "\033[1;37m"; // bold white
  } else if (strcasecmp(distro, "debian") == 0) {
    color_outer = "\033[1;31m"; // bold red
    color_inner = "\033[1;37m"; // bold white
  } else if (strcasecmp(distro, "fedora-asahi-remix") == 0) {
    color_outer = "\033[1;33m"; // bold yellow
    color_inner = "\033[1;32m"; // bold green
  } else if (strcasecmp(distro, "fedora") == 0 ||
             strncasecmp(distro, "fedora-", 7) == 0) {
    color_outer = "\033[1;34m"; // bold blue
    color_inner = "\033[1;37m"; // bold white
  } else if (strcasecmp(distro, "nixos") == 0) {
    color_outer = "\033[1;34m"; // bold blue
    color_inner = "\033[1;36m"; // bold cyan
  } else if (strcasecmp(distro, "void") == 0) {
    color_outer = "\033[1;32m"; // bold green
    color_inner = "\033[1;32m"; // bold green
  } else if (strcasecmp(distro, "alpine") == 0) {
    color_outer = "\033[1;34m"; // bold blue
    color_inner = "\033[1;37m"; // bold white
  } else if (strcasecmp(distro, "opensuse-tumbleweed") == 0 ||
             strcasecmp(distro, "opensuse-leap") == 0 ||
             strcasecmp(distro, "opensuse") == 0) {
    color_outer = "\033[1;32m"; // bold green
    color_inner = "\033[1;37m"; // bold white
  }
}

int main(int argc, char **argv) {
  // Load logo: --logo <name> flag, or ~/.config/fetch/logo.txt, or auto-detect
  char distro[64] = "";
  const char *logo_name = NULL;

  for (int i = 1; i < argc; i++) {
    if ((strcmp(argv[i], "--logo") == 0 || strcmp(argv[i], "-l") == 0) &&
        i + 1 < argc) {
      logo_name = argv[++i];
    }
  }

  if (logo_name) {
    // Try loading from fastfetch by name
    if (!load_logo_fastfetch(logo_name))
      load_default_logo();
    strncpy(distro, logo_name, sizeof(distro) - 1);
  } else if (!load_logo_file()) {
    // Auto-detect distro
    if (detect_distro(distro, sizeof(distro)))
      if (!load_logo_fastfetch(distro))
        load_default_logo();
    if (logo_rows == 0)
      load_default_logo();
  } else {
    // Logo loaded from file — use file_distro header if present
    if (file_distro[0])
      strncpy(distro, file_distro, sizeof(distro) - 1);
    else
      detect_distro(distro, sizeof(distro));
  }

  if (distro[0])
    set_distro_colors(distro);

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

  for (int frame = 0; frame < 2000; frame++) {
    struct pollfd pfd = {.fd = STDIN_FILENO, .events = POLLIN};
    if (poll(&pfd, 1, 0) > 0)
      break;
    clear_buf();
    A += 0.04f;
    B += 0.06f;
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
        const char *chars = ".,-~:;=!*#$@";
        int ci = (int)(L * 11.0f);
        if (ci < 0)
          ci = 0;
        if (ci > 11)
          ci = 11;
        screen[ys][xs] = chars[ci];
        colorbuf[ys][xs] = (PWEIGHT[i] >= 0.5f) ? 1 : 0;
      }
    }

    printf("\033[H");
    for (int i = 0; i < HEIGHT; i++) {
      int prev_color = -1;
      for (int j = 0; j < ANIM_WIDTH; j++) {
        if (screen[i][j] == ' ') {
          if (prev_color != -1) {
            printf("\033[0m");
            prev_color = -1;
          }
          fputc(' ', stdout);
        } else {
          int c = colorbuf[i][j];
          if (c != prev_color) {
            printf("%s", c == 1 ? color_inner : color_outer);
            prev_color = c;
          }
          fputc(screen[i][j], stdout);
        }
      }
      if (prev_color != -1)
        printf("\033[0m");

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
