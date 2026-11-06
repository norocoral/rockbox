/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2026 Rockbox Innioasis Y1 Port
 * MediaTek MT6572 / MT6627 FM Radio Tuner Driver via liby1
 ****************************************************************************/

#define RB_FILESYSTEM_OS
#include "config.h"
#include <stdbool.h>
#include "kernel.h"
#include "tuner.h"
#include "fmradio.h"
#include "tuner-y1.h"
#include "y1-log.h"
#include "liby1.h"

static bool force_mono = false;

void y1_tuner_init(void)
{
    force_mono = false;
}

int y1_tuner_set(int setting, int value)
{
    switch (setting)
    {
        case RADIO_SLEEP:
            return y1_fm_power(value == 0);

        case RADIO_FREQUENCY:
            return y1_fm_tune(value);

        case RADIO_SCAN_FREQUENCY:
            y1_fm_tune(value);
            return y1_fm_is_tuned() ? 1 : 0;

        case RADIO_MUTE:
            return y1_fm_set_mute(value != 0);

        case RADIO_FORCE_MONO:
            force_mono = (value != 0);
            return 0;

        default:
            return -1;
    }
}

int y1_tuner_get(int setting)
{
    switch (setting)
    {
        case RADIO_PRESENT:
            return 1;

        case RADIO_TUNED:
            return y1_fm_is_tuned() ? 1 : 0;

        case RADIO_STEREO:
            return force_mono ? 0 : 1;

        case RADIO_RSSI:
            return y1_fm_get_rssi();

        case RADIO_RSSI_MIN:
            return 0;

        case RADIO_RSSI_MAX:
            return 100;

        default:
            return -1;
    }
}
