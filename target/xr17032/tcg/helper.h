/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2026 monkuous
 */

DEF_HELPER_2(raise_exception, noreturn, env, i32)

#ifndef CONFIG_USER_ONLY
/* CRs helper */
DEF_HELPER_2(crwr_itbpte, void, env, tl)
DEF_HELPER_2(crwr_dtbpte, void, env, tl)
DEF_HELPER_2(crwr_itbctrl, void, env, tl)
DEF_HELPER_2(crwr_dtbctrl, void, env, tl)
/*DEF_HELPER_1(csrrd_pgd, i64, env)
DEF_HELPER_1(csrrd_cpuid, i64, env)
DEF_HELPER_1(csrrd_tval, i64, env)
DEF_HELPER_1(csrrd_msgir, i64, env)
DEF_HELPER_2(csrwr_stlbps, i64, env, tl)
DEF_HELPER_2(csrwr_estat, i64, env, tl)
DEF_HELPER_2(csrwr_asid, i64, env, tl)
DEF_HELPER_2(csrwr_tcfg, i64, env, tl)
DEF_HELPER_2(csrwr_ticlr, i64, env, tl)
DEF_HELPER_2(csrwr_pwcl, i64, env, tl)
DEF_HELPER_2(csrwr_pwch, i64, env, tl)
DEF_HELPER_2(iocsrrd_b, i64, env, tl)
DEF_HELPER_2(iocsrrd_h, i64, env, tl)
DEF_HELPER_2(iocsrrd_w, i64, env, tl)
DEF_HELPER_2(iocsrrd_d, i64, env, tl)
DEF_HELPER_3(iocsrwr_b, void, env, tl, tl)
DEF_HELPER_3(iocsrwr_h, void, env, tl, tl)
DEF_HELPER_3(iocsrwr_w, void, env, tl, tl)
DEF_HELPER_3(iocsrwr_d, void, env, tl, tl)*/

/* TLB helper */
/*DEF_HELPER_1(tlbwr, void, env)
DEF_HELPER_1(tlbfill, void, env)
DEF_HELPER_1(tlbsrch, void, env)
DEF_HELPER_1(tlbrd, void, env)
DEF_HELPER_1(tlbclr, void, env)
DEF_HELPER_1(tlbflush, void, env)
DEF_HELPER_1(invtlb_all, void, env)
DEF_HELPER_2(invtlb_all_g, void, env, i32)
DEF_HELPER_2(invtlb_all_asid, void, env, tl)
DEF_HELPER_3(invtlb_page_asid, void, env, tl, tl)
DEF_HELPER_3(invtlb_page_asid_or_g, void, env, tl, tl)*/

DEF_HELPER_1(rfe, void, env)
DEF_HELPER_1(hlt, void, env)
#endif
