/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2026 Nugget Y1
 * Rockbox glue for liby1 Bluetooth stack (y1_bt.c).
 ****************************************************************************/

#define RB_FILESYSTEM_OS
#include "autoconf.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "system.h"
#include "kernel.h"
#include "button.h"
#include "settings.h"
#include "bt-y1.h"
#include "pcm-eac.h"

static void rb_bt_avrcp_passthrough(uint8_t op_id)
{
    int btn = 0;
    switch (op_id) {
        case 0x44: /* PLAY */
        case 0x46: /* PAUSE */
        case 0x45: /* STOP */
            btn = BUTTON_MULTIMEDIA_PLAYPAUSE;
            break;
        case 0x4B: /* FORWARD / NEXT */
            btn = BUTTON_MULTIMEDIA_NEXT;
            break;
        case 0x4C: /* BACKWARD / PREV */
            btn = BUTTON_MULTIMEDIA_PREV;
            break;
        case 0x41: /* VOLUME UP */
            btn = BUTTON_MULTIMEDIA_VOLUME_UP;
            break;
        case 0x42: /* VOLUME DOWN */
            btn = BUTTON_MULTIMEDIA_VOLUME_DOWN;
            break;
        default:
            break;
    }
    if (btn != 0) {
        sim_enter_irq_handler();
        button_queue_post(btn, 0);
        sim_exit_irq_handler();
    }
}

void y1_bt_rockbox_init(void)
{
    static const struct y1_bt_callbacks cbs = {
        .on_audio_connect = y1_bt_audio_connect_hook,
        .on_audio_disconnect = y1_bt_audio_disconnect_hook,
        .on_receiver_mode_changed = y1_audio_receiver_mode_set,
        .on_receiver_stream_state = y1_audio_receiver_set_active,
        .on_receiver_pcm = y1_audio_receiver_write,
        .on_avrcp_passthrough = rb_bt_avrcp_passthrough,
    };

    y1_bt_set_callbacks(&cbs);
    y1_bt_set_receiver_auto_connect(global_settings.bt_receiver_auto_connect);
    y1_bt_set_enabled(global_settings.bt_enabled);
}
