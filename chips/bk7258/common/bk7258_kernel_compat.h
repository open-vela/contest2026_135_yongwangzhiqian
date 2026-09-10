/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BK7258_KERNEL_COMPAT_H
#define BK7258_KERNEL_COMPAT_H

/* Retained STAR exception/SMP compatibility, not an SDK interception API.
 * No control flow changes: see kernel_compat.json for pinned source inputs
 * and docs/platforms/bk7258/chip-board-wrapper-review-20260910.md for evidence.
 * Reject unreviewed optimizer/nesting semantics instead of silently assuming
 * a linker --wrap remains effective across a changed kernel build.
 */
#if !defined(CONFIG_BK7258_AP_CORE) || defined(CONFIG_BK7258_AP_SMP_SCHED_ONLINE)
#  if !defined(CONFIG_LTO_NONE)
#    error "BK7258 kernel wrappers require reviewed non-LTO code generation"
#  endif
#  if defined(CONFIG_ARCH_HIPRI_INTERRUPT)
#    error "BK7258 kernel wrapper nesting requires review for HIPRI interrupts"
#  endif
#endif

#endif
