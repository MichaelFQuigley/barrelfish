/**
 * \file
 * \brief Simple RTC hardware clock access.
 *
 * This file implements a simple driver for the hardware real time clock.
 */

/*
 * Copyright (c) 2007, 2008, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */

#include <stdio.h>
#include <barrelfish/barrelfish.h>
#include <barrelfish_kpi/x86.h>
#include "rtc.h"
#include "lpc_rtc_dev.h"

/** \brief This function reads the hardware clock.
    This function reads the hardware real time clock and fills the
    passed struct with the read values.
    \param t pointer to a rtc_time struct
*/

void rtc_write_cmos(int addr, uint8_t b)
{
    LPC_rtc_t rtc;
    LPC_rtc_initialize(&rtc,0x00);
    LPC_rtc_ndx_wr(&rtc,addr);
    LPC_rtc_target_wr(&rtc,b);
}

void rtc_write_extended(int addr, uint8_t b)
{
    LPC_rtc_t rtc;
    LPC_rtc_initialize(&rtc,0x00);
    LPC_rtc_endx_wr(&rtc,addr);
    LPC_rtc_etarget_wr(&rtc,b);
}

uint8_t rtc_read_cmos(int addr)
{
    LPC_rtc_t rtc;
    LPC_rtc_initialize(&rtc,0x00);
    LPC_rtc_ndx_wr(&rtc,addr);
    return LPC_rtc_target_rd(&rtc);
}

uint8_t rtc_read_extended(int addr, uint8_t b)
{
    LPC_rtc_t rtc;
    LPC_rtc_initialize(&rtc,0x00);
    LPC_rtc_endx_wr(&rtc,addr);
    return LPC_rtc_etarget_rd(&rtc);
}

static inline uint8_t _rtc_read( LPC_rtc_t *rtc, uint8_t _r) 
{
    LPC_rtc_ndx_wr(rtc,_r);
    return LPC_rtc_target_rd(rtc);
}


void rtc_read(struct rtc_time *t)
{
    uint8_t sec, min, hr;

    LPC_rtc_t rtc;
    LPC_rtc_initialize(&rtc,0x00);

    // read hour
    hr = _rtc_read(&rtc, LPC_rtc_hours );

    // read minutes
    min = _rtc_read(&rtc, LPC_rtc_minutes );

    // read seconds
    sec = _rtc_read(&rtc, LPC_rtc_seconds );

    // Convert in the case of BCD hours
    LPC_rtc_ndx_wr(&rtc, LPC_rtc_regb);
    if ( LPC_rtc_regb_rd(&rtc).dm ) {
        t->hr = hr;
        t->min = min;
        t->sec = sec;
    } else {
        t->hr = (hr / 16) * 10 + hr % 16;
        t->min = (min / 16) * 10 + min % 16;
        t->sec = (sec / 16) * 10 + sec % 16;
    }
}

uint8_t rtc_read_secs(void)
{
    LPC_rtc_t rtc;
    LPC_rtc_initialize(&rtc, 0x00);
    return _rtc_read(&rtc, LPC_rtc_seconds);
}

/** \brief Print current time.
    This function prints the given time
    \param t pointer to a rtc_time struct
*/

void rtc_print(struct rtc_time *t)
{
    printf("%02hhu:%02hhu:%02hhu\n", t->hr, t->min, t->sec);
}
