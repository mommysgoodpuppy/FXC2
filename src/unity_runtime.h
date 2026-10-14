/* Runtime calls not supplied by Linux Unity's PE loader. Force-include this
 * before CRT headers when building the Unity variant. */
#ifndef FXC2_UNITY_RUNTIME_H
#define FXC2_UNITY_RUNTIME_H
// vkd3d must establish its Windows union macros before any CRT header.
// Declare these with the compiler's size type; CRT declarations appear later.
#define __USE_MINGW_ANSI_STDIO 0
/* The adapter's narrow C formatter does not use UCRT legacy locale modes. */
#define _CRT_INTERNAL_LOCAL_PRINTF_OPTIONS 0ULL
#define _CRTIMP
#ifdef __cplusplus
extern "C" {
#endif
__SIZE_TYPE__ fxc2_strlen(const char *s);
void *fxc2_calloc(__SIZE_TYPE__ count, __SIZE_TYPE__ size);
char *fxc2_getenv(const char *name);
int fxc2_putenv(const char *value);
#ifdef __cplusplus
}
#endif
#define strlen fxc2_strlen
#define calloc fxc2_calloc
#define getenv fxc2_getenv
#define _putenv fxc2_putenv
#endif
