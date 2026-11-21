/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2005 by Dave Chapman
 * Copyright (C) 2009 by Karl Kurbjun
 *
 * Rockbox driver for 16-bit colour LCDs
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

#include "config.h"

#include "cpu.h"
#include "lcd.h"
#include "kernel.h"
#include "thread.h"
#include <stdlib.h>
#include "string-extra.h" /* mem*() */
#include "file.h"
#include "debug.h"
#include "system.h"
#include "font.h"
#include "rbunicode.h"
#include "bidi.h"
#include "scroll_engine.h"

// #define LOGF_ENABLE
#include "logf.h"

#define ROW_INC lcd_current_viewport->buffer->stride
#define COL_INC 1

extern lcd_fastpixelfunc_type* const lcd_fastpixelfuncs_backdrop[];
extern lcd_fastpixelfunc_type* const lcd_fastpixelfuncs_bgcolor[];

#ifndef DISABLE_ALPHA_BITMAP
static void ICODE_ATTR lcd_alpha_bitmap_part_mix(const fb_data* image,
                                      const unsigned char *src, int src_x,
                                      int src_y, int x, int y,
                                      int width, int height,
                                      int stride_image, int stride_src);
#endif /* !DISABLE_ALPHA_BITMAP */

#ifdef INNIOASIS_Y1
static bool y1_render_aa_image = true;
static bool y1_render_aa_text = true;
static bool y1_render_transparency_plus = true;

void lcd_set_rendering_options(bool aa_image, bool aa_text, bool trans_plus)
{
    y1_render_aa_image = aa_image;
    y1_render_aa_text = aa_text;
    y1_render_transparency_plus = trans_plus;
}

static inline fb_data y1_rgb565_blend(fb_data bg, fb_data fg, int alpha32)
{
    if (alpha32 <= 0)
        return bg;
    if (alpha32 >= 32)
        return fg;
    unsigned bg_rb = bg & 0xf81f;
    unsigned fg_rb = fg & 0xf81f;
    unsigned bg_g  = bg & 0x07e0;
    unsigned fg_g  = fg & 0x07e0;
    unsigned rb = (bg_rb + (((int)fg_rb - (int)bg_rb) * alpha32 >> 5)) & 0xf81f;
    unsigned g  = (bg_g  + (((int)fg_g  - (int)bg_g)  * alpha32 >> 5)) & 0x07e0;
    return (fb_data)(rb | g);
}

static inline int y1_rgb565_luma(fb_data px)
{
    int r = (px >> 11) & 0x1f;
    int g = (px >> 6) & 0x1f;
    int b = px & 0x1f;
    return r * 2 + g * 5 + b;
}

static inline bool y1_is_transparent_px(fb_data px, bool trans_plus)
{
    if (px == TRANSPARENT_COLOR)
        return true;
    if (!trans_plus)
        return false;
    int r = (px >> 11) & 0x1f;
    int g = (px >> 6) & 0x1f;
    int b = px & 0x1f;
    if (r >= 18 && b >= 18 && g < 16 && r > g + 6 && b > g + 6)
    {
        int m_dist = (31 - r) + (31 - b) + g * 2;
        if (m_dist <= 4)
            return true;
    }
    return false;
}
#endif

#include "lcd-color-common.c"
#include "lcd-bitmap-common.c"
#include "lcd-16bit-common.c"

/*** drawing functions ***/

/* Draw a horizontal line (optimised) */
void lcd_hline(int x1, int x2, int y)
{
    struct viewport *vp = lcd_current_viewport;
    int width;
    unsigned bits = 0;
    enum fill_opt fillopt = OPT_NONE;
    fb_data *dst, *dst_end;

    if (!clip_viewport_hline(vp, &x1, &x2, &y))
        return;

    width = x2 - x1 + 1;

    /* drawmode and optimisation */
    if (vp->drawmode & DRMODE_INVERSEVID)
    {
        if (vp->drawmode & DRMODE_BG)
        {
            if (!lcd_backdrop)
            {
                fillopt = OPT_SET;
                bits = vp->bg_pattern;
            }
            else
                fillopt = OPT_COPY;
        }
    }
    else
    {
        if (vp->drawmode & DRMODE_FG)
        {
            fillopt = OPT_SET;
            bits = vp->fg_pattern;
        }
    }
    if (fillopt == OPT_NONE && vp->drawmode != DRMODE_COMPLEMENT)
        return;

    dst = FBADDR(x1, y);

    switch (fillopt)
    {
      case OPT_SET:
        memset16(dst, bits, width);
        break;

      case OPT_COPY:
        memcpy(dst, PTR_ADD(dst, lcd_backdrop_offset),
               width * sizeof(fb_data));
        break;

      case OPT_NONE:  /* DRMODE_COMPLEMENT */
        dst_end = dst + width;
        do
            *dst = ~(*dst);
        while (++dst < dst_end);
        break;
    }
}

/* Draw a vertical line (optimised) */
void lcd_vline(int x, int y1, int y2)
{
    struct viewport *vp = lcd_current_viewport;
    fb_data *dst, *dst_end;
    int stride_dst;
    lcd_fastpixelfunc_type *pfunc = lcd_fastpixelfuncs[vp->drawmode];

    if (!clip_viewport_vline(vp, &x, &y1, &y2))
        return;

    dst = FBADDR(x, y1);
    stride_dst = vp->buffer->stride;
    dst_end = dst + (y2 - y1) * stride_dst;

    do
    {
        pfunc(dst);
        dst += stride_dst;
    }
    while (dst <= dst_end);
}

/* Draw a partial native bitmap */
void ICODE_ATTR lcd_bitmap_part(const fb_data *src, int src_x, int src_y,
                                int stride, int x, int y, int width,
                                int height)
{
    struct viewport *vp = lcd_current_viewport;
    fb_data *dst;
    int stride_dst;

    if (!clip_viewport_rect(vp, &x, &y, &width, &height, &src_x, &src_y))
        return;

    src += stride * src_y + src_x; /* move starting point */
    dst = FBADDR(x, y);
    stride_dst = vp->buffer->stride;

#ifdef INNIOASIS_Y1
    if (y1_render_aa_image && width >= 3 && height >= 3)
    {
        for (int row = 0; row < height; row++)
        {
            const fb_data *s_row = src + row * stride;
            fb_data *d_row = dst + row * stride_dst;
            if (row == 0 || row == height - 1)
            {
                memcpy(d_row, s_row, width * sizeof(fb_data));
                continue;
            }
            const fb_data *s_up = s_row - stride;
            const fb_data *s_dn = s_row + stride;
            d_row[0] = s_row[0];
            for (int col = 1; col < width - 1; col++)
            {
                fb_data c = s_row[col];
                fb_data u = s_up[col];
                fb_data d = s_dn[col];
                fb_data l = s_row[col - 1];
                fb_data r = s_row[col + 1];
                fb_data nb = c;
                if (u == l && u != c && d != u && r != u)
                    nb = u;
                else if (u == r && u != c && d != u && l != u)
                    nb = u;
                else if (d == l && d != c && u != d && r != d)
                    nb = d;
                else if (d == r && d != c && u != d && l != d)
                    nb = d;

                if (nb != c && abs(y1_rgb565_luma(nb) - y1_rgb565_luma(c)) > 24)
                    d_row[col] = y1_rgb565_blend(c, nb, 8);
                else
                    d_row[col] = c;
            }
            d_row[width - 1] = s_row[width - 1];
        }
        return;
    }
#endif

    do
    {
        memcpy(dst, src, width * sizeof(fb_data));
        src += stride;
        dst += stride_dst;
    }
    while (--height > 0);
}

/* Draw a partial native bitmap with transparency and foreground colors */
void ICODE_ATTR lcd_bitmap_transparent_part(const fb_data *src, int src_x,
                                            int src_y, int stride, int x,
                                            int y, int width, int height)
{
    struct viewport *vp = lcd_current_viewport;
    fb_data *dst;
    unsigned fg = vp->fg_pattern;
    int stride_dst = vp->buffer->stride;

    if (!clip_viewport_rect(vp, &x, &y, &width, &height, &src_x, &src_y))
        return;

    src += stride * src_y + src_x; /* move starting point */
    dst = FBADDR(x, y);

#ifdef INNIOASIS_Y1
    if (y1_render_aa_image || y1_render_transparency_plus)
    {
        bool aa_img = y1_render_aa_image;
        bool trans_p = y1_render_transparency_plus;
        for (int row = 0; row < height; row++)
        {
            const fb_data *src_row = src + row * stride;
            fb_data *dst_row = dst + row * stride_dst;
            for (int col = 0; col < width; col++)
            {
                fb_data px = src_row[col];
                if (px == TRANSPARENT_COLOR)
                    continue;
                if (px == REPLACEWITHFG_COLOR)
                    px = (fb_data)fg;

                int alpha = 32;
                if (trans_p)
                {
                    int r = (px >> 11) & 0x1f;
                    int g = (px >> 6) & 0x1f;
                    int b = px & 0x1f;
                    if (r >= 18 && b >= 18 && g < 16 && r > g + 6 && b > g + 6)
                    {
                        int m_dist = (31 - r) + (31 - b) + g * 2;
                        if (m_dist <= 4)
                            continue;
                        if (m_dist < 24)
                        {
                            alpha = ((m_dist - 4) * 32) / 20;
                            if (alpha < 1) alpha = 1;
                            int dr = ((int)r * 32 - (32 - alpha) * 31) / alpha;
                            int dg = ((int)g * 32) / alpha;
                            int db = ((int)b * 32 - (32 - alpha) * 31) / alpha;
                            if (dr < 0) dr = 0; else if (dr > 31) dr = 31;
                            if (dg < 0) dg = 0; else if (dg > 31) dg = 31;
                            if (db < 0) db = 0; else if (db > 31) db = 31;
                            px = (fb_data)((dr << 11) | (dg << 6) | db);
                        }
                    }
                }

                if (aa_img && alpha == 32)
                {
                    int t_cnt = 0;
                    if (col > 0 && y1_is_transparent_px(src_row[col - 1], trans_p))
                        t_cnt++;
                    if (col + 1 < width && y1_is_transparent_px(src_row[col + 1], trans_p))
                        t_cnt++;
                    if (row > 0 && y1_is_transparent_px(src_row[col - stride], trans_p))
                        t_cnt++;
                    if (row + 1 < height && y1_is_transparent_px(src_row[col + stride], trans_p))
                        t_cnt++;

                    if (t_cnt == 1)
                        alpha = 25;
                    else if (t_cnt == 2)
                        alpha = 18;
                    else if (t_cnt >= 3)
                        alpha = 11;
                    else if (col > 0 && col + 1 < width && row > 0 && row + 1 < height)
                    {
                        fb_data u = src_row[col - stride];
                        fb_data d = src_row[col + stride];
                        fb_data l = src_row[col - 1];
                        fb_data r = src_row[col + 1];
                        fb_data nb = px;
                        if (u == l && u != px && d != u && r != u) nb = u;
                        else if (u == r && u != px && d != u && l != u) nb = u;
                        else if (d == l && d != px && u != d && r != d) nb = d;
                        else if (d == r && d != px && u != d && l != d) nb = d;
                        if (nb != px && abs(y1_rgb565_luma(nb) - y1_rgb565_luma(px)) > 24)
                            px = y1_rgb565_blend(px, nb, 8);
                    }
                }

                dst_row[col] = (alpha >= 32) ? px : y1_rgb565_blend(dst_row[col], px, alpha);
            }
        }
        return;
    }
#endif

#ifdef CPU_ARM
    {
        int w, px;
        asm volatile (
            BEGIN_ARM_ASM_SYNTAX_UNIFIED
        ".rowstart:                              \n"
            "mov     %[w], %[width]              \n" /* Load width for inner loop */
        ".nextpixel:                             \n"
            "ldrh    %[px], [%[s]], #2           \n" /* Load src pixel */
            "add     %[d], %[d], #2              \n" /* Uncoditionally increment dst */
                                                 /* done here for better pipelining */
            "cmp     %[px], %[fgcolor]           \n" /* Compare to foreground color */
            "strheq  %[fgpat], [%[d], #-2]       \n" /* Store foregroud if match */
            "cmpne   %[px], %[transcolor]        \n" /* Compare to transparent color */
            "strhne  %[px], [%[d], #-2]          \n" /* Store dst if not transparent */
            "subs    %[w], %[w], #1              \n" /* Width counter has run down? */
            "bgt     .nextpixel                  \n" /* More in this row? */
            "add     %[s], %[s], %[sstp], lsl #1 \n" /* Skip over to start of next line */
            "add     %[d], %[d], %[dstp], lsl #1 \n"
            "subs    %[h], %[h], #1              \n" /* Height counter has run down? */
            "bgt     .rowstart                   \n" /* More rows? */
            END_ARM_ASM_SYNTAX_UNIFIED
            : [w]"=&r"(w), [h]"+&r"(height), [px]"=&r"(px),
              [s]"+&r"(src), [d]"+&r"(dst)
            : [width]"r"(width),
              [sstp]"r"(stride - width),
              [dstp]"r"(stride_dst - width),
              [transcolor]"r"(TRANSPARENT_COLOR),
              [fgcolor]"r"(REPLACEWITHFG_COLOR),
              [fgpat]"r"(fg)
        );
    }
#else  /* optimized C version */
    do
    {
        const fb_data *src_row = src;
        fb_data *dst_row = dst;
        fb_data *row_end = dst_row + width;
        do
        {
            unsigned data = *src_row++;
            if (data != TRANSPARENT_COLOR)
            {
                if (data == REPLACEWITHFG_COLOR)
                    data = fg;
                *dst_row = data;
            }
        }
        while (++dst_row < row_end);
        src += stride;
        dst += stride_dst;
    }
    while (--height > 0);
#endif
}
