/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Innioasis Y1 CPU Clock and Governor Control (MediaTek MT6572)
 * Uses liby1 hardware abstraction layer.
 ****************************************************************************/

#include <stdbool.h>
#include "config.h"
#include "cpufreq-y1.h"
#include "settings.h"
#include "bt-y1.h"
#include "y1-log.h"
#include "liby1.h"

static bool _boot_boost_active = true;

void y1_boot_boost_finish(void)
{
    if (_boot_boost_active) {
        _boot_boost_active = false;
        y1_logf("[cpufreq-y1] Rockbox fully loaded; applying user CPU preferences (mode=%d, cores=%d)\n",
                global_settings.y1_cpu_mode, global_settings.y1_cpu_cores);
        y1_set_cpu_mode(global_settings.y1_cpu_mode);
        y1_set_cpu_cores_setting(global_settings.y1_cpu_cores);
    }
}

void y1_set_screen_off_power_mode(bool screen_off)
{
    if (screen_off) {
        if (!global_settings.y1_screen_off_cpu_save)
            return;
        if (y1_bt_is_receiver_mode())
            return;
    }
    y1_cpu_set_screen_off_clamp(screen_off);
}

void y1_set_cpu_mode(int mode)
{
    if (_boot_boost_active)
        return;
    y1_cpu_set_mode(mode);
}

int y1_get_current_freq(void)
{
    return y1_cpu_get_freq_mhz();
}

void y1_set_cpu_cores_setting(int mode)
{
    if (_boot_boost_active)
        return;
    /* mode 0: Dual-Core, mode 1: Single-Core */
    y1_cpu_set_dual_core(mode == 0);
}
