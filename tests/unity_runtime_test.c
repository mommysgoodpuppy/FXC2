#include "unity_runtime.h"
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <locale.h>
#include <windows.h>
#include <math.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static int compare(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}
static uint32_t bits(float f) { uint32_t u; memcpy(&u, &f, sizeof(u)); return u; }
static DWORD WINAPI thread(void *arg)
{
    const char *value = arg;
    char setting[80];
    snprintf(setting, sizeof(setting), "VKD3D_HLSL_IMPLICIT_UNROLL_LIMIT=%s", value);
    if (fxc2_putenv(setting)) return 1;
    Sleep(20);
    if (strcmp(fxc2_getenv("VKD3D_HLSL_IMPLICIT_UNROLL_LIMIT"), value)) return 2;
    if (fxc2_putenv("VKD3D_HLSL_IMPLICIT_UNROLL_LIMIT=")) return 3;
    return 0;
}
int main(void)
{
    CHECK(exp2f(0.0f) == 1.0f && exp2f(10.0f) == 1024.0f);
    CHECK(exp2f(-149.0f) == 0x1p-149f);
    CHECK(exp2f(-150.0f) == 0.0f && isinf(exp2f(128.0f)));
    CHECK(isnan(exp2f(NAN)) && exp2f(-INFINITY) == 0.0f);
    /* Disassembly swizzles exposed Unity's %c truncation/va_list bug.
     * Check every truncation boundary, length-only calls, and embedded NUL. */
    {
        static const char expected[] = ".xyzw-tail";
        char buffer[32];
        size_t size;
        for (size = 0; size <= sizeof(expected) + 1; ++size)
        {
            memset(buffer, 0x5a, sizeof(buffer));
            CHECK(snprintf(size ? buffer : NULL, size, ".%s%c%c%c%c%s",
                    "", 'x', 'y', 'z', 'w', "-tail") == sizeof(expected) - 1);
            if (size)
            {
                size_t copied = size - 1 < sizeof(expected) - 1 ? size - 1 : sizeof(expected) - 1;
                CHECK(!memcmp(buffer, expected, copied));
                CHECK(buffer[copied] == 0);
                CHECK(buffer[size] == 0x5a);
            }
        }
        CHECK(snprintf(buffer, sizeof(buffer), "%c%s", 0, "tail") == 5);
        CHECK(!memcmp(buffer, "\0tail\0", 6));
        CHECK(snprintf(buffer, sizeof(buffer), "%*.*s:%08x:%.2f", 4, 2, "abcd", 0x12u, 1.25) == 18);
        CHECK(!strcmp(buffer, "  ab:00000012:1.25"));
    }
    int values[] = {5, 2, 5, -1, INT_MAX, INT_MIN, 0, 17, -100};
    int key = 17;
    char *p, *end;
    HANDLE threads[2];
    DWORD result;
    CHECK(fxc2_strlen("") == 0 && fxc2_strlen("abc") == 3);
    CHECK(strnlen("abc", 2) == 2 && strcspn("abc,def", ",") == 3);
    CHECK(fxc2_calloc(SIZE_MAX, 2) == NULL);
    p = fxc2_calloc(17, 13);
    CHECK(p != NULL);
    for (int i = 0; i < 221; ++i) CHECK(p[i] == 0);
    free(p);
    qsort(values, sizeof(values) / sizeof(*values), sizeof(*values), compare);
    for (size_t i = 1; i < sizeof(values) / sizeof(*values); ++i) CHECK(values[i - 1] <= values[i]);
    CHECK(*(int *)bsearch(&key, values, 9, sizeof(*values), compare) == 17);
    key = 18; CHECK(bsearch(&key, values, 9, sizeof(*values), compare) == NULL);
    CHECK(strtoul("  -1tail", &end, 0) == ULONG_MAX && !strcmp(end, "tail"));
    CHECK(strtoul("0x10!", &end, 0) == 16 && *end == '!');
    CHECK(strtoul("010", &end, 0) == 8 && !*end);
    CHECK(strtoul("0x", &end, 0) == 0 && *end == 'x');
    CHECK(strtoul("z", &end, 36) == 35 && !*end);
    errno = 0; CHECK(strtoul("4294967296tail", &end, 10) == ULONG_MAX && errno == ERANGE && *end == 't');
    CHECK(_create_locale(LC_ALL, "C") != NULL);
    CHECK(_create_locale(LC_ALL, "unsupported") == NULL);
    CHECK(bits(_strtof_l("1.000000059604644775390625", &end, NULL)) == bits(1.0f));
    CHECK(!*end);
    CHECK(bits(_strtof_l(" -0.0f", &end, NULL)) == 0x80000000u && *end == 'f');
    errno = 0; CHECK(bits(_strtof_l("1e9999", &end, NULL)) == 0x7f800000u && errno == ERANGE);
    errno = 0; CHECK(bits(_strtof_l("1e-9999", &end, NULL)) == 0 && errno == ERANGE);
    const char *invalid = "  +bad";
    CHECK(_strtof_l(invalid, &end, NULL) == 0 && end == invalid);
    // Compare 20,000 decimal constants with the host's original UCRT parser.
    HMODULE crt = LoadLibraryA("ucrtbase.dll");
    typedef float (__cdecl *parse_fn)(const char *, char **, _locale_t);
    parse_fn reference = (parse_fn)GetProcAddress(crt, "_strtof_l");
    CHECK(reference != NULL);
    uint32_t random = 0x31415926;
    for (int i = 0; i < 20000; ++i)
    {
        char number[80];
        random = random * 1664525u + 1013904223u;
        snprintf(number, sizeof(number), "%s%ue%d", (random & 1) ? "-" : "", random, (int)(random % 100) - 60);
        char *a, *b;
        CHECK(bits(_strtof_l(number, &a, NULL)) == bits(reference(number, &b, NULL)));
        CHECK(a == b);
    }
    threads[0] = CreateThread(NULL, 0, thread, "17", 0, NULL);
    threads[1] = CreateThread(NULL, 0, thread, "254", 0, NULL);
    CHECK(threads[0] && threads[1]);
    CHECK(WaitForMultipleObjects(2, threads, TRUE, 10000) == WAIT_OBJECT_0);
    for (int i = 0; i < 2; ++i) { GetExitCodeThread(threads[i], &result); CHECK(result == 0); CloseHandle(threads[i]); }
    CHECK(fxc2_getenv("VKD3D_HLSL_IMPLICIT_UNROLL_LIMIT") == NULL);
    printf("PASS runtime helpers, 20000 UCRT float comparisons, per-thread retry\n");
    return 0;
}
