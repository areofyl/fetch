#ifndef FETCH_WINDOWS_INFO_H
#define FETCH_WINDOWS_INFO_H

#define WIN_FIELD_VALUES 16
static char win_values[F_COUNT][WIN_FIELD_VALUES][MAX_LINE_LEN];
static char win_labels[F_COUNT][WIN_FIELD_VALUES][128];
static int win_value_count[F_COUNT];

static const char *win_modules[F_COUNT] = {
    [F_OS] = "os", [F_HOST] = "host", [F_KERNEL] = "kernel",
    [F_PACKAGES] = "packages", [F_SHELL] = "shell", [F_DISPLAY] = "display",
    [F_WM] = "wm", [F_THEME] = "theme", [F_ICONS] = "icons",
    [F_FONT] = "font", [F_CURSOR] = "cursor", [F_TERMINAL] = "terminal",
    [F_CPU] = "cpu", [F_GPU] = "gpu", [F_DISK] = "disk", [F_IP] = "localip",
    [F_BATTERY] = "battery", [F_LOCALE] = "locale"};

static void win_parse_info(FILE *fp) {
  char line[2048];
  while (fgets(line, sizeof(line), fp)) {
    if (strncmp(line, "__fetch_", 8) != 0)
      continue;
    char *end;
    long id = strtol(line + 8, &end, 10);
    if (end == line + 8 || id < 0 || id >= F_COUNT ||
        strncmp(end, "__", 2) != 0 || win_value_count[id] >= WIN_FIELD_VALUES)
      continue;
    char *marker = strstr(end + 2, "__end__: ");
    char *value;
    if (marker) {
      size_t length = (size_t)(marker - (end + 2));
      if (length >= sizeof(win_labels[0][0]))
        length = sizeof(win_labels[0][0]) - 1;
      memcpy(win_labels[id][win_value_count[id]], end + 2, length);
      win_labels[id][win_value_count[id]][length] = '\0';
      value = marker + 9;
    } else if (strncmp(end, "__: ", 4) == 0) {
      value = end + 4;
    } else {
      continue;
    }
    value[strcspn(value, "\r\n")] = '\0';
    if (!value[0])
      continue;
    // Formatted pipe output has no ANSI; discard other control characters.
    char *dest = win_values[id][win_value_count[id]++];
    int n = 0;
    for (const unsigned char *p = (unsigned char *)value;
         *p && n < MAX_LINE_LEN - 1; p++)
      if (*p >= 32 && *p != 127)
        dest[n++] = (char)*p;
    dest[n] = '\0';
  }
}

static void win_gather_static(void) {
  if (!win_find_fastfetch())
    return;
  wchar_t directory[MAX_PATH], path[MAX_PATH];
  DWORD n = GetTempPathW(MAX_PATH, directory);
  if (!n || n >= MAX_PATH || !GetTempFileNameW(directory, L"ftc", 0, path)) {
    fprintf(stderr, "fetch: could not create a temporary Fastfetch config.\n");
    return;
  }
  FILE *config = _wfopen(path, L"wb");
  if (!config) {
    DeleteFileW(path);
    return;
  }
  fputs("{\"logo\":{\"type\":\"none\"},\"display\":{\"pipe\":true,"
        "\"showErrors\":false,\"separator\":\": \"},\"modules\":[", config);
  int first = 1;
  for (int i = 0; i < F_COUNT; i++) {
    if (!win_modules[i] || !field_enabled[i])
      continue;
    const char *detail = i == F_DISK ? " ({mountpoint})" :
                         i == F_DISPLAY ? " ({name})" :
                         i == F_IP ? " ({ifname})" : "";
    fprintf(config, "%s{\"type\":\"%s\",\"key\":\"__fetch_%d__%s__end__\"}",
            first ? "" : ",", win_modules[i], i, detail);
    first = 0;
  }
  fputs("]}", config);
  int ok = !ferror(config);
  if (fclose(config) != 0)
    ok = 0;
  // Fastfetch uses the suffix to recognize a config file; .tmp is rejected.
  wchar_t json_path[MAX_PATH + 6];
  wcscpy(json_path, path);
  wcscat(json_path, L".json");
  if (!MoveFileW(path, json_path)) {
    DeleteFileW(path);
    return;
  }
  const wchar_t *args[] = {L"--config", json_path, L"--format", L"default"};
  FILE *fp = ok ? win_fastfetch(args, 4) : NULL;
  DeleteFileW(json_path);
  if (!fp) {
    fprintf(stderr, "fetch: Fastfetch system information failed; showing native Windows fields.\n");
    return;
  }
  win_parse_info(fp);
  fclose(fp);
}

static void win_cached(int field, const char *label) {
  for (int i = 0; i < win_value_count[field]; i++) {
    char full_label[160];
    snprintf(full_label, sizeof(full_label), "%s%s", label, win_labels[field][i]);
    add_info(full_label, "%s", win_values[field][i]);
  }
}

static void gather_title(void) {
  wchar_t wide_user[256] = L"", wide_host[256] = L"";
  DWORD n = 256;
  GetUserNameW(wide_user, &n);
  n = 256;
  GetComputerNameW(wide_host, &n);
  char user[768], host[768], line[MAX_LINE_LEN];
  win_utf8(wide_user, user, sizeof(user));
  win_utf8(wide_host, host, sizeof(host));
  snprintf(line, sizeof(line), "\033[1;%sm%.200s\033[0m@\033[1;%sm%.200s\033[0m",
           label_color, user, label_color, host);
  add_line(line);
  int width = visible_width(user) + 1 + visible_width(host);
  char separator[MAX_LINE_LEN];
  int length = (int)strlen(config_separator), pos = 0;
  for (int i = 0; i < width && length && pos + length < MAX_LINE_LEN; i++) {
    memcpy(separator + pos, config_separator, length);
    pos += length;
  }
  separator[pos] = '\0';
  add_line(separator);
}

static void gather_os(void) {
  if (win_value_count[F_OS])
    win_cached(F_OS, "OS");
  else
    add_info("OS", "Windows");
}

static void gather_host(void) {
  if (win_value_count[F_HOST]) {
    win_cached(F_HOST, "Host");
    return;
  }
  wchar_t wide[256] = L"";
  char host[768];
  DWORD n = 256;
  if (GetComputerNameW(wide, &n)) {
    win_utf8(wide, host, sizeof(host));
    add_info("Host", "%s", host);
  }
}

static void gather_uptime(void) {
  ULONGLONG minutes = GetTickCount64() / 60000;
  unsigned long long days = minutes / 1440;
  unsigned hours = (unsigned)((minutes % 1440) / 60);
  unsigned mins = (unsigned)(minutes % 60);
  if (days)
    add_info("Uptime", "%llu days, %u hours, %u mins", days, hours, mins);
  else if (hours)
    add_info("Uptime", "%u hours, %u mins", hours, mins);
  else
    add_info("Uptime", "%u mins", mins);
}

static void gather_memory(void) {
  MEMORYSTATUSEX memory = {0};
  memory.dwLength = sizeof(memory);
  if (GlobalMemoryStatusEx(&memory))
    add_info("Memory", "%.2f GiB / %.2f GiB (%lu%%)",
             (memory.ullTotalPhys - memory.ullAvailPhys) / 1073741824.0,
             memory.ullTotalPhys / 1073741824.0, (unsigned long)memory.dwMemoryLoad);
}

static void gather_swap(void) {
  MEMORYSTATUSEX memory = {0};
  memory.dwLength = sizeof(memory);
  if (GlobalMemoryStatusEx(&memory))
    add_info("Commit", "%.2f GiB / %.2f GiB",
             (memory.ullTotalPageFile - memory.ullAvailPageFile) / 1073741824.0,
             memory.ullTotalPageFile / 1073741824.0);
}

static void gather_disk(void) {
  win_cached(F_DISK, "Disk");
  for (int i = 0; i < extra_disk_count; i++) {
    wchar_t *path = win_wide(extra_disks[i]);
    ULARGE_INTEGER free_bytes, total_bytes;
    if (path && GetDiskFreeSpaceExW(path, NULL, &total_bytes, &free_bytes))
      add_info("Disk", "%s: %.2f GiB / %.2f GiB", extra_disks[i],
               (total_bytes.QuadPart - free_bytes.QuadPart) / 1073741824.0,
               total_bytes.QuadPart / 1073741824.0);
    free(path);
  }
}

#define WIN_CACHED(name, field, label) \
  static void gather_##name(void) { win_cached(field, label); }
WIN_CACHED(kernel, F_KERNEL, "Kernel")
WIN_CACHED(packages, F_PACKAGES, "Packages")
WIN_CACHED(shell, F_SHELL, "Shell")
WIN_CACHED(display, F_DISPLAY, "Display")
WIN_CACHED(wm, F_WM, "WM")
WIN_CACHED(theme, F_THEME, "Theme")
WIN_CACHED(icons, F_ICONS, "Icons")
WIN_CACHED(font, F_FONT, "Font")
WIN_CACHED(cursor, F_CURSOR, "Cursor")
WIN_CACHED(terminal, F_TERMINAL, "Terminal")
WIN_CACHED(cpu, F_CPU, "CPU")
WIN_CACHED(gpu, F_GPU, "GPU")
WIN_CACHED(ip, F_IP, "Local IP")
WIN_CACHED(battery, F_BATTERY, "Battery")
WIN_CACHED(locale, F_LOCALE, "Locale")
#undef WIN_CACHED
static void gather_displaymanager(void) {}
static void gather_powerprofile(void) {}

#endif
