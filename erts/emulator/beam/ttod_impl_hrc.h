/*
 * %CopyrightBegin%
 *
 * Copyright Basho Technologies, Inc 2015. All Rights Reserved.
 *
 * The contents of this file are subject to the Erlang Public License,
 * Version 1.1, (the "License"); you may not use this file except in
 * compliance with the License. You should have received a copy of the
 * Erlang Public License along with this software. If not, it can be
 * retrieved online at http://www.erlang.org/.
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and limitations
 * under the License.
 *
 * %CopyrightEnd%
 */

/*
 * This file implements ONE Tolerant Time Of Day (TTOD) strategy.
 * It is included twice in erl_time_sup.c:
 *
 * First, it's included with ERTS_TTOD_IMPL_CHK defined to a non-zero value.
 * If the macro ERTS_TTOD_USE_<STRATEGY> is defined with a non-zero value AND
 * the necessary resources are available to implement the strategy, that macro
 * should be defined to exactly '1' after inclusion of this file, and any of
 * the ERTS_TTOD_IMPL_NEED_xxx macros (refer to erl_time_sup.c to see which
 * ones are available) needed for compilation should be defined to '1'.
 * During this inclusion, absolutely NO code should be emitted!
 *
 * If the strategy cannot (or should not) be used in the compilation
 * environment, the ERTS_TTOD_USE_<STRATEGY> macro should have a zero value
 * after the first inclusion of this file.
 *
 * Second, it's included with ERTS_TTOD_IMPL_CHK undefined, and if the
 * ERTS_TTOD_USE_<STRATEGY> macro is defined with a non-zero value then the
 * implementation of the strategy should be included, and the code has access
 * to the static declarations indicated by the ERTS_TTOD_IMPL_NEED_xxx macros.
 *
 * If any implementation code is included, 'init_ttod_<strategy>(char ** name)'
 * MUST be a valid statement resolving to a pointer to a get_ttod_f function
 * on success or NULL if initialization was not successful.
 *
 * By convention, only static symbols are declared here, and all such symbols
 * at file scope include the moniker 'ttod_<strategy>' in their name, where
 * '<strategy>' matches 'ttod_impl_<strategy>' in the file name.
 */

#if ERTS_TTOD_IMPL_CHK

#if     ERTS_TTOD_USE_HRC \
    &&  SOME_PREPROCESSOR_DEFINITIONS
#undef  ERTS_TTOD_USE_HRC
#define ERTS_TTOD_USE_HRC 1
#define ERTS_TTOD_IMPL_NEED_GET_TTOD_FAIL 1
#else
#undef  ERTS_TTOD_USE_HRC
#define ERTS_TTOD_USE_HRC 0
#endif  /* requirements check */

#elif   ERTS_TTOD_USE_HRC

/*
 * Allocate locally as needed
 */
static struct
{
    int foo;
}
    ttod_hrc_state;

/*
 * Return the number of microseconds since 1-Jan-1970 UTC on success or
 * get_ttod_fail(get_ttod_hrc) to disable this strategy.
 *
 * Each implementation is responsible for figuring out when it has failed
 * permanently, but should not blindly continue trying when it's clear it's
 * just not working.
 */
static u_microsecs_t get_ttod_hrc(void)
{
    SysTimeval  tod;

    /* EVERY implementation MUST do this! */
    if (erts_tolerant_timeofday.disable)
        return  gettimeofday_us();

    sys_gettimeofday(& tod);

    return  get_ttod_fail(get_ttod_hrc);
}

/*
 * Return a ttod function to indicate successful initialization.
 * Even if NULL is returned, the value in the '*name' output variable MAY
 * be used for a status message, so it MUST be initialized.
 *
 * This function should check the runtime environment to ensure support for
 * the strategy and only initialize if the necessary behavior is present.
 */
static get_ttod_f init_ttod_hrc(const char ** name)
{
    /* MUST be initialized before ANY return */
    *name = "hrc";

    return  get_ttod_hrc;
}

#endif  /* ERTS_TTOD_USE_HRC */
