#define main fetch_program_main
#define wmain fetch_program_wmain
#include "../fetch.c"
#undef main
#undef wmain

static int failures;
#define CHECK(test) do { \
  if (!(test)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #test); failures++; } \
} while (0)

int main(int argc, char **argv) {
  float number = 0;
  int integer = 0;
  CHECK(parse_number("test", "1.5", 0, 5, &number) && number == 1.5f);
  const char *bad_numbers[] = {"", "nan", "inf", "1abc", "1e9999", "-1"};
  for (unsigned i = 0; i < sizeof(bad_numbers) / sizeof(bad_numbers[0]); i++)
    CHECK(!parse_number("test", bad_numbers[i], 0, 5, &number));
  CHECK(parse_integer("test", "12", 0, INT_MAX, &integer) && integer == 12);
  const char *bad_integers[] = {"", "-1", "1.5", "12junk", "2147483648", "9999999999999999999999"};
  for (unsigned i = 0; i < sizeof(bad_integers) / sizeof(bad_integers[0]); i++)
    CHECK(!parse_integer("test", bad_integers[i], 0, INT_MAX, &integer));

  FILE *fixture = tmpfile();
  CHECK(fixture != NULL);
  if (fixture) {
    fputs("__fetch_15__: GPU One\n__fetch_15__: GPU Two\n"
          "__fetch_0__: Windows fixture\n__fetch_-1__: nope\n"
          "__fetch_999__: nope\n__fetch_bad__: nope\n"
          "__fetch_18__ (C:\\)__end__: 10 GiB / 20 GiB\n", fixture);
    rewind(fixture);
    win_parse_info(fixture);
    fclose(fixture);
    CHECK(win_value_count[F_GPU] == 2);
    CHECK(strcmp(win_values[F_GPU][1], "GPU Two") == 0);
    CHECK(win_value_count[F_OS] == 1);
    CHECK(strcmp(win_labels[F_DISK][0], " (C:\\)") == 0);
    CHECK(strcmp(win_values[F_DISK][0], "10 GiB / 20 GiB") == 0);
  }

  for (int i = 0; i < F_COUNT; i++) field_line[i] = -1;
  current_field = F_MEMORY;
  gather_memory();
  CHECK(fetch_line_count == 1 && strstr(fetch_lines[0], "GiB"));
  current_field = F_UPTIME;
  gather_uptime();
  CHECK(fetch_line_count == 2);
  is_refresh_pass = 1;
  current_field = F_MEMORY;
  gather_memory();
  current_field = F_UPTIME;
  gather_uptime();
  CHECK(fetch_line_count == 2);
  is_refresh_pass = 0;

  fetch_line_count = 0;
  for (int i = 0; i < F_COUNT; i++) field_line[i] = -1;
  current_field = -1;
  add_line("title"); add_line("-----");
  char long_value[400];
  memset(long_value, 'x', sizeof(long_value) - 1);
  long_value[sizeof(long_value) - 1] = '\0';
  for (int i = 0; i < MAX_FETCH_LINES - 2; i++) {
    current_field = F_MEMORY;
    add_info("Long field", "%s", long_value);
  }
  current_field = F_GPU;
  add_info("Dropped", "value");
  CHECK(field_line[F_GPU] == -1);
  box_wrap_lines();
  CHECK(fetch_line_count == MAX_FETCH_LINES && field_line[F_MEMORY] == -1);
  CHECK(strlen(fetch_lines[2]) < MAX_LINE_LEN);
  fetch_line_count = 2;
  box_width = 0;

  int widths[] = {1, 10, 30, 80, 160};
  int heights[] = {1, 5, 15, 40};
  for (unsigned w = 0; w < sizeof(widths) / sizeof(widths[0]); w++) {
    for (unsigned h = 0; h < sizeof(heights) / sizeof(heights[0]); h++) {
      term_cols = widths[w]; term_rows = heights[h];
      apply_layout(1);
      CHECK(anim_width >= 0 && anim_width <= term_cols);
      CHECK(render_height >= 0 && render_height <= MAX_HEIGHT);
      CHECK(stacked_info_rows >= 0 && stacked_info_rows <= fetch_line_count);
    }
  }

  for (int shape = 0; shape < 3; shape++) {
    logo_rows = shape == 2 ? 3 : 1;
    for (int r = 0; r < logo_rows; r++)
      strcpy(logo_data[r], shape == 1 ? "@@@" : "@");
    process_logo();
    POINT_COUNT = 0;
    config_depth = 1.0f;
    build_points();
    CHECK(POINT_COUNT > 0);
    for (int i = 0; i < POINT_COUNT; i++)
      CHECK(isfinite(PX[i]) && isfinite(PY[i]) && isfinite(PZ[i]) &&
            isfinite(NX[i]) && isfinite(NY[i]) && isfinite(NZ[i]));
  }

  if (argc > 1) {
    wchar_t *path = win_wide(argv[1]);
    CHECK(path != NULL);
    if (path) {
      wcscpy(win_fastfetch_path, path);
      win_fastfetch_checked = 1;
      free(path);
      const wchar_t *args[] = {L"--echo", L"", L"space here", L"quote\"here",
                               L"trailing\\", L"two\\\\\"quotes", L"Gr\u00fc\u00dfe", L"&|<>%PATH%"};
      const char *expected[] = {"", "space here", "quote\"here", "trailing\\",
                                "two\\\\\"quotes", "Gr\xc3\xbc\xc3\x9f\x65", "&|<>%PATH%"};
      FILE *fp = win_fastfetch(args, 8);
      CHECK(fp != NULL);
      if (fp) {
        char line[4096];
        for (int i = 0; i < 7; i++) {
          CHECK(fgets(line, sizeof(line), fp) != NULL);
          line[strcspn(line, "\r\n")] = '\0';
          CHECK(strcmp(line, expected[i]) == 0);
        }
        fclose(fp);
      }
      const wchar_t *large[] = {L"--large"};
      fp = win_fastfetch(large, 1);
      CHECK(fp != NULL);
      if (fp) { fseek(fp, 0, SEEK_END); CHECK(ftell(fp) == 200000); fclose(fp); }
      SetEnvironmentVariableW(L"FETCH_TEST_MODE", L"fail");
      CHECK(win_fastfetch(args, 8) == NULL);
      SetEnvironmentVariableW(L"FETCH_TEST_MODE", L"timeout");
      ULONGLONG start = GetTickCount64();
      CHECK(win_fastfetch(args, 8) == NULL);
      CHECK(GetTickCount64() - start < 12000);
      SetEnvironmentVariableW(L"FETCH_TEST_MODE", NULL);
    }
  }
  printf("Windows unit tests: %s\n", failures ? "FAILED" : "passed");
  return failures ? 1 : 0;
}
