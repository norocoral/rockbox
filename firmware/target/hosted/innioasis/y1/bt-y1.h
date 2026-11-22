/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2026 Nugget Y1
 * Thin Rockbox wrapper over liby1 Bluetooth stack (y1_bt.c).
 ****************************************************************************/

#ifndef _BT_Y1_H_
#define _BT_Y1_H_

#include "liby1.h"

void y1_bt_rockbox_init(void);
void y1_bt_audio_disconnect_hook(void);
void y1_bt_audio_connect_hook(void);
void y1_bt_process_pending_audio_events(void);

#endif /* _BT_Y1_H_ */
