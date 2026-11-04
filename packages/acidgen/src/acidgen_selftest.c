/* SPDX-License-Identifier: GPL-3.0-only
 * acidgen: a self-test to run on the target (or anywhere): hashes of fixed lines and MUTATEs over many settings,
 * seeds and lengths. Equal to ACID_SELFTEST_TB3PO / ACID_SELFTEST_AICD: the port makes the same lines as Felucca and
 * X0X on this compiler and CPU (a wrong integer width, a signed shift, an optimiser's liberty would show). No stdio,
 * no heap; ~1.2 KB of stack, ~5 M instructions (host: tens of ms on a 100 MHz core). Optional: leave this file out of
 * a release build. */
#include "acidgen.h"

#define ACID_SELFTEST_TB3PO 0xCB4CC370u   /* (Felucca's tb3po.c: the same lines; test/acidgen_test.c) */
#define ACID_SELFTEST_AICD 0x99FE18ACu    /* (Felucca's tests/tb3po_test.c: the same lines) */

static uint32_t acid_line_hash(const acid_step_t *b, uint32_t len)
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < len; i++)
        h = (h ^ (b[i].note | (uint32_t)b[i].flags << 8)) * 16777619u;
    return h;
}

uint32_t acid_selftest_tb3po(void)   /* 64 settings x a line and a MUTATE */
{
    uint32_t i, h = 2166136261u;
    for (i = 0; i < 64u; i++) {
        acid_step_t l[ACID_NSTEP];
        acid_cfg_t c;
        uint32_t rr = i * 2654435761u;
        acid_defaults(&c, 0xB0A7u + i * 11u);
        c.density = (uint8_t)(i * 7u % 101u);
        c.accent = (uint8_t)(i * 13u % 101u);
        c.slide = (uint8_t)(i * 17u % 101u);
        c.oct_range = (uint8_t)(1u + i % 3u);
        c.root = (uint8_t)(i % 12u);
        c.scale = (uint8_t)(i % ACID_NSCALES);
        c.base_oct = (uint8_t)(i % 5u);
        acid_tb3po_generate(&c, l, 1u + i);
        h = (h ^ acid_line_hash(l, ACID_NSTEP)) * 16777619u;
        acid_tb3po_mutate(&c, l, 1u + i, &rr);
        h = (h ^ acid_line_hash(l, ACID_NSTEP) ^ rr) * 16777619u;
    }
    return h;
}

#if ACIDGEN_AICD
uint32_t acid_selftest_aicd(void)    /* 64 settings x a line and a MUTATE */
{
    uint32_t i, h = 2166136261u;
    for (i = 0; i < 64u; i++) {
        acid_step_t l[ACID_NSTEP];
        acid_cfg_t c;
        uint32_t rr = i * 2654435761u;
        acid_defaults(&c, 0x51EDu + i * 7u);
        c.mode = ACID_MODE_AICD;
        c.temp = (uint8_t)(10u + i * 3u);
        c.a_dens = (uint8_t)(i * 5u % 101u);
        c.a_acc = (uint8_t)(i * 11u % 101u);
        c.a_sld = (uint8_t)(i * 13u % 101u);
        c.a_scale = (uint8_t)(i % 7u);
        c.root = (uint8_t)(i % 12u);
        c.base_oct = (uint8_t)(i % 5u);
        acid_aicd_generate(&c, l, 1u + i);
        h = (h ^ acid_line_hash(l, ACID_NSTEP)) * 16777619u;
        acid_aicd_mutate(&c, l, 1u + i, &rr);
        h = (h ^ acid_line_hash(l, ACID_NSTEP) ^ rr) * 16777619u;
    }
    return h;
}
#endif

int acid_selftest(void)              /* 1: both as they should be */
{
    return acid_selftest_tb3po() == ACID_SELFTEST_TB3PO
#if ACIDGEN_AICD
           && acid_selftest_aicd() == ACID_SELFTEST_AICD
#endif
        ;
}
