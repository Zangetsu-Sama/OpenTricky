/*
 * ot_macos_glue.c -- the macOS cross build only (CMakeLists.txt): what the
 * toolchain in Docker does not bring (no compiler-rt for macOS).
 */
#ifdef __APPLE__
#include <stdint.h>
#include <stdio.h>
#include <sys/sysctl.h>

/* Clang's @available check (SDL3's Objective-C uses it). macOS is platform 1.
 * The same as the game's (port/src/posix/exec_glue.c). */
int32_t __isPlatformVersionAtLeast(uint32_t platform, uint32_t major, uint32_t minor, uint32_t sub)
{
    static int cached = 0, M, m, s;
    if (platform != 1) return 0;
    if (!cached) {
        char v[32] = {0};
        size_t n = sizeof v - 1;
        if (sysctlbyname("kern.osproductversion", v, &n, NULL, 0) == 0)
            sscanf(v, "%d.%d.%d", &M, &m, &s);
        cached = 1;
    }
    if ((uint32_t)M != major) return (uint32_t)M > major;
    if ((uint32_t)m != minor) return (uint32_t)m > minor;
    return (uint32_t)s >= sub;
}
#endif
