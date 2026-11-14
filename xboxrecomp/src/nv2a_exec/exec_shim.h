/* SPDX-License-Identifier: GPL-3.0-only
 * exec_shim.h -- OpenTricky (macOS): what nv2a_pb_exec.c, from SSX3-recomp's
 * toolkit, takes from that toolkit's kernel. */
#ifndef NV2A_EXEC_SHIM_H
#define NV2A_EXEC_SHIM_H
#include <stdint.h>
#include <stddef.h>
#include "win32_compat.h"
/* The contiguous-memory window (MmAllocateContiguousMemory), as there. */
#ifndef XBOX_CONTIG_BASE
#define XBOX_CONTIG_BASE 0x80000000u
#define XBOX_CONTIG_SIZE (64u * 1024u * 1024u)
#endif
/* A flip: the title's frame counters advance (pgraph_vk.c). */
void xbox_Nv2aFrameCounterFlip(void);
#endif
