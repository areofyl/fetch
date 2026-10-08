#ifndef FETCH_WINDOWS_H
#define FETCH_WINDOWS_H

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <wchar.h>

#define strcasecmp _stricmp

static HANDLE win_input, win_output;
static DWORD win_input_mode, win_output_mode;
static UINT win_input_cp, win_output_cp;
static CONSOLE_CURSOR_INFO win_cursor;
static int win_input_saved, win_output_saved, win_cursor_saved;
static int win_redirected, win_screen_active;
static volatile LONG win_stop;

static wchar_t *win_wide(const char *text) {
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
  if (!n)
    return NULL;
  wchar_t *out = malloc((size_t)n * sizeof(*out));
  if (out && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, out, n)) {
    free(out);
    return NULL;
  }
  return out;
}

static void win_utf8(const wchar_t *text, char *out, int capacity) {
  if (!WideCharToMultiByte(CP_UTF8, 0, text, -1, out, capacity, NULL, NULL))
    out[0] = '\0';
}

static FILE *win_config_file(const wchar_t *name) {
  DWORD n = GetEnvironmentVariableW(L"APPDATA", NULL, 0);
  if (!n)
    return NULL;
  size_t capacity = n + wcslen(name) + 8;
  wchar_t *path = malloc(capacity * sizeof(*path));
  if (!path)
    return NULL;
  if (!GetEnvironmentVariableW(L"APPDATA", path, n)) {
    free(path);
    return NULL;
  }
  wcscat(path, L"\\fetch\\");
  wcscat(path, name);
  FILE *fp = _wfopen(path, L"rb");
  free(path);
  return fp;
}

static int win_write_raw(const char *data, size_t length) {
  while (length) {
    DWORD written;
    DWORD chunk = length > MAXDWORD ? MAXDWORD : (DWORD)length;
    if (!WriteFile(win_output, data, chunk, &written, NULL) || !written)
      return 0;
    data += written;
    length -= written;
  }
  return 1;
}

static int win_write_frame(char *data, size_t length) {
  if (win_redirected) {
    size_t out = 0;
    for (size_t i = 0; i < length;) {
      if (data[i] == '\033' && i + 1 < length && data[i + 1] == '[') {
        i += 2;
        while (i < length && !(data[i] >= '@' && data[i] <= '~')) i++;
        if (i < length) i++;
      } else {
        if (data[i]) data[out++] = data[i];
        i++;
      }
    }
    length = out;
  }
  return win_write_raw(data, length);
}

static void win_console_cleanup(void) {
  if (win_screen_active) {
    const char restore[] = "\033[0m\033[?1049l";
    win_write_raw(restore, sizeof(restore) - 1);
    win_screen_active = 0;
  }
  if (win_cursor_saved) {
    SetConsoleCursorInfo(win_output, &win_cursor);
    win_cursor_saved = 0;
  }
  if (win_input_saved) {
    SetConsoleMode(win_input, win_input_mode);
    SetConsoleCP(win_input_cp);
    win_input_saved = 0;
  }
  if (win_output_saved) {
    SetConsoleMode(win_output, win_output_mode);
    SetConsoleOutputCP(win_output_cp);
    win_output_saved = 0;
  }
}

static BOOL WINAPI win_control(DWORD event) {
  if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT) {
    InterlockedExchange(&win_stop, 1);
    return TRUE;
  }
  return FALSE;
}

static int win_console_init(void) {
  win_input = GetStdHandle(STD_INPUT_HANDLE);
  win_output = GetStdHandle(STD_OUTPUT_HANDLE);
  if (!GetConsoleMode(win_output, &win_output_mode)) {
    win_redirected = 1;
    return 1;
  }
  win_output_saved = 1;
  win_output_cp = GetConsoleOutputCP();
  win_cursor_saved = GetConsoleCursorInfo(win_output, &win_cursor);
  if (!SetConsoleMode(win_output, win_output_mode | ENABLE_PROCESSED_OUTPUT |
                      ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN) ||
      !SetConsoleOutputCP(CP_UTF8)) {
    fprintf(stderr, "fetch: a console with virtual terminal output is required (use Windows Terminal).\n");
    win_console_cleanup();
    return 0;
  }
  if (GetConsoleMode(win_input, &win_input_mode)) {
    win_input_saved = 1;
    win_input_cp = GetConsoleCP();
    DWORD mode = (win_input_mode | ENABLE_EXTENDED_FLAGS | ENABLE_MOUSE_INPUT |
                  ENABLE_WINDOW_INPUT | ENABLE_PROCESSED_INPUT) &
                 ~(ENABLE_QUICK_EDIT_MODE | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT |
                   ENABLE_VIRTUAL_TERMINAL_INPUT);
    if (!SetConsoleMode(win_input, mode) || !SetConsoleCP(CP_UTF8)) {
      fprintf(stderr, "fetch: could not configure console input.\n");
      win_console_cleanup();
      return 0;
    }
  }
  if (!SetConsoleCtrlHandler(win_control, TRUE)) {
    win_console_cleanup();
    return 0;
  }
  return 1;
}

static void win_term_size(int *rows, int *cols) {
  CONSOLE_SCREEN_BUFFER_INFO info;
  *rows = *cols = 0;
  if (GetConsoleScreenBufferInfo(win_output, &info)) {
    *rows = info.srWindow.Bottom - info.srWindow.Top + 1;
    *cols = info.srWindow.Right - info.srWindow.Left + 1;
  }
}

static int win_poll_input(float *a, float *b, int *dragging, int *last_x,
                          int *last_y, float *vx, float *vy, int *resized) {
  if (InterlockedCompareExchange(&win_stop, 0, 0))
    return 1;
  if (!win_input_saved)
    return 0;
  INPUT_RECORD record;
  DWORD count;
  while (PeekConsoleInputW(win_input, &record, 1, &count) && count) {
    if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown) {
      WORD key = record.Event.KeyEvent.wVirtualKeyCode;
      if (key != VK_SHIFT && key != VK_CONTROL && key != VK_MENU &&
          key != VK_CAPITAL && key != VK_NUMLOCK && key != VK_SCROLL &&
          key != VK_LWIN && key != VK_RWIN)
        return 1; // Leave the key event queued for the shell.
    }
    if (!ReadConsoleInputW(win_input, &record, 1, &count) || !count)
      break;
    if (record.EventType == WINDOW_BUFFER_SIZE_EVENT)
      *resized = 1;
    if (record.EventType != MOUSE_EVENT)
      continue;
    MOUSE_EVENT_RECORD *mouse = &record.Event.MouseEvent;
    int down = (mouse->dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) != 0;
    int x = mouse->dwMousePosition.X, y = mouse->dwMousePosition.Y;
    if (down && !*dragging) {
      *dragging = 1;
      *last_x = x;
      *last_y = y;
      *vx = *vy = 0;
    } else if (down && *dragging && mouse->dwEventFlags == MOUSE_MOVED) {
      *vy = -(x - *last_x) * 0.03f;
      *vx = -(y - *last_y) * 0.03f;
      *b += *vy;
      *a += *vx;
      *last_x = x;
      *last_y = y;
    } else if (!down) {
      *dragging = 0;
    }
  }
  return 0;
}

// Quote one argument using the Windows C runtime's backslash/quote rules.
static int win_quote(wchar_t **cursor, wchar_t *end, const wchar_t *arg) {
  wchar_t *p = *cursor;
  if (p + 2 >= end)
    return 0;
  *p++ = L'"';
  while (*arg) {
    size_t slashes = 0;
    while (*arg == L'\\') { slashes++; arg++; }
    size_t copies = (*arg == L'"' || !*arg) ? slashes * 2 : slashes;
    if (*arg == L'"')
      copies++;
    if (copies >= (size_t)(end - p) || p + copies + 2 >= end)
      return 0;
    while (copies--) *p++ = L'\\';
    if (*arg) *p++ = *arg++;
  }
  if (p + 2 >= end)
    return 0;
  *p++ = L'"';
  *p = L'\0';
  *cursor = p;
  return 1;
}

static wchar_t win_fastfetch_path[32768];
static int win_fastfetch_checked;

static int win_find_fastfetch(void) {
  if (!win_fastfetch_checked) {
    win_fastfetch_checked = 1;
    DWORD n = SearchPathW(NULL, L"fastfetch.exe", NULL,
                         32768, win_fastfetch_path, NULL);
    if (!n || n >= 32768) {
      win_fastfetch_path[0] = L'\0';
      fprintf(stderr, "fetch: fastfetch.exe was not found on PATH; using the built-in logo and native Windows fields.\n");
    }
  }
  return win_fastfetch_path[0] != L'\0';
}

// Drain stdout while waiting: waiting first can deadlock a full pipe.
// Fastfetch runs only before animation, with a bounded timeout and output size.
static FILE *win_fastfetch(const wchar_t *const *args, int argc) {
  if (!win_find_fastfetch())
    return NULL;
  wchar_t command[32768], *cursor = command, *end = command + 32768;
  if (!win_quote(&cursor, end, win_fastfetch_path))
    return NULL;
  for (int i = 0; i < argc; i++) {
    if (cursor + 2 >= end)
      return NULL;
    *cursor++ = L' ';
    if (!win_quote(&cursor, end, args[i]))
      return NULL;
  }
  SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
  HANDLE pipe_read = NULL, pipe_write = NULL;
  HANDLE null_file = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, NULL);
  if (null_file == INVALID_HANDLE_VALUE)
    return NULL;
  if (!CreatePipe(&pipe_read, &pipe_write, &security, 0) ||
      !SetHandleInformation(pipe_read, HANDLE_FLAG_INHERIT, 0)) {
    if (pipe_read) CloseHandle(pipe_read);
    if (pipe_write) CloseHandle(pipe_write);
    CloseHandle(null_file);
    return NULL;
  }
  STARTUPINFOW startup = {0};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = startup.hStdError = null_file;
  startup.hStdOutput = pipe_write;
  PROCESS_INFORMATION process;
  BOOL started = CreateProcessW(win_fastfetch_path, command, NULL, NULL, TRUE,
                               CREATE_NO_WINDOW, NULL, NULL, &startup, &process);
  CloseHandle(pipe_write);
  CloseHandle(null_file);
  if (!started) {
    CloseHandle(pipe_read);
    return NULL;
  }
  FILE *fp = tmpfile();
  ULONGLONG deadline = GetTickCount64() + 10000;
  size_t total = 0;
  int ok = fp != NULL;
  for (;;) {
    DWORD available = 0;
    if (!PeekNamedPipe(pipe_read, NULL, 0, NULL, &available, NULL))
      break;
    if (available) {
      char buf[4096];
      DWORD n = 0;
      if (!ReadFile(pipe_read, buf, available < sizeof(buf) ? available : sizeof(buf), &n, NULL)) {
        ok = 0;
        break;
      }
      total += n;
      if (total > 1024 * 1024 || !fp || fwrite(buf, 1, n, fp) != n) {
        ok = 0;
        break;
      }
    } else if (WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0) {
      break;
    } else {
      Sleep(5);
    }
    if (GetTickCount64() >= deadline || InterlockedCompareExchange(&win_stop, 0, 0)) {
      ok = 0;
      break;
    }
  }
  if (!ok)
    TerminateProcess(process.hProcess, 1);
  if (WaitForSingleObject(process.hProcess, 1000) != WAIT_OBJECT_0) {
    TerminateProcess(process.hProcess, 1);
    WaitForSingleObject(process.hProcess, 1000);
    ok = 0;
  }
  DWORD status = 1;
  GetExitCodeProcess(process.hProcess, &status);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  CloseHandle(pipe_read);
  if (!ok || status != 0) {
    if (fp) fclose(fp);
    return NULL;
  }
  rewind(fp);
  return fp;
}

static int win_logo_exists(const char *name) {
  const wchar_t *args[] = {L"-c", L"none", L"--list-logos", L"--pipe", L"true"};
  FILE *fp = win_fastfetch(args, 5);
  if (!fp)
    return 0;
  char line[2048];
  int found = 0;
  while (!found && fgets(line, sizeof(line), fp)) {
    char *p = line;
    while ((p = strchr(p, '"'))) {
      char *q = strchr(++p, '"');
      if (!q) break;
      *q = '\0';
      if (strcasecmp(p, name) == 0) { found = 1; break; }
      p = q + 1;
    }
  }
  fclose(fp);
  return found;
}

#endif
