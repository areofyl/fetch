#define main fetch_program_main
#define wmain fetch_program_wmain
#include "../fetch.c"
#undef main
#undef wmain

static FILE *report;
static int failures;
#define CHECK(test) do { \
  if (!(test)) { fprintf(report, "FAIL line %d: %s\n", __LINE__, #test); fflush(report); failures++; } \
} while (0)

static PROCESS_INFORMATION launch(const wchar_t *exe, const wchar_t *options) {
  wchar_t command[32768], *p = command;
  PROCESS_INFORMATION process = {0};
  CHECK(win_quote(&p, command + 32768, exe));
  *p++ = L' ';
  wcscpy(p, options);
  STARTUPINFOW startup = {0};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = win_input;
  startup.hStdOutput = startup.hStdError = win_output;
  CHECK(CreateProcessW(exe, command, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &process));
  return process;
}

static int active(PROCESS_INFORMATION *process, DWORD baseline) {
  ULONGLONG deadline = GetTickCount64() + 4000;
  do {
    DWORD mode;
    if (GetConsoleMode(win_input, &mode) && mode != baseline)
      { Sleep(100); return 1; }
    if (WaitForSingleObject(process->hProcess, 0) == WAIT_OBJECT_0)
      return 0;
    Sleep(10);
  } while (GetTickCount64() < deadline);
  return 0;
}

static void finish(PROCESS_INFORMATION *process) {
  if (!process->hProcess)
    return;
  DWORD wait = WaitForSingleObject(process->hProcess, 7000);
  CHECK(wait == WAIT_OBJECT_0);
  if (wait != WAIT_OBJECT_0) {
    TerminateProcess(process->hProcess, 99);
    WaitForSingleObject(process->hProcess, 1000);
  }
  DWORD code = 99;
  GetExitCodeProcess(process->hProcess, &code);
  CHECK(code == 0);
  CloseHandle(process->hProcess);
  CloseHandle(process->hThread);
}

static void restored(DWORD input_mode, DWORD output_mode, UINT input_cp,
                     UINT output_cp, BOOL cursor_visible) {
  DWORD mode;
  CONSOLE_CURSOR_INFO cursor;
  CHECK(GetConsoleMode(win_input, &mode) && mode == input_mode);
  CHECK(GetConsoleMode(win_output, &mode) && mode == output_mode);
  CHECK(GetConsoleCP() == input_cp);
  CHECK(GetConsoleOutputCP() == output_cp);
  CHECK(GetConsoleCursorInfo(win_output, &cursor) && cursor.bVisible == cursor_visible);
}

static int console_tests(const wchar_t *exe) {
  report = fopen("tests/.work/console-report.txt", "w");
  if (!report) return 2;
  SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
  win_input = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, NULL);
  win_output = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, NULL);
  CHECK(win_input != INVALID_HANDLE_VALUE && win_output != INVALID_HANDLE_VALUE);
  SetStdHandle(STD_INPUT_HANDLE, win_input);
  SetStdHandle(STD_OUTPUT_HANDLE, win_output);
  DWORD input_mode = ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT |
                     ENABLE_EXTENDED_FLAGS | ENABLE_QUICK_EDIT_MODE;
  DWORD output_mode = ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT;
  SetConsoleMode(win_input, input_mode);
  SetConsoleMode(win_output, output_mode);
  CHECK(SetConsoleCP(1252));
  CHECK(SetConsoleOutputCP(437));
  CONSOLE_CURSOR_INFO cursor = {25, FALSE};
  CHECK(SetConsoleCursorInfo(win_output, &cursor));
  FlushConsoleInputBuffer(win_input);

  CHECK(win_console_init());
  INPUT_RECORD records[4] = {0};
  records[0].EventType = MOUSE_EVENT;
  records[0].Event.MouseEvent.dwButtonState = FROM_LEFT_1ST_BUTTON_PRESSED;
  records[0].Event.MouseEvent.dwMousePosition = (COORD){10, 5};
  records[1] = records[0];
  records[1].Event.MouseEvent.dwEventFlags = MOUSE_MOVED;
  records[1].Event.MouseEvent.dwMousePosition = (COORD){16, 8};
  records[2] = records[1];
  records[2].Event.MouseEvent.dwButtonState = 0;
  records[3].EventType = WINDOW_BUFFER_SIZE_EVENT;
  records[3].Event.WindowBufferSizeEvent.dwSize = (COORD){50, 18};
  DWORD count;
  CHECK(WriteConsoleInputW(win_input, records, 4, &count) && count == 4);
  float a = 0, b = 0, vx = 0, vy = 0;
  int dragging = 0, x = 0, y = 0, resized = 0;
  CHECK(!win_poll_input(&a, &b, &dragging, &x, &y, &vx, &vy, &resized));
  CHECK(fabsf(a + 0.09f) < 0.001f && fabsf(b + 0.18f) < 0.001f);
  CHECK(!dragging && resized && vx != 0 && vy != 0);
  INPUT_RECORD key = {0};
  key.EventType = KEY_EVENT;
  key.Event.KeyEvent.bKeyDown = TRUE;
  key.Event.KeyEvent.wVirtualKeyCode = 'Q';
  key.Event.KeyEvent.uChar.UnicodeChar = L'q';
  CHECK(WriteConsoleInputW(win_input, &key, 1, &count));
  CHECK(win_poll_input(&a, &b, &dragging, &x, &y, &vx, &vy, &resized));
  INPUT_RECORD peek;
  CHECK(PeekConsoleInputW(win_input, &peek, 1, &count) && count == 1 &&
        peek.Event.KeyEvent.uChar.UnicodeChar == L'q');
  FlushConsoleInputBuffer(win_input);
  win_console_cleanup();
  win_console_cleanup();
  SetConsoleCtrlHandler(win_control, FALSE);
  restored(input_mode, output_mode, 1252, 437, FALSE);

  PROCESS_INFORMATION process = launch(exe, L"--frames 26 --shading-mode blocks");
  CHECK(active(&process, input_mode));
  SMALL_RECT window = {0, 0, 49, 17};
  CHECK(SetConsoleWindowInfo(win_output, TRUE, &window));
  CHECK(WriteConsoleInputW(win_input, records, 3, &count));
  finish(&process);
  restored(input_mode, output_mode, 1252, 437, FALSE);

  FlushConsoleInputBuffer(win_input);
  process = launch(exe, L"--infinite --no-info --shading-mode sextants");
  CHECK(active(&process, input_mode));
  CHECK(WriteConsoleInputW(win_input, &key, 1, &count));
  finish(&process);
  restored(input_mode, output_mode, 1252, 437, FALSE);
  CHECK(PeekConsoleInputW(win_input, &peek, 1, &count) && count == 1 &&
        peek.EventType == KEY_EVENT && peek.Event.KeyEvent.uChar.UnicodeChar == L'q');

  FlushConsoleInputBuffer(win_input);
  process = launch(exe, L"--infinite --no-info");
  CHECK(active(&process, input_mode));
  CHECK(SetConsoleCtrlHandler(NULL, TRUE));
  CHECK(GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0));
  finish(&process);
  SetConsoleCtrlHandler(NULL, FALSE);
  restored(input_mode, output_mode, 1252, 437, FALSE);
  fprintf(report, "Hidden-console integration tests: %s\n", failures ? "FAILED" : "passed");
  fclose(report);
  return failures ? 1 : 0;
}

int wmain(int argc, wchar_t **argv) {
  if (argc == 3 && wcscmp(argv[1], L"--child") == 0)
    return console_tests(argv[2]);
  if (argc != 2) return 2;
  wchar_t self[32768], command[32768], *p = command;
  GetModuleFileNameW(NULL, self, 32768);
  if (!win_quote(&p, command + 32768, self)) return 2;
  wcscpy(p, L" --child "); p += 9;
  if (!win_quote(&p, command + 32768, argv[1])) return 2;
  STARTUPINFOW startup = {0};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESHOWWINDOW;
  startup.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION process;
  if (!CreateProcessW(self, command, NULL, NULL, FALSE, CREATE_NEW_CONSOLE,
                      NULL, NULL, &startup, &process)) return 2;
  DWORD wait = WaitForSingleObject(process.hProcess, 30000), status = 99;
  if (wait != WAIT_OBJECT_0) TerminateProcess(process.hProcess, 99);
  GetExitCodeProcess(process.hProcess, &status);
  CloseHandle(process.hProcess);
  CloseHandle(process.hThread);
  return (int)status;
}
