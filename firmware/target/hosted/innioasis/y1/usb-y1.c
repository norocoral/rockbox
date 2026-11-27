/***************************************************************************
 *             __________               __   ___
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2026 Nugget Y1
 * USB management for minimal Linux on Innioasis Y1 (MT6572) via liby1
 ****************************************************************************/

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "config.h"
#include "disk.h"
#include "usb.h"
#include "power.h"
#include "usb-y1.h"
#include "liby1.h"

static bool y1_charge_only = false;
static volatile bool g_ipod_converting = false;
static volatile bool g_tc_reload_needed = false;
static bool g_attached_since_post_sync = false;
static char g_convert_stage[96] = {0};
static int g_convert_done = 0;
static int g_convert_total = 0;

bool y1_usb_is_converting(char *status_buf, size_t buf_sz)
{
    if (!g_ipod_converting)
        return false;

    if (status_buf && buf_sz > 0) {
        if (g_convert_total > 0 && g_convert_stage[0])
            snprintf(status_buf, buf_sz, "%s (%d/%d)",
                     g_convert_stage, g_convert_done, g_convert_total);
        else if (g_convert_stage[0])
            snprintf(status_buf, buf_sz, "%s", g_convert_stage);
        else
            snprintf(status_buf, buf_sz, "Syncing iPod library...");
    }
    return true;
}

bool y1_usb_take_tc_reload_needed(void)
{
    bool needed = g_tc_reload_needed;
    g_tc_reload_needed = false;
    return needed;
}

static bool y1_is_serial_terminal_session(void)
{
    if (y1_usb_is_serial_active())
        return true;

    const char *tty = ttyname(STDIN_FILENO);
    if (tty && strstr(tty, "ttyGS") != NULL)
        return true;

    return false;
}

static void attach_with_pre_sync(void)
{
    if (y1_is_serial_terminal_session())
        return;

    if (y1_usb_is_storage_attached())
        return;

    if (y1_usb_get_ipod_mode()) {
        y1_ipod_ensure_device_files();
        y1_ipod_native_invalidate();
    }

    if (y1_usb_attach_mass_storage())
        g_attached_since_post_sync = true;
}

static void run_post_disconnect_once(void)
{
    y1_usb_sync_clock_from_sdcard();

    if (!g_attached_since_post_sync)
        return;
    g_attached_since_post_sync = false;

    if (!y1_usb_get_ipod_mode())
        return;

    y1_ipod_ensure_device_files();
    y1_ipod_native_invalidate();
    g_tc_reload_needed = true;
}

bool y1_is_charge_only_mode(void)
{
    return y1_charge_only || y1_is_serial_terminal_session();
}

void y1_usb_attach_storage(void)
{
    attach_with_pre_sync();
}

void y1_usb_eject_to_charge_only(void)
{
    y1_charge_only = true;
    if (!y1_is_serial_terminal_session()) {
        y1_usb_detach_mass_storage();
        run_post_disconnect_once();
    }
    sync();
    usb_release_exclusive_storage();
}

int usb_detect(void)
{
    static long last_usb_check_tick = 0;
    static bool cached_present = false;
    extern volatile long current_tick;
    extern void backlight_on(void);
    extern void buttonlight_on(void);
    extern void reset_poweroff_timer(void);

    if (y1_is_serial_terminal_session())
        return USB_EXTRACTED;

    long now = current_tick;
    long interval = HZ / 4; /* 250ms: fast wake & mount even from sleep */

    if (last_usb_check_tick == 0 || !TIME_BEFORE(now, last_usb_check_tick + interval))
    {
        bool prev_present = cached_present;
        last_usb_check_tick = now ? now : 1;
        cached_present = y1_power_usb_data_online();

        /* Wake screen & grab wakelock immediately on USB insertion edge so
         * kernel autosleep cannot re-suspend during USB_DEBOUNCE_POLL. */
        if (!prev_present && cached_present)
        {
            y1_power_set_idle_sleep(false);
            if (y1_display_is_early_suspended())
            {
                backlight_on();
                buttonlight_on();
                reset_poweroff_timer();
            }
        }
    }

    if (!cached_present)
    {
        y1_charge_only = false;
        return USB_EXTRACTED;
    }

    if (y1_charge_only)
        return USB_EXTRACTED;

    return USB_INSERTED;
}

void usb_enable(bool on)
{
    if (y1_is_serial_terminal_session())
        return;

    /* Attaching storage is deferred until disk_unmount_all() so Rockbox
       threads (including tagcache) flush and pause before PC mounts LUN0. */
    if (!on)
        y1_usb_detach_mass_storage();
}

void usb_init_device(void)
{
    if (y1_is_serial_terminal_session())
        return;

    if (usb_detect() == USB_INSERTED)
        attach_with_pre_sync();
    else
        usb_enable(false);
}

int disk_mount_all(void)
{
    sync();
    if (!y1_is_serial_terminal_session())
        run_post_disconnect_once();
    return 1;
}

int disk_unmount_all(void)
{
    attach_with_pre_sync();
    return 1;
}
