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
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <time.h>
#include <linux/rtc.h>
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
static uint64_t g_pre_usb_sys_hash = 0;
static bool g_pre_usb_sys_valid = false;
static char g_convert_stage[96] = {0};
static int g_convert_done = 0;
static int g_convert_total = 0;

static void y1_sync_progress_cb(const char *stage, int done, int total, void *user)
{
    (void)user;
    if (stage)
        snprintf(g_convert_stage, sizeof(g_convert_stage), "%s", stage);
    else
        g_convert_stage[0] = '\0';
    g_convert_done = done;
    g_convert_total = total;
}

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

static uint64_t fnv1a_update(uint64_t h, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static uint64_t hash_dir_tree(const char *path, uint64_t h)
{
    DIR *d = opendir(path);
    if (!d)
        return h;

    struct dirent *ent;
    char child[512];
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        if (strcmp(ent->d_name, "y1_debug.txt") == 0)
            continue;

        snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
        struct stat st;
        if (lstat(child, &st) != 0)
            continue;

        h = fnv1a_update(h, child, strlen(child) + 1);
        uint64_t meta[3] = {
            (uint64_t)st.st_mode,
            (uint64_t)st.st_size,
            (uint64_t)st.st_mtime
        };
        h = fnv1a_update(h, meta, sizeof(meta));

        if (S_ISDIR(st.st_mode))
            h = hash_dir_tree(child, h);
    }
    closedir(d);
    return h;
}

static uint64_t compute_sys_tree_hash(void)
{
    uint64_t h = 14695981039346656037ULL;
    h = hash_dir_tree("/mnt/sdcard/.rockbox", h);
    h = hash_dir_tree("/mnt/sdcard/.system", h);
    return h;
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

    g_pre_usb_sys_hash = compute_sys_tree_hash();
    g_pre_usb_sys_valid = true;

    if (y1_usb_attach_mass_storage())
        g_attached_since_post_sync = true;
}

static void sync_time_from_usb_sdcard(void)
{
    time_t now = time(NULL);
    time_t best_mtime = 0;

    static const char * const probe_paths[] = {
        "/mnt/sdcard",
        "/mnt/sdcard/iPod_Control/iTunes/iTunesDB",
        "/mnt/sdcard/iPod_Control/iTunes",
        "/mnt/sdcard/.rockbox",
        "/mnt/sdcard/Playlists",
    };

    for (size_t i = 0; i < sizeof(probe_paths) / sizeof(probe_paths[0]); i++) {
        struct stat st;
        if (stat(probe_paths[i], &st) == 0 && st.st_mtime > best_mtime)
            best_mtime = st.st_mtime;
    }

    DIR *d = opendir("/mnt/sdcard");
    if (d) {
        struct dirent *ent;
        char full[300];
        while ((ent = readdir(d)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
                continue;
            snprintf(full, sizeof(full), "/mnt/sdcard/%s", ent->d_name);
            struct stat st;
            if (stat(full, &st) == 0 && st.st_mtime > best_mtime)
                best_mtime = st.st_mtime;
        }
        closedir(d);
    }

    /* Update clock only if the newest host timestamp is valid and strictly newer than current time */
    if (best_mtime >= 1700000000L && best_mtime > now) {
        struct timeval tv = { .tv_sec = best_mtime, .tv_usec = 0 };
        settimeofday(&tv, NULL);

        struct tm tm_utc;
        if (gmtime_r(&best_mtime, &tm_utc)) {
            int rfd = open("/dev/rtc0", O_WRONLY | O_CLOEXEC);
            if (rfd >= 0) {
                struct rtc_time rtm;
                memset(&rtm, 0, sizeof(rtm));
                rtm.tm_sec  = tm_utc.tm_sec;
                rtm.tm_min  = tm_utc.tm_min;
                rtm.tm_hour = tm_utc.tm_hour;
                rtm.tm_mday = tm_utc.tm_mday;
                rtm.tm_mon  = tm_utc.tm_mon;
                rtm.tm_year = tm_utc.tm_year;
                rtm.tm_wday = tm_utc.tm_wday;
                rtm.tm_yday = tm_utc.tm_yday;
                ioctl(rfd, RTC_SET_TIME, &rtm);
                close(rfd);
            }
        }
    }
}

static void run_post_disconnect_once(void)
{
    sync_time_from_usb_sdcard();

    if (g_pre_usb_sys_valid) {
        g_pre_usb_sys_valid = false;
        uint64_t post_hash = compute_sys_tree_hash();
        if (post_hash != g_pre_usb_sys_hash) {
            sync();
            y1_sys_reboot();
        }
    }

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
        cached_present = y1_power_usb_online();

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
