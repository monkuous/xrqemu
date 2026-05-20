/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2026 monkuous
 */

DEF_HELPER_2(raise_exception, noreturn, env, i32)

#ifndef CONFIG_USER_ONLY
/* CRs helper */
DEF_HELPER_2(crwr_itbpte, void, env, tl)
DEF_HELPER_2(crwr_dtbpte, void, env, tl)
DEF_HELPER_2(crwr_itbindex, void, env, tl)
DEF_HELPER_2(crwr_dtbindex, void, env, tl)
DEF_HELPER_2(crwr_itbctrl, void, env, tl)
DEF_HELPER_2(crwr_dtbctrl, void, env, tl)

DEF_HELPER_1(rfe, void, env)
DEF_HELPER_1(hlt, void, env)
#endif
