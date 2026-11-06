/***************************************************************************
 *             __________               __   ___
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2026 Rockbox Innioasis Y1 Port
 * USB management for minimal Linux on Innioasis Y1 (MT6572) via liby1
 ****************************************************************************/

#include <stdbool.h>
#include <unistd.h>
#include "config.h"
#include "disk.h"
#include "usb.h"
#include "power.h"
#include "usb-y1.h"
#include "liby1.h"

static bool y1_charge_only = false;

bool y1_is_charge_only_mode(void)
{
    return y1_charge_only;
}

void y1_usb_attach_storage(void)
{
    y1_usb_attach_mass_storage();
}

void y1_usb_eject_to_charge_only(void)
{
    y1_charge_only = true;
    y1_usb_detach_mass_storage();
    sync();
    usb_release_exclusive_storage();
}

int usb_detect(void)
{
    static long last_usb_check_tick = 0;
    static bool cached_present = false;
    extern volatile long current_tick;
    extern bool y1_is_screen_sleeping(void);

    long now = current_tick;
    long interval = y1_is_screen_sleeping() ? (2 * HZ) : (HZ / 2);

    if (last_usb_check_tick == 0 || !TIME_BEFORE(now, last_usb_check_tick + interval))
    {
        last_usb_check_tick = now ? now : 1;
        cached_present = y1_power_usb_online();
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
    if (on)
        y1_usb_attach_mass_storage();
    else
        y1_usb_detach_mass_storage();
}

void usb_init_device(void)
{
    if (usb_detect() == USB_INSERTED)
        y1_usb_attach_mass_storage();
    else
        usb_enable(false);
}

int disk_mount_all(void)
{
    sync();
    return 1;
}

int disk_unmount_all(void)
{
    y1_usb_attach_mass_storage();
    return 1;
}
