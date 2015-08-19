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

#ifndef TIME_INTERNAL_H__
#define TIME_INTERNAL_H__

#ifndef TIME_INTERNAL_DEBUG
#if     ERTS_MULTI_TIW
#ifdef  DEBUG
#define TIME_INTERNAL_DEBUG 1
#endif
#endif
#endif
#ifndef TIME_INTERNAL_DEBUG
#define TIME_INTERNAL_DEBUG 0
#endif

/*
    erts_short_time_t is 32 bits
    need to be able to manipulate values as signed and unsigned
    keep all this together because it's size-dependent
*/
typedef Sint32  s_short_time_t;
typedef Uint32  u_short_time_t;
#define INVALID_S_SHORT_TIME    -1
#define INVALID_U_SHORT_TIME    ((u_short_time_t) INVALID_S_SHORT_TIME)
#define INVALID_ERTS_SHORT_TIME ((erts_short_time_t) INVALID_S_SHORT_TIME)

/*
    private time.c API for erl_time_sup.c

    returns:
        0:
            there's a timer due for processing right now
        0 < Ticks <= ERTS_SHORT_TIME_T_MAX:
            the next timer is due for processing in 'Ticks' ticks
        INVALID_ERTS_SHORT_TIME:
            there are no timers due for processing
*/
erts_short_time_t erts_next_time(void);

/*
    private erl_time_sup.c API for time.c

    TODO: check whether this is used at all
*/
extern erts_smp_atomic_t * last_delivered_ms_p;

ERTS_GLB_INLINE Uint64 erts_get_timer_time(void);

#if ERTS_GLB_INLINE_INCL_FUNC_DEF

ERTS_GLB_INLINE Uint64 erts_get_timer_time (void)
{
    return erts_smp_atomic_read_nob(last_delivered_ms_p);
}

#endif  /* ERTS_GLB_INLINE_INCL_FUNC_DEF */

#if TIME_INTERNAL_DEBUG
#define DBG_NL()    erts_printf("\n")
#define DBG_LOC()   erts_printf("%s:%u\n", __FILE__, __LINE__)
#define DBG_UINT(V) erts_printf("%s:%u %s = %u\n", __FILE__, __LINE__, #V, V)
#define DBG_SINT(V) erts_printf("%s:%u %s = %u\n", __FILE__, __LINE__, #V, V)
#define DBG_MSG(S)  erts_printf("%s:%u %s\n", __FILE__, __LINE__, S)
#define DBG_PTR(P)  erts_printf("%s:%u %s = %p\n", __FILE__, __LINE__, #P, P)
#define DBG_FMT(F,...)  erts_printf("%s:%u " F "\n", __FILE__, __LINE__, __VA_ARGS__)
#else
#define DBG_NL()
#define DBG_LOC()
#define DBG_UINT(V)
#define DBG_SINT(V)
#define DBG_MSG(S)
#define DBG_PTR(P)
#define DBG_FMT(F,...)
#endif

#endif  /* TIME_INTERNAL_H__ */
