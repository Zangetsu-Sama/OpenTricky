/*
 * ot_macos_cxx_compat.h -- force-included in the C++ sources of the macOS
 * cross build only (CMakeLists.txt). The macOS SDK's libc++ headers (LLVM 21)
 * call the type-generic bit builtins of clang 19+; the Docker image's
 * clang 18 lacks them. Same results, from the older fixed-width builtins.
 */
#ifndef OT_MACOS_CXX_COMPAT_H
#define OT_MACOS_CXX_COMPAT_H
#if defined(__cplusplus) && defined(__clang__) && __clang_major__ < 19 && !__has_builtin(__builtin_clzg)

template <class T> constexpr int ot_compat_clz(T x)
{
    if (sizeof(T) <= sizeof(unsigned)) return __builtin_clz((unsigned)x) - (int)(8 * (sizeof(unsigned) - sizeof(T)));
    if (sizeof(T) <= sizeof(unsigned long long)) return __builtin_clzll((unsigned long long)x) - (int)(8 * (sizeof(unsigned long long) - sizeof(T)));
    unsigned long long hi = (unsigned long long)(x >> 64);
    return hi ? __builtin_clzll(hi) : 64 + __builtin_clzll((unsigned long long)x);
}
template <class T> constexpr int ot_compat_ctz(T x)
{
    if (sizeof(T) <= sizeof(unsigned long long)) return __builtin_ctzll((unsigned long long)x);
    unsigned long long lo = (unsigned long long)x;
    return lo ? __builtin_ctzll(lo) : 64 + __builtin_ctzll((unsigned long long)(x >> 64));
}
template <class T> constexpr int ot_compat_clzg(T x, int if_zero) { return x == 0 ? if_zero : ot_compat_clz(x); }
template <class T> constexpr int ot_compat_clzg(T x) { return ot_compat_clz(x); }
template <class T> constexpr int ot_compat_ctzg(T x, int if_zero) { return x == 0 ? if_zero : ot_compat_ctz(x); }
template <class T> constexpr int ot_compat_ctzg(T x) { return ot_compat_ctz(x); }
template <class T> constexpr int ot_compat_popcountg(T x)
{
    if (sizeof(T) <= sizeof(unsigned long long)) return __builtin_popcountll((unsigned long long)x);
    return __builtin_popcountll((unsigned long long)x) + __builtin_popcountll((unsigned long long)(x >> 64));
}
#define __builtin_clzg(...) ot_compat_clzg(__VA_ARGS__)
#define __builtin_ctzg(...) ot_compat_ctzg(__VA_ARGS__)
#define __builtin_popcountg(...) ot_compat_popcountg(__VA_ARGS__)

#endif
#endif
