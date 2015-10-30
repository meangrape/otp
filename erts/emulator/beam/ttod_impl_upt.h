/*
 * %CopyrightBegin%
 *
 * Copyright Basho Technologies, Inc 2015. All Rights Reserved.
 * Copyright Ericsson AB 1999-2012. All Rights Reserved.
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

#if     ERTS_TTOD_USE_UPT \
    &&  defined(CORRECT_USING_TIMES) && 0   /* disabled for now */
#undef  ERTS_TTOD_USE_UPT
#define ERTS_TTOD_USE_UPT   1
#define ERTS_TTOD_IMPL_NEED_GET_TTOD_FAIL 1
#else
#undef  ERTS_TTOD_USE_UPT
#define ERTS_TTOD_USE_UPT   0
#endif  /* requirements check */

#elif   ERTS_TTOD_USE_UPT

static clock_t          ttod_upt_init_ct;
static clock_t          ttod_upt_last_ct;
static Sint64           ttod_upt_ct_wrap;
static s_millisecs_t    ttod_upt_corr_supress;
static s_millisecs_t    ttod_upt_last_ct_diff;
static s_millisecs_t    ttod_upt_last_cc;

/*
  sys_times() might need to be wrapped and the values shifted (right)
  a bit to cope with newer linux (2.5.*) kernels, this has to be taken care
  of dynamically to start with, a special version that uses
  the times() return value as a high resolution timer can be made
  to fully utilize the faster ticks, like on windows, but for now, we'll
  settle with this silly workaround
 */
static CPU_FORCE_INLINE clock_t sys_kernel_ticks(void)
{
#define KERNEL_TICKS_MASK   ((1uLL << ((sizeof(clock_t) * 8) - 1)) - 1)
#ifdef  ERTS_WRAP_SYS_TIMES
    return  (sys_times_wrap() & KERNEL_TICKS_MASK);
#else
    SysTimes    buf[1];
    return  (sys_times(buf) & KERNEL_TICKS_MASK);
#endif  /* ERTS_WRAP_SYS_TIMES */
#undef  KERNEL_TICKS_MASK
}

/*
 * Return the number of microseconds since 1-Jan-1970 UTC on success or
 * get_ttod_fail(get_ttod_times) to disable this strategy.
 *
 * Each implementation is responsible for figuring out when it has failed
 * permanently, but should not blindly continue trying when it's clear it's
 * just not working.
 */
static u_microsecs_t get_ttod_upt(void)
{
    s_millisecs_t   ct_diff;
    s_millisecs_t   tv_diff;
    s_millisecs_t   cur_corr;
    s_millisecs_t   act_corr;   /* long shown to be too small */
    s_millisecs_t   max_adjust;
    s_millisecs_t   curr_ms;
    clock_t         curr_ct;
    SysTimeval      tod_buf[1];

#ifdef ERTS_WRAP_SYS_TIMES
#define TICK_MS (1000 / SYS_CLK_TCK_WRAP)
#else
#define TICK_MS (1000 / SYS_CLK_TCK)
#endif

    /* EVERY implementation MUST do this! */
    if (erts_tolerant_timeofday.disable)
        return  gettimeofday_us();

    sys_gettimeofday(tod_buf);
    curr_ct = sys_kernel_ticks();
    curr_ms = s_get_tv_millis(tod_buf);

    /* I dont know if uptime can move some units backwards
       on some systems, but I allow for small backward
       jumps to avoid such problems if they exist...*/
    if (ttod_upt_last_ct > 100 && curr_ct < (ttod_upt_last_ct - 100))
    {
        ttod_upt_ct_wrap += ((Sint64) 1) << ((sizeof(clock_t) * 8) - 1);
    }
    ttod_upt_last_ct = curr_ct;
    ct_diff = ((ttod_upt_ct_wrap + curr_ct) - ttod_upt_init_ct) * TICK_MS;

    /*
     * We will adjust the time in milliseconds and we allow for 1%
     * adjustments, but if this function is called more often then every 100
     * millisecond (which is obviously possible), we will never adjust, so
     * we accumulate small times by setting ttod_upt_last_ct_diff iff max_adjust > 0
     */
    if ((max_adjust = (ct_diff - ttod_upt_last_ct_diff) / 100) > 0)
        ttod_upt_last_ct_diff = ct_diff;

    tv_diff = (curr_ms - ts_data->init_ms);

    cur_corr = ((ct_diff - tv_diff) / TICK_MS) * TICK_MS; /* trunc */

    /*
     * We allow the cur_corr value to wobble a little, as it
     * suffers from the low resolution of the kernel ticks.
     * if it hasn't changed more than one tick in either direction,
     * we will keep the old value.
     */
    if ((ttod_upt_last_cc > (cur_corr + TICK_MS))
    ||  (ttod_upt_last_cc < (cur_corr - TICK_MS)))
        ttod_upt_last_cc = cur_corr;
    else
        cur_corr = ttod_upt_last_cc;

    /*
     * As time goes, we try to get the actual correction to 0,
     * that is, make erlangs time correspond to the systems dito.
     * The act correction is what we seem to need (cur_corr)
     * minus the correction suppression. The correction supression
     * will change slowly (max 1% of elapsed time) but in millisecond steps.
     */
    act_corr = (cur_corr - ttod_upt_corr_supress);
    if (max_adjust > 0)
    {
        /*
         * Here we slowly adjust erlangs time to correspond with the
         * system time by changing the ttod_upt_corr_supress variable.
         * It can change max_adjust milliseconds which is 1% of elapsed time
         */
        if (act_corr > 0)
        {
            if ((cur_corr - ttod_upt_corr_supress) > max_adjust)
                ttod_upt_corr_supress += max_adjust;
            else
                ttod_upt_corr_supress = cur_corr;
            act_corr = (cur_corr - ttod_upt_corr_supress);
        }
        else if (act_corr < 0)
        {
            if ((ttod_upt_corr_supress - cur_corr) > max_adjust)
                ttod_upt_corr_supress -= max_adjust;
            else
                ttod_upt_corr_supress = cur_corr;
            act_corr = (cur_corr - ttod_upt_corr_supress);
        }
    }
    /*
     * The actual correction will correct the timeval so that system
     * time warps gets smothed down.
     */
    return  (u_microsecs_t) ((curr_ms + act_corr) * ONE_THOUSAND);

#undef TICK_MS
}

/*
 * Return a ttod function to indicate successful initialization.
 * Even if NULL is returned, the value in the '*name' output variable MAY
 * be used for a status massage, so it MUST be initialized.
 *
 * This function should check the runtime environment to ensure support for
 * the strategy and only initialize if the necessary behavior is present.
 */
static get_ttod_f init_ttod_upt(const char ** name)
{
    /* MUST be initialized before ANY return */
    *name = "upt";

    ttod_upt_last_ct = ttod_upt_init_ct = sys_kernel_ticks();
    ttod_upt_last_cc = 0;
    ttod_upt_ct_wrap = 0;
    ttod_upt_corr_supress = 0;

    return  get_ttod_upt;
}

#endif  /* ERTS_TTOD_USE_UPT */
