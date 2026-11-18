/***************************************************************************
 *             __________               __   ___
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2026 Nugget Y1
 ****************************************************************************/

#ifndef _USB_Y1_H_
#define _USB_Y1_H_

#include <stdbool.h>
#include <stddef.h>

void y1_usb_attach_storage(void);
void y1_usb_eject_to_charge_only(void);
bool y1_is_charge_only_mode(void);
bool y1_usb_check_host_ejected(void);
bool y1_usb_is_converting(char *status_buf, size_t buf_sz);
bool y1_usb_take_tc_reload_needed(void);
void y1_usb_set_ipod_mode(bool enabled);

#endif /* _USB_Y1_H_ */
