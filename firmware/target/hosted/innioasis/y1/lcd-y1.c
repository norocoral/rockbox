/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Innioasis Y1 Native Framebuffer Display Driver (MediaTek MT6572)
 * Uses liby1 hardware abstraction layer.
 ****************************************************************************/

#define RB_FILESYSTEM_OS
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "lcd.h"
#include "lcd-target.h"
#include "backlight-target.h"
#include "kernel.h"
#include "settings.h"
#include "audio.h"
#include "y1-log.h"
#include "bt-y1.h"
#include "liby1.h"

#if LCD_DEPTH == 16
static fb_data navigation_old_frame[LCD_WIDTH * LCD_HEIGHT];
static long navigation_fade_start;
static bool navigation_fade_active;
static unsigned navigation_fade_alpha;
#endif

fb_data *framebuffer = NULL; /* global variable referenced by lcd-memframe / lcd-target.h */

static int y1_resolution_mode = 0; /* 0 = Normal (480x360), 1 = Classic (320x240 stretched) */

void lcd_init_device(void)
{
    if (y1_fb_init() < 0)
        return;

    /* Preserve boot splash in mapped_fb until first Rockbox lcd_update() */
    framebuffer = FBADDR(0, 0);

#ifdef HAVE_LCD_ENABLE
    lcd_set_active(true);
#endif
    y1_log("[lcd-y1] LCD device initialized via liby1.\n");
}

void lcd_enable(bool on)
{
    bool cur_act = lcd_active();
    if (cur_act == on)
        return;

    if (!y1_fb_is_ready())
        return;

#if LCD_DEPTH == 16
    navigation_fade_active = false;
#endif
    lcd_set_active(on);
    if (on) {
        y1_fb_set_eco_mode(false);
        send_event(LCD_EVENT_ACTIVATION, NULL);
        lcd_update();
    } else {
        y1_fb_set_eco_mode(true);
    }
}

bool y1_playback_active(void)
{
    int stat = audio_status();
    if (y1_bt_is_receiver_mode()) {
        uint8_t play_status = y1_bt_receiver_get_play_status();
        return play_status == 1 || play_status == 3 || play_status == 4;
    }
    return (stat & AUDIO_STATUS_PLAY) && !(stat & AUDIO_STATUS_PAUSE);
}

void y1_set_resolution_setting(int mode)
{
    y1_resolution_mode = mode;
}

void lcd_update(void)
{
    if (y1_resolution_mode == 1)
        lcd_update_rect(0, 0, 320, 240);
    else
        lcd_update_rect(0, 0, LCD_WIDTH, LCD_HEIGHT);
}

#if LCD_DEPTH == 16
static long navigation_fade_duration(void)
{
    long duration = (HZ * 30 + 999) / 1000;
    return duration > 0 ? duration : 1;
}

void lcd_navigation_fade_capture(void)
{
    if (!global_settings.navigation_fade)
    {
        navigation_fade_active = false;
        return;
    }
    if (navigation_fade_active || !framebuffer)
        return;

    fb_data *src = FBADDR(0, 0);
    if (!src)
        return;

    memcpy(navigation_old_frame, src, sizeof(navigation_old_frame));
    navigation_fade_start = current_tick;
    navigation_fade_alpha = 0;
    navigation_fade_active = true;
}

static uint16_t navigation_blend_cb(int x, int y, uint16_t new_pixel, void *user)
{
    (void)user;
    uint16_t old_pixel = (uint16_t)navigation_old_frame[y * LCD_WIDTH + x];
    unsigned a = navigation_fade_alpha;
    unsigned inv_a = 256 - a;
    unsigned red = (((old_pixel >> 11) & 0x1f) * inv_a +
                    ((new_pixel >> 11) & 0x1f) * a) >> 8;
    unsigned green = (((old_pixel >> 5) & 0x3f) * inv_a +
                      ((new_pixel >> 5) & 0x3f) * a) >> 8;
    unsigned blue = ((old_pixel & 0x1f) * inv_a +
                     (new_pixel & 0x1f) * a) >> 8;
    return (uint16_t)((red << 11) | (green << 5) | blue);
}
#endif

void lcd_update_rect(int x, int y, int width, int height)
{
    if (!y1_fb_is_ready())
        return;

#ifdef HAVE_LCD_ENABLE
    if (!lcd_active())
        return;
#endif

#if LCD_DEPTH == 16
    bool blend_navigation = navigation_fade_active;
    if (blend_navigation)
    {
        if (!global_settings.navigation_fade)
        {
            navigation_fade_active = false;
            blend_navigation = false;
        }
        else
        {
            long duration = navigation_fade_duration();
            long elapsed = current_tick - navigation_fade_start;
            if (elapsed >= duration)
                navigation_fade_alpha = 256;
            else
                navigation_fade_alpha = (unsigned)(elapsed * 256 / duration);

            x = 0;
            y = 0;
            width = y1_resolution_mode == 1 ? 320 : LCD_WIDTH;
            height = y1_resolution_mode == 1 ? 240 : LCD_HEIGHT;
        }
    }
#else
    const bool blend_navigation = false;
#endif

redraw_navigation_fade: ;

    fb_data *src_base = FBADDR(0, 0);
    if (!src_base)
        return;

#if LCD_DEPTH == 16
    y1_pixel_blend_fn cb = (blend_navigation && navigation_fade_alpha < 256)
                           ? navigation_blend_cb : NULL;
#else
    y1_pixel_blend_fn cb = NULL;
#endif

    if (y1_resolution_mode == 1) {
        y1_fb_blit_rgb565_classic_scaled((const uint16_t *)src_base, LCD_WIDTH,
                                         x, y, width, height, cb, NULL);
    } else {
        y1_fb_blit_rgb565_rect((const uint16_t *)src_base, LCD_WIDTH,
                               x, y, width, height, cb, NULL);
    }

#if LCD_DEPTH == 16
    if (navigation_fade_active)
    {
        sleep(1);
        long duration = navigation_fade_duration();
        long elapsed = current_tick - navigation_fade_start;
        if (elapsed < duration)
            navigation_fade_alpha = (unsigned)(elapsed * 256 / duration);
        else
        {
            navigation_fade_alpha = 256;
            navigation_fade_active = false;
        }
        goto redraw_navigation_fade;
    }
#endif
}

#ifdef HAVE_LCD_SHUTDOWN
void lcd_shutdown(void)
{
    y1_fb_close();
}
#endif

bool y1_display_raw_screen(const char *name)
{
    fb_data *fb16 = FBADDR(0, 0);
    return y1_fb_display_raw_screen(name, (uint16_t *)fb16, LCD_WIDTH,
                                    y1_resolution_mode == 1);
}
