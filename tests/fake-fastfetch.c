#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

int wmain(int argc, wchar_t **argv) {
  wchar_t log_path[1024];
  if (GetEnvironmentVariableW(L"FETCH_TEST_LOG", log_path, 1024)) {
    FILE *log = _wfopen(log_path, L"ab");
    if (!log) return 2;
    fputs("call\n", log);
    fclose(log);
  }
  char *mode = getenv("FETCH_TEST_MODE");
  if (mode && strcmp(mode, "fail") == 0) return 9;
  if (mode && strcmp(mode, "timeout") == 0) Sleep(15000);
  for (int i = 1; i < argc; i++) {
    if (wcscmp(argv[i], L"--echo") == 0) {
      for (int j = i + 1; j < argc; j++) {
        char text[4096];
        WideCharToMultiByte(CP_UTF8, 0, argv[j], -1, text, sizeof(text), NULL, NULL);
        printf("%s\n", text);
      }
      return 0;
    }
    if (wcscmp(argv[i], L"--large") == 0) {
      for (int j = 0; j < 200000; j++) putchar('x');
      return 0;
    }
    if (wcscmp(argv[i], L"--list-logos") == 0) {
      puts("Builtin logos:\n1)  \"Windows\" \"Windows 11\"\n2)  \"arch\"\n3)  \"test logo\"");
      return 0;
    }
    if (wcscmp(argv[i], L"--config") == 0 && i + 1 < argc) {
      FILE *config = _wfopen(argv[i + 1], L"rb");
      if (!config || !wcsstr(argv[i + 1], L".json")) return 3;
      fclose(config);
      puts("__fetch_0__: Windows fixture\n__fetch_14__: Fixture CPU\n"
           "__fetch_15__: GPU One\n__fetch_15__: GPU Two\n__fetch_6__: Display One");
      return 0;
    }
  }
  puts("\033[1;34m  @@  \033[0m\n\033[1;32m@@@@@@\033[0m\n\033[1;34m  @@  \033[0m");
  return 0;
}
