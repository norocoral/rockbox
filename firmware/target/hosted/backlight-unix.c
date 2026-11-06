/***************************************************************************
 *             __________               __   ___
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2017 Marcin Bukat
 * Copyright (C) 2019 by Roman Stolyarov
 * Copyright (C) 2025 by Melissa Autumn
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/
#define RB_FILESYSTEM_OS
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include "config.h"
#include "backlight.h"
#include "backlight-target.h"
#include "sysfs.h"
#include "panic.h"
#include "lcd.h"
#include "kernel.h"
#include "misc.h"
#include "settings.h"
#if defined(INNIOASIS_Y1)
#include "innioasis/y1/y1-log.h"
#include "innioasis/y1/button-target.h"
#endif

#if defined(BACKLIGHT_RG_NANO)
static const char * const sysfs_bl_brightness =
    "/sys/class/backlight/backlight/brightness";

static const char * const sysfs_bl_power =
    "/sys/class/backlight/backlight/bl_power";
#elif defined(BACKLIGHT_HIBY)
static const char * const sysfs_bl_brightness =
    "/sys/class/backlight/backlight_pwm0/brightness";

static const char * const sysfs_bl_power =
    /* Framebuffer powers off both touch (if available) and screen */
    "/sys/class/graphics/fb0/blank";

#elif defined(BACKLIGHT_INNIOASIS)
static const char * const sysfs_bl_brightness =
    "/sys/class/leds/lcd-backlight/brightness";

static const char * const sysfs_bl_power = NULL;
#else
static const char * const sysfs_bl_brightness =
    "/sys/class/backlight/pwm-backlight.0/brightness";

static const char * const sysfs_bl_power =
    "/sys/class/backlight/pwm-backlight.0/bl_power";
#endif

static int current_brightness = DEFAULT_BRIGHTNESS_SETTING;

bool backlight_hw_init(void)
{
    current_brightness = DEFAULT_BRIGHTNESS_SETTING;
    backlight_hw_on();
    backlight_hw_brightness(DEFAULT_BRIGHTNESS_SETTING);
#ifdef HAVE_BUTTON_LIGHT
    buttonlight_hw_on();
#ifdef HAVE_BUTTONLIGHT_BRIGHTNESS
    buttonlight_hw_brightness(DEFAULT_BRIGHTNESS_SETTING);
#endif
#endif
    return true;
}

static int last_bl = -1;

/* Ref: https://www.kernel.org/doc/html/latest/gpu/backlight.html#c.backlight_properties */
#define BACKLIGHT_POWER_ON 0
#define BACKLIGHT_POWER_REDUCED 1
#define BACKLIGHT_POWER_OFF 4

#if defined(BACKLIGHT_INNIOASIS)
#include "cpufreq-y1.h"
#include "lcd-target.h"
#include "innioasis/y1/bt-y1.h"
#include "innioasis/y1/y1-log.h"
#include "action.h"
#include "liby1.h"

static void y1_enter_kernel_early_suspend(void)
{
    y1_display_enter_early_suspend();
}

static void y1_exit_kernel_early_suspend(void)
{
    y1_display_exit_early_suspend();
}

static void write_bl_hw(int val)
{
    y1_backlight_set_raw(val);
}

int y1_get_effective_brightness(void)
{
    if (is_keys_locked() && global_settings.backlight_on_button_hold >= 3) {
        static const int hold_brightness_table[] = {
            0, 0, 0, 1, 5, 10, 15, 20, 25, 30, 40, 50, 60, 70, 80, 90, 100
        };
        int idx = global_settings.backlight_on_button_hold;
        if (idx < (int)(sizeof(hold_brightness_table)/sizeof(hold_brightness_table[0])))
            return hold_brightness_table[idx];
    }
    return current_brightness > 0 ? current_brightness :
           (backlight_brightness > 0 ? backlight_brightness : DEFAULT_BRIGHTNESS_SETTING);
}
#endif

#if defined(BACKLIGHT_INNIOASIS)
bool y1_is_backlight_active(void)
{
    return last_bl == BACKLIGHT_POWER_ON;
}

bool y1_is_screen_sleeping(void)
{
    return last_bl != BACKLIGHT_POWER_ON;
}
#endif

void backlight_hw_on(void)
{
    if (last_bl != BACKLIGHT_POWER_ON) {
        last_bl = BACKLIGHT_POWER_ON;
        if (sysfs_bl_power)
            sysfs_set_int(sysfs_bl_power, last_bl);
#if defined(BACKLIGHT_INNIOASIS)
        y1_exit_kernel_early_suspend();
        y1_set_screen_off_power_mode(false);
#endif
#ifdef HAVE_LCD_ENABLE
        lcd_enable(true);
#endif
#if defined(BACKLIGHT_INNIOASIS)
        int target_br = y1_get_effective_brightness();
        int hw_val = (target_br * 255) / 100;
        write_bl_hw(hw_val);
#endif
    } else {
#ifdef HAVE_LCD_ENABLE
        if (!lcd_active())
            lcd_enable(true);
#endif
#if defined(BACKLIGHT_INNIOASIS)
        int target_br = y1_get_effective_brightness();
        int hw_val = (target_br * 255) / 100;
        write_bl_hw(hw_val);
#endif
    }
}

void backlight_hw_off(void)
{
#if defined(BACKLIGHT_INNIOASIS)
    y1_logf("[backlight-unix] backlight_hw_off() called (last_bl=%d)\n", last_bl);
    if (last_bl != BACKLIGHT_POWER_OFF) {
        last_bl = BACKLIGHT_POWER_OFF;
        write_bl_hw(0);
        if (sysfs_bl_power)
            sysfs_set_int(sysfs_bl_power, last_bl);
    }
#else
    if (last_bl != BACKLIGHT_POWER_REDUCED) {
        last_bl = BACKLIGHT_POWER_REDUCED;
        if (sysfs_bl_power)
            sysfs_set_int(sysfs_bl_power, last_bl);
#ifdef HAVE_LCD_ENABLE
        lcd_enable(false);
#endif
    }
#endif
}

void backlight_hw_brightness(int brightness)
{
    /* cap range, just in case */
    if (brightness > MAX_BRIGHTNESS_SETTING)
        brightness = MAX_BRIGHTNESS_SETTING;
    if (brightness < 0)
        brightness = 0;

    if (brightness > 0)
        current_brightness = brightness;

#if defined(BACKLIGHT_INNIOASIS)
    if (last_bl == BACKLIGHT_POWER_ON || last_bl == -1) {
        int eff = y1_get_effective_brightness();
        int hw_val = (eff * 255) / 100;
        write_bl_hw(hw_val);
    }
#else
    sysfs_set_int(sysfs_bl_brightness, brightness);
#endif
}

#ifdef HAVE_LCD_SLEEP
#if defined(BACKLIGHT_INNIOASIS)
void lcd_awake(void)
{
    y1_logf("[backlight-unix] lcd_awake() called\n");
    y1_exit_kernel_early_suspend();
    y1_set_screen_off_power_mode(false);
#ifdef HAVE_LCD_ENABLE
    lcd_enable(true);
#endif
}

void lcd_sleep(void)
{
    y1_logf("[backlight-unix] lcd_sleep() called (last_bl=%d)\n", last_bl);
#ifdef HAVE_LCD_ENABLE
    y1_logf("[backlight-unix] lcd_sleep: calling lcd_enable(false)\n");
    lcd_enable(false);
#endif

    y1_set_screen_off_power_mode(true);

    if (!y1_display_is_early_suspended())
        y1_enter_kernel_early_suspend();
}
#else
void lcd_awake(void)
{
    /* Nothing to do */
}

void lcd_sleep(void)
{
    if (last_bl != BACKLIGHT_POWER_OFF) {
        last_bl = BACKLIGHT_POWER_OFF;
        sysfs_set_int(sysfs_bl_power, last_bl);
#ifdef HAVE_LCD_ENABLE
        lcd_enable(false);
#endif
    }
}
#endif
#endif
