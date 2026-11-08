/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Compiles the standalone liby1 hardware abstraction layer into Rockbox.
 ****************************************************************************/

#define RB_FILESYSTEM_OS
#if defined(__has_include) && __has_include("../../../../../../!nugget/main/liby1/src/y1_core.c")
#include "../../../../../../!nugget/main/liby1/src/y1_core.c"
#include "../../../../../../!nugget/main/liby1/src/y1_display.c"
#include "../../../../../../!nugget/main/liby1/src/y1_cpu.c"
#include "../../../../../../!nugget/main/liby1/src/y1_input.c"
#include "../../../../../../!nugget/main/liby1/src/y1_audio.c"
#include "../../../../../../!nugget/main/liby1/src/y1_tuner.c"
#include "../../../../../../!nugget/main/liby1/src/y1_usb.c"
#else
#include "../../../../../hosted_linux_port/liby1/src/y1_core.c"
#include "../../../../../hosted_linux_port/liby1/src/y1_display.c"
#include "../../../../../hosted_linux_port/liby1/src/y1_cpu.c"
#include "../../../../../hosted_linux_port/liby1/src/y1_input.c"
#include "../../../../../hosted_linux_port/liby1/src/y1_audio.c"
#include "../../../../../hosted_linux_port/liby1/src/y1_tuner.c"
#include "../../../../../hosted_linux_port/liby1/src/y1_usb.c"
#endif

