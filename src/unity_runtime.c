/* Compatibility helpers for Unity's Linux PE loader. These are real
 * implementations, not failure thunks. Only the compiler's own environment
 * override is local; other getenv calls still use the host's implementation. */
#include "unity_runtime.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <windows.h>
#undef getenv
extern char *getenv(const char *name);

size_t fxc2_strlen(const char *s)
{
    const char *p = s;
    while (*p) ++p;
    return (size_t)(p - s);
}

void *fxc2_calloc(size_t count, size_t size)
{
    void *p;
    if (size && count > SIZE_MAX / size) return NULL;
    size *= count;
    if ((p = malloc(size))) memset(p, 0, size);
    return p;
}

/* The old shim changed a process-wide environment variable for its unroll
 * retry. Store that override per thread instead. Unity supplies TLS APIs.
 * Index -2 means another thread is initializing the slot. */
static volatile LONG unroll_tls = -1;
static DWORD tls_slot(volatile LONG *state)
{
    LONG index = InterlockedCompareExchange(state, -2, -1);
    if (index == -1)
    {
        DWORD slot = TlsAlloc();
        InterlockedExchange(state, (LONG)slot);
        return slot;
    }
    while (index == -2)
    {
        Sleep(0);
        index = InterlockedCompareExchange(state, -2, -2);
    }
    return (DWORD)index;
}

char *fxc2_getenv(const char *name)
{
    char *value;
    if (!strcmp(name, "VKD3D_HLSL_IMPLICIT_UNROLL_LIMIT"))
    {
        DWORD slot = tls_slot(&unroll_tls);
        if (slot != TLS_OUT_OF_INDEXES && (value = TlsGetValue(slot))) return value;
    }
    value = getenv(name);
    if (!value && (!strcmp(name, "VKD3D_DEBUG") || !strcmp(name, "VKD3D_SHADER_DEBUG")))
        return "none";
    return value;
}

int fxc2_putenv(const char *value)
{
    static const char key[] = "VKD3D_HLSL_IMPLICIT_UNROLL_LIMIT=";
    char *old, *next = NULL;
    DWORD slot;
    size_t length;
    if (strncmp(value, key, sizeof(key) - 1)) return -1;
    value += sizeof(key) - 1;
    if ((slot = tls_slot(&unroll_tls)) == TLS_OUT_OF_INDEXES) return -1;
    if (*value)
    {
        length = fxc2_strlen(value) + 1;
        if (!(next = malloc(length))) return -1;
        memcpy(next, value, length);
    }
    old = TlsGetValue(slot);
    if (!TlsSetValue(slot, next)) { free(next); return -1; }
    free(old);
    return 0;
}

/* LLVM can recognize malloc+memset / string loops in other translation
 * units and introduce these standard names after preprocessing. */
#undef strlen
#undef calloc
size_t strlen(const char *s) { return fxc2_strlen(s); }
void *calloc(size_t count, size_t size) { return fxc2_calloc(count, size); }


static volatile LONG errno_tls = -1;
int *_errno(void)
{
    static int exhausted;
    DWORD slot = tls_slot(&errno_tls);
    int *value;
    if (slot == TLS_OUT_OF_INDEXES) return &exhausted;
    if ((value = TlsGetValue(slot))) return value;
    value = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*value));
    if (!value) return &exhausted;
    if (!TlsSetValue(slot, value)) { HeapFree(GetProcessHeap(), 0, value); return &exhausted; }
    return value;
}

size_t strcspn(const char *s, const char *reject)
{
    size_t n = 0;
    while (s[n] && !strchr(reject, s[n])) ++n;
    return n;
}
size_t strnlen(const char *s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n]) ++n;
    return n;
}
char *strcpy(char *to, const char *from)
{
    char *start = to;
    while ((*to++ = *from++)) {}
    return start;
}
void *memchr(const void *data, int c, size_t size)
{
    const unsigned char *p = data;
    while (size--) { if (*p == (unsigned char)c) return (void *)p; ++p; }
    return NULL;
}
size_t wcslen(const wchar_t *s)
{
    const wchar_t *p = s;
    while (*p) ++p;
    return (size_t)(p - s);
}
size_t wcsnlen(const wchar_t *s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n]) ++n;
    return n;
}

static void byte_swap(unsigned char *a, unsigned char *b, size_t size)
{
    while (size--) { unsigned char c = *a; *a++ = *b; *b++ = c; }
}
static void sift(unsigned char *base, size_t root, size_t count, size_t size,
        int (*compare)(const void *, const void *))
{
    while (root < count / 2)
    {
        size_t child = root * 2 + 1;
        if (child + 1 < count && compare(base + child * size, base + (child + 1) * size) < 0) ++child;
        if (compare(base + root * size, base + child * size) >= 0) return;
        byte_swap(base + root * size, base + child * size, size);
        root = child;
    }
}
void qsort(void *data, size_t count, size_t size, int (*compare)(const void *, const void *))
{
    unsigned char *base = data;
    size_t i;
    if (count < 2 || !size) return;
    for (i = count / 2; i; --i) sift(base, i - 1, count, size, compare);
    for (i = count - 1; i; --i)
    {
        byte_swap(base, base + i * size, size);
        sift(base, 0, i, size, compare);
    }
}
void *bsearch(const void *key, const void *data, size_t count, size_t size,
        int (*compare)(const void *, const void *))
{
    const unsigned char *base = data;
    while (count)
    {
        size_t middle = count / 2;
        const void *at = base + middle * size;
        int order = compare(key, at);
        if (!order) return (void *)at;
        if (order < 0) count = middle;
        else { base += (middle + 1) * size; count -= middle + 1; }
    }
    return NULL;
}

/* The compiler requests only the C locale, and never inspects this token. */
#include <locale.h>
_locale_t _create_locale(int category, const char *name)
{
    static char c_locale;
    (void)category;
    if (name && (!strcmp(name, "C") || !strcmp(name, "POSIX"))) return (_locale_t)&c_locale;
    return NULL;
}
void _free_locale(_locale_t locale) { (void)locale; }

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <wchar.h>
#include <math.h>

/* Used for HLSL constant folding; Unity provides double pow, not exp2f. */
float exp2f(float value)
{
    return (float)pow(2.0, (double)value);
}

/* The linked formatter measures narrow strings via these C-locale helpers.
 * Its compiler formats are byte strings, not a host-locale conversion. */
size_t mbrtowc(wchar_t *output, const char *input, size_t count, mbstate_t *state)
{
    (void)state;
    if (!input) return 0;
    if (!count) return (size_t)-2;
    if (output) *output = (unsigned char)*input;
    return *input ? 1 : 0;
}
size_t wcrtomb(char *output, wchar_t input, mbstate_t *state)
{
    (void)state;
    if (!output) return 1;
    if (input > UCHAR_MAX) { errno = EILSEQ; return (size_t)-1; }
    *output = (char)input;
    return 1;
}

/* MinGW's formatter uses localeconv even for narrow strings. Keep compiler
 * formatting in the C locale, with the Windows (not Linux) lconv layout. */
struct lconv *localeconv(void)
{
    static struct lconv locale = {
        .decimal_point = ".", .thousands_sep = "", .grouping = "",
        .int_curr_symbol = "", .currency_symbol = "", .mon_decimal_point = "",
        .mon_thousands_sep = "", .mon_grouping = "", .positive_sign = "", .negative_sign = "",
        .int_frac_digits = CHAR_MAX, .frac_digits = CHAR_MAX,
        .p_cs_precedes = CHAR_MAX, .p_sep_by_space = CHAR_MAX,
        .n_cs_precedes = CHAR_MAX, .n_sep_by_space = CHAR_MAX,
        .p_sign_posn = CHAR_MAX, .n_sign_posn = CHAR_MAX,
        ._W_decimal_point = L".", ._W_thousands_sep = L"",
        ._W_int_curr_symbol = L"", ._W_currency_symbol = L"",
        ._W_mon_decimal_point = L"", ._W_mon_thousands_sep = L"",
        ._W_positive_sign = L"", ._W_negative_sign = L"",
    };
    return &locale;
}

/* Unity's formatter consumes a %c argument only when it writes the character.
 * A truncated "%c%s" then treats the character as a string pointer. Provide
 * the UCRT entry locally so existing snprintf/sprintf wrappers use MinGW's
 * linked C99 formatter instead. The compiler uses narrow C-locale formatting
 * and standard snprintf sizing; legacy _vsnprintf modes are not supported. */
int __cdecl __stdio_common_vsprintf(unsigned __int64 options, char *buffer, size_t size,
        const char *format, _locale_t locale, va_list args)
{
    /* The toolchain's prebuilt wrappers also set legacy wide-specifier flags;
     * they do not change the compiler's narrow formats. */
    if (locale || (options & _CRT_INTERNAL_PRINTF_LEGACY_VSPRINTF_NULL_TERMINATION))
    {
        errno = EINVAL;
        return -1;
    }
    return __mingw_vsnprintf(buffer, size, format, args);
}

static int digit_value(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return -1;
}
unsigned long strtoul(const char *input, char **end, int base)
{
    const char *p = input;
    unsigned long value = 0;
    int negative = 0, overflow = 0, any = 0, d;
    if (end) *end = (char *)input;
    if (base && (base < 2 || base > 36)) { errno = EINVAL; return 0; }
    while (*p == ' ' || (*p >= '\t' && *p <= '\r')) ++p;
    if (*p == '+' || *p == '-') { negative = *p == '-'; ++p; }
    if ((!base || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') &&
        digit_value((unsigned char)p[2]) >= 0 && digit_value((unsigned char)p[2]) < 16)
    { base = 16; p += 2; }
    if (!base) base = *p == '0' ? 8 : 10;
    while ((d = digit_value((unsigned char)*p)) >= 0 && d < base)
    {
        any = 1;
        if (value > (ULONG_MAX - (unsigned long)d) / (unsigned long)base) overflow = 1;
        else if (!overflow) value = value * (unsigned long)base + (unsigned long)d;
        ++p;
    }
    if (!any) return 0;
    if (end) *end = (char *)p;
    if (overflow) { errno = ERANGE; return ULONG_MAX; }
    return negative ? 0ul - value : value;
}
