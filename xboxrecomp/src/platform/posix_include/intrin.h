/* POSIX/arm64 stand-in for MSVC <intrin.h>: the few intrinsics the runtime uses. */
#pragma once
#include <stdint.h>
#if defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#else
static inline uint64_t __rdtsc(void)
{
#if defined(__aarch64__)
    uint64_t v; __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v)); return v;
#else
    return 0;
#endif
}
static inline void _mm_pause(void)
{
#if defined(__aarch64__)
    __asm__ volatile("yield");
#endif
}
#endif
#ifndef _ReadWriteBarrier
#define _ReadWriteBarrier() __asm__ volatile("" ::: "memory")
#endif
