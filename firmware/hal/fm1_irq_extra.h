/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* This fork's additions to felucca-bsp's chip/fm1_irq.h, for the 303 engine (eng_303.c: X0X's float Open303 in the
 * audio ISR). Until the sync with upstream 1.8.1 they were in hal/fm1_irq.h, which moved into the BSP:
 *
 *   fm1_fpu_traps_off()             after fm1_irq_init (main.c): the FPU's exception traps off
 *   fm1_irq_hold(n) / _release(n)   source n's enable bit off / on, from the next instruction on (csync)
 *   fm1_crash_regs(c, &usp, &ssp, stk)   the last crash's usp / ssp (the BSP's fm1_crash_ctx) and the main loop's
 *                                   stack at usp - 4, usp - 8, usp + 20 (0: usp outside it): what a return there popped
 */
#pragma once
#include <stdint.h>
#include "fm1_irq.h"

/* EMU_CON bits 16..20: the FPU's exception traps, off (as core 1 has them, chip/fm1_cpu1.h, and Melodee its core 0).
 * On, the 303's float DSP crashed on an ordinary IEEE result: an underflow in its drive's tail (x0x/bass303.c
 * drive_block, more often while CUT moves); the IEEE default value is right. Cleared, as the BSP clears the div0 trap,
 * in case a reset kept them from a build that armed them */
static void fm1_fpu_traps_off(void) { FM1_EMU_CON &= ~(0x1Fu << 16); }

/* source n held off / let through again (its ICFG enable bit; the csync: from the next instruction on). From the
 * audio ISR: eng_303.c holds TIMER5 off while the 303's float code runs */
static inline void fm1_irq_hold(uint32_t n)
{
    FM1_ICFG(n) &= ~(1u << ((n & 7u) * 4u));
    __asm__ volatile("csync" ::: "memory");
}
static inline void fm1_irq_release(uint32_t n)
{
    FM1_ICFG(n) |= 1u << ((n & 7u) * 4u);
    __asm__ volatile("csync" ::: "memory");
}

/* the crash record c's usp and ssp (0 when the BSP's fm1_crash_ctx no longer holds it) and three words of the main
 * loop's stack around usp (main.c fm1_fault, the crash screen; read after the fault, before anything runs on it) */
extern char _ustack_lo[], _ustack_top[];      /* app.ld: the main loop's stack */
static void fm1_crash_regs(const fm1_crash_t *c, uint32_t *usp, uint32_t *ssp, uint32_t stk[3])
{
    const fm1_crash_ctx_t *x = &fm1_crash_ctx[c->count % FM1_CRASH_CTX_N];
    uint32_t u = x->count == c->count ? x->usp : 0u;
    int ok = !(u & 3u) && u >= (uint32_t)(uintptr_t)_ustack_lo + 8u && u <= (uint32_t)(uintptr_t)_ustack_top - 24u;
    *usp = u;
    *ssp = x->count == c->count ? x->ssp : 0u;
    stk[0] = ok ? *(volatile uint32_t *)(u - 4u) : 0u;
    stk[1] = ok ? *(volatile uint32_t *)(u - 8u) : 0u;
    stk[2] = ok ? *(volatile uint32_t *)(u + 20u) : 0u;
}
