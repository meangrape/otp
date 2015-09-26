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
 *
 * It is included directly in erl_time_sup.c, and has access to static
 * declarations in that file. By convention, only static symbols are declared
 * here, and all such symbols at file scope include the moniker
 * 'ttod_<strategy>', where '<strategy>' matches 'ttod_impl_<strategy>' in
 * the file name.
 *
 * On entry, the macro HAVE_TTOD_<STRATEGY> is defined with the value '0'.
 * If the necessary resources are available to implement the strategy, this
 * macro should be defined to exactly '1' after inclusion of this file, and
 * 'init_ttod_<strategy>(char ** name)' should be a valid statement resolving
 * to a pointer to a get_ttod_f function on success or NULL if initialization
 * was not successful.
 *
 * If the strategy cannot (or should not) be used in the compilation
 * environment, no code should be included and the HAVE_TTOD_<STRATEGY>
 * macro should have a zero value after inclusion of this file.
 */

#if SOME_PREPROCESSOR_DEFINITIONS
#undef  HAVE_TTOD_HPET
#define HAVE_TTOD_HPET 1
#endif  /* requirements check */

#if HAVE_TTOD_HPET

/*
 * Allocate locally as needed
 */
static struct
{
    int foo;
}
    ttod_hpet_state;

/*
 * Return the number of microseconds since 1-Jan-1970 UTC on success or
 * get_ttod_fail(get_ttod_hpet) to disable this strategy.
 *
 * Each implementation is responsible for figuring out when it has failed
 * permanently, but should not blindly continue trying when it's clear it's
 * just not working.
 */
static u_microsecs_t get_ttod_hpet(void)
{
    SysTimeval  tod;

    /* EVERY implementation MUST do this! */
    if (erts_tolerant_timeofday.disable)
        return  gettimeofday_us();

    sys_gettimeofday(& tod);

    return  get_ttod_fail(get_ttod_hpet);
}

/*
 * Return a ttod function to indicate successful initialization.
 * Even if NULL is returned, the value in the '*name' output variable MAY
 * be used for a status message, so it MUST be initialized.
 *
 * This function should check the runtime environment to ensure support for
 * the strategy and only initialize if the necessary behavior is present.
 */
static get_ttod_f init_ttod_hpet(const char ** name)
{
    /* MUST be initialized before ANY return */
    *name = "HPET";

    return  get_ttod_hpet;
}

#endif  /* HAVE_TTOD_HPET */
