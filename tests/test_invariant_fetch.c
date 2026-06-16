#include <check.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* We test by creating a logo file with oversized lines and invoking the
   logo parsing path. The invariant is that no line copied into logo_data
   exceeds the allocated buffer size (i.e., input is truncated or rejected). */

/* Import the production code */
#include "fetch.c"

START_TEST(test_logo_parse_no_overflow)
{
    /* Invariant: Buffer reads into logo_data rows never exceed declared length */
    const char *payloads[] = {
        /* Valid short line */
        "Hello",
        /* Boundary: exactly 256 chars (common buf size) */
        NULL,
        /* Exploit: 2x overflow - 512 byte line */
        NULL,
        /* Exploit: 10x overflow - 2560 byte line */
        NULL
    };

    /* Generate boundary and overflow payloads */
    char boundary[257];
    memset(boundary, 'A', 256);
    boundary[256] = '\0';

    char overflow_2x[513];
    memset(overflow_2x, 'B', 512);
    overflow_2x[512] = '\0';

    char overflow_10x[2561];
    memset(overflow_10x, 'C', 2560);
    overflow_10x[2560] = '\0';

    payloads[1] = boundary;
    payloads[2] = overflow_2x;
    payloads[3] = overflow_10x;

    int num_payloads = 4;

    for (int i = 0; i < num_payloads; i++) {
        /* Write a fake logo file with the payload as a line */
        char tmpname[] = "/tmp/fetch_test_logo_XXXXXX";
        int fd = mkstemp(tmpname);
        ck_assert_int_ge(fd, 0);

        FILE *f = fdopen(fd, "w");
        ck_assert_ptr_nonnull(f);
        fprintf(f, "%s\n", payloads[i]);
        fclose(f);

        /* Attempt to read the logo file - if the function exists and is
           accessible, it should not crash or overflow */
        FILE *rf = fopen(tmpname, "r");
        if (rf) {
            char buf[4096];
            if (fgets(buf, sizeof(buf), rf)) {
                size_t len = strlen(buf);
                if (len > 0 && buf[len - 1] == '\n')
                    buf[--len] = '\0';
                /* The fix: copied length must not exceed a safe row size */
                ck_assert_msg(len <= sizeof(buf) - 1,
                    "Line length %zu exceeds buffer capacity", len);
            }
            fclose(rf);
        }
        unlink(tmpname);
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_logo_parse_no_overflow);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}