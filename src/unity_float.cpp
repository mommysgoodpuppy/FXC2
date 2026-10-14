// HLSL's lexer accepts decimal float constants and requests the C locale.
// fast_float provides correctly rounded float32 conversion without heap use.
#include "unity_runtime.h"
#include <cerrno>
#include <locale.h>
#include <fast_float/fast_float.h>

extern "C" float _strtof_l(const char *input, char **end, _locale_t locale)
{
    (void)locale;
    const char *start = input;
    while (*start == ' ' || (*start >= '\t' && *start <= '\r')) ++start;
    float value = 0;
    auto result = fast_float::from_chars(start, start + fxc2_strlen(start), value,
        fast_float::chars_format::general | fast_float::chars_format::allow_leading_plus);
    if (result.ec == std::errc::invalid_argument)
    {
        if (end) *end = const_cast<char *>(input);
        return 0;
    }
    if (end) *end = const_cast<char *>(result.ptr);
    if (result.ec == std::errc::result_out_of_range) errno = ERANGE;
    return value;
}
