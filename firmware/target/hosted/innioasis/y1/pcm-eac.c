/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2026 Rockbox Innioasis Y1 Port
 * MediaTek MT6572 direct /dev/eac PCM Sink Driver via liby1
 ****************************************************************************/

#define RB_FILESYSTEM_OS
#include "autoconf.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include "system.h"
#include "debug.h"
#include "kernel.h"
#include "panic.h"
#include "pcm.h"
#include "pcm_sampr.h"
#include "pcm-internal.h"
#include "settings.h"
#include "pcm_mixer.h"
#include "pcm_sink.h"
#include "pcm_sw_volume.h"
#include "audiohw.h"
#include "bt-y1.h"
#include "pcm-eac.h"
#include "button-target.h"
#include "y1-log.h"
#include "liby1.h"

static unsigned long current_sampr = 44100;
static bool audio_output_enabled = false;
static volatile sig_atomic_t dma_stopped = 1;
static volatile sig_atomic_t dma_locked = 0;

#define Y1_BT_PCM_RING_FRAMES 8192
static int16_t bt_pcm_ring[Y1_BT_PCM_RING_FRAMES * 2];
static size_t bt_pcm_ring_read;
static size_t bt_pcm_ring_write;
static volatile bool bt_receiver_active;
static volatile bool bt_receiver_mode_power_hold;
static long bt_receiver_last_pcm_tick;
static pthread_mutex_t bt_pcm_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t pcm_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pcm_cond = PTHREAD_COND_INITIALIZER;

static int pending_vol_l = 0;
static int pending_vol_r = 0;

static void sink_set_freq(uint16_t freq);
static void y1_audio_set_output_enabled(bool enable);
static int vol_cb_to_hp_idx(int vol_cb);
static int vol_cb_to_sw_remainder(int vol_cb, int hp_idx);

static bool y1_audio_receiver_is_active(void)
{
    pthread_mutex_lock(&bt_pcm_mtx);
    bool active = bt_receiver_active;
    pthread_mutex_unlock(&bt_pcm_mtx);
    return active;
}

void y1_audio_receiver_set_active(bool active, unsigned long sample_rate)
{
    if (active && !y1_bt_is_receiver_mode())
        return;

    pthread_mutex_lock(&bt_pcm_mtx);
    if (bt_receiver_active == active) {
        pthread_mutex_unlock(&bt_pcm_mtx);
        return;
    }
    bt_receiver_active = active;
    bt_pcm_ring_read = 0;
    bt_pcm_ring_write = 0;
    bt_receiver_last_pcm_tick = current_tick;
    pthread_mutex_unlock(&bt_pcm_mtx);

    if (active) {
        y1_audio_set_deep_idle(false);
        y1_audio_wake_lock(true);
        sink_set_freq((uint16_t)sample_rate);
        if (global_settings.bt_receiver_full_volume)
            audiohw_set_volume(60, 60);
    } else if (dma_stopped != 0) {
        y1_audio_set_output_enabled(false);
        if (!bt_receiver_mode_power_hold) {
            y1_audio_wake_lock(false);
            y1_audio_set_deep_idle(true);
        }
    }

    pthread_mutex_lock(&pcm_mtx);
    pthread_cond_signal(&pcm_cond);
    pthread_mutex_unlock(&pcm_mtx);
}

void y1_audio_receiver_mode_set(bool enabled)
{
    bt_receiver_mode_power_hold = enabled;
    if (enabled) {
        y1_audio_set_deep_idle(false);
        y1_audio_wake_lock(true);
    } else if (!y1_audio_receiver_is_active()) {
        y1_audio_wake_lock(false);
        y1_audio_set_deep_idle(true);
    }
}

size_t y1_audio_receiver_write(const int16_t *samples, size_t frames)
{
    if (!samples || !y1_bt_is_receiver_mode())
        return 0;

    size_t written = 0;
    pthread_mutex_lock(&bt_pcm_mtx);
    while (bt_receiver_active && written < frames) {
        size_t next = (bt_pcm_ring_write + 1) % Y1_BT_PCM_RING_FRAMES;
        if (next == bt_pcm_ring_read)
            break;
        bt_pcm_ring[bt_pcm_ring_write * 2] = samples[written * 2];
        bt_pcm_ring[bt_pcm_ring_write * 2 + 1] = samples[written * 2 + 1];
        bt_pcm_ring_write = next;
        written++;
    }
    if (frames > 0)
        bt_receiver_last_pcm_tick = current_tick;
    pthread_mutex_unlock(&bt_pcm_mtx);
    return written;
}

static int16_t y1_receiver_apply_volume(int16_t sample, int volume_cb)
{
    static const int32_t gain_q15[] = {
        29204, 32768, 36766, 41259, 46286, 51970, 58270, 65381
    };

    if (volume_cb <= -740)
        return 0;

    int hp_idx = vol_cb_to_hp_idx(volume_cb);
    int sw_cb = vol_cb_to_sw_remainder(volume_cb, hp_idx);
    if (sw_cb <= -740)
        return 0;
    if (sw_cb < -10)
        sw_cb = -10;
    else if (sw_cb > 60)
        sw_cb = 60;

    int32_t scaled = ((int32_t)sample *
                      gain_q15[(sw_cb + 10) / 10]) >> 15;
    if (scaled > INT16_MAX)
        scaled = INT16_MAX;
    else if (scaled < INT16_MIN)
        scaled = INT16_MIN;
    return (int16_t)scaled;
}

#define MT6323_HP_PGA_MAX_IDX    7
#define MT6323_HP_PGA_STEP_CB    20
#define MT6323_HP_PGA_MAX_CB     (MT6323_HP_PGA_MAX_IDX * MT6323_HP_PGA_STEP_CB)
#define MT6323_HP_PGA_OFFSET_CB  MT6323_HP_PGA_MAX_CB

int y1_read_pmic_reg(uint32_t offset)
{
    return y1_pmic_read_reg(offset);
}

void y1_write_pmic_reg(uint32_t offset, uint32_t val, uint32_t mask)
{
    y1_pmic_write_reg(offset, val, mask);
}

void audiohw_preinit(void) { }
void audiohw_postinit(void) { }
void audiohw_set_frequency(int fsel) { (void)fsel; }

static int vol_cb_to_hp_idx(int vol_cb)
{
    int target_cb;
    int analog_cb;

    if (vol_cb <= -740)
        return 0;

    target_cb = vol_cb + MT6323_HP_PGA_OFFSET_CB;
    if (target_cb <= 0)
        return 0;

    analog_cb = ((target_cb + MT6323_HP_PGA_STEP_CB - 1) / MT6323_HP_PGA_STEP_CB)
                * MT6323_HP_PGA_STEP_CB;
    if (analog_cb > MT6323_HP_PGA_MAX_CB)
        analog_cb = MT6323_HP_PGA_MAX_CB;
    return analog_cb / MT6323_HP_PGA_STEP_CB;
}

static int vol_cb_to_sw_remainder(int vol_cb, int hp_idx)
{
    int target_cb;
    int analog_cb;

    if (vol_cb <= -740)
        return PCM_MUTE_LEVEL;

    target_cb = vol_cb + MT6323_HP_PGA_OFFSET_CB;
    analog_cb = hp_idx * MT6323_HP_PGA_STEP_CB;
    return target_cb - analog_cb;
}

static void y1_audio_set_output_enabled(bool enable)
{
    if (enable) {
        if (!y1_audio_is_open() || !audio_output_enabled) {
            if (y1_audio_open(current_sampr) == 0) {
                audio_output_enabled = true;
                y1_audio_set_hp_analog_gain(vol_cb_to_hp_idx(pending_vol_l),
                                            vol_cb_to_hp_idx(pending_vol_r));
            }
        }
        if (y1_audio_is_open()) {
            bool run_dma = (dma_stopped == 0 && dma_locked == 0) ||
                           y1_audio_receiver_is_active();
            y1_audio_set_dma_running(run_dma);
        }
    } else {
        if (y1_audio_is_open() || audio_output_enabled) {
            y1_audio_close();
            audio_output_enabled = false;
        }
    }
}

void audiohw_set_volume(int vol_l, int vol_r)
{
    if (y1_bt_is_receiver_mode() && global_settings.bt_receiver_full_volume)
        vol_l = vol_r = 60;

    int idx_l = vol_cb_to_hp_idx(vol_l);
    int idx_r = vol_cb_to_hp_idx(vol_r);
    int sw_l = vol_cb_to_sw_remainder(vol_l, idx_l);
    int sw_r = vol_cb_to_sw_remainder(vol_r, idx_r);

    pending_vol_l = vol_l;
    pending_vol_r = vol_r;

    if (y1_bt_a2dp_is_streaming())
    {
        if (sw_l > 0) sw_l = 0;
        if (sw_r > 0) sw_r = 0;
    }

    pcm_set_master_volume(sw_l, sw_r);
    y1_audio_set_hp_analog_gain(idx_l, idx_r);
}

static pthread_t pcm_thread;
static volatile bool pcm_thread_exit = false;
static const void *pcm_data = NULL;
static size_t pcm_size = 0;

static void *pcm_worker(void *arg)
{
    (void)arg;

    while (!pcm_thread_exit)
    {
        pthread_mutex_lock(&pcm_mtx);
        while ((dma_stopped == 1 || dma_locked == 1) &&
               !y1_audio_receiver_is_active() && !pcm_thread_exit)
        {
            if (audio_output_enabled)
            {
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_sec += 5;
                int rc = pthread_cond_timedwait(&pcm_cond, &pcm_mtx, &ts);
                if (rc == ETIMEDOUT && dma_stopped == 1 &&
                    !y1_audio_receiver_is_active() && audio_output_enabled)
                {
                    pthread_mutex_unlock(&pcm_mtx);
                    y1_audio_set_output_enabled(false);
                    y1_audio_set_deep_idle(true);
                    pthread_mutex_lock(&pcm_mtx);
                }
            }
            else
            {
                pthread_cond_wait(&pcm_cond, &pcm_mtx);
            }
        }
        pthread_mutex_unlock(&pcm_mtx);

        if (pcm_thread_exit)
            break;

        if (y1_audio_receiver_is_active())
        {
            int16_t samples[512 * 2];
            size_t frames = 0;
            pthread_mutex_lock(&bt_pcm_mtx);
            while (frames < 512 && bt_pcm_ring_read != bt_pcm_ring_write) {
                samples[frames * 2] = bt_pcm_ring[bt_pcm_ring_read * 2];
                samples[frames * 2 + 1] = bt_pcm_ring[bt_pcm_ring_read * 2 + 1];
                bt_pcm_ring_read = (bt_pcm_ring_read + 1) % Y1_BT_PCM_RING_FRAMES;
                frames++;
            }
            pthread_mutex_unlock(&bt_pcm_mtx);

            if (frames == 0) {
                pthread_mutex_lock(&bt_pcm_mtx);
                long last_pcm_tick = bt_receiver_last_pcm_tick;
                pthread_mutex_unlock(&bt_pcm_mtx);
                if (audio_output_enabled &&
                    !TIME_BEFORE(current_tick, last_pcm_tick + MAX(HZ / 2, 1)))
                    y1_audio_set_output_enabled(false);
                usleep(20000);
                continue;
            }

            if (!y1_audio_receiver_is_active())
                continue;

            if (!audio_output_enabled)
                y1_audio_set_output_enabled(true);
            if (!y1_audio_receiver_is_active()) {
                y1_audio_set_output_enabled(false);
                continue;
            }

            int volume_l = pending_vol_l;
            int volume_r = pending_vol_r;
            if (y1_bt_is_receiver_mode() &&
                !global_settings.bt_receiver_full_volume) {
                for (size_t i = 0; i < frames; i++) {
                    samples[i * 2] = y1_receiver_apply_volume(samples[i * 2], volume_l);
                    samples[i * 2 + 1] = y1_receiver_apply_volume(samples[i * 2 + 1], volume_r);
                }
            }

            size_t offset = 0;
            size_t bytes = frames * 2 * sizeof(int16_t);
            while (offset < bytes && y1_audio_receiver_is_active() && y1_audio_is_open()) {
                ssize_t result = y1_audio_write_pcm((const uint8_t *)samples + offset,
                                                    bytes - offset);
                if (result > 0)
                    offset += (size_t)result;
                else if (result < 0 && errno == EINTR)
                    continue;
                else if (result < 0 && errno == EAGAIN)
                    usleep(1000);
                else
                    break;
            }
            continue;
        }

        if (pcm_size == 0)
        {
            if (!pcm_play_dma_complete_callback(PCM_DMAST_OK, &pcm_data, &pcm_size))
            {
                usleep(1000);
                continue;
            }
            pcm_play_dma_status_callback(PCM_DMAST_STARTED);
        }

        if (pcm_data && pcm_size > 0 && dma_stopped == 0 && !pcm_thread_exit)
        {
            bool bt_active = y1_bt_a2dp_is_streaming();
            if (!bt_active && (!audio_output_enabled || !y1_audio_is_open()))
                y1_audio_set_output_enabled(true);
        }

        if ((y1_audio_is_open() || y1_bt_a2dp_is_streaming()) && pcm_data && pcm_size > 0)
        {
            const char *buf = (const char *)pcm_data;
            size_t remaining = pcm_size;

            while (remaining > 0 && dma_stopped == 0 && dma_locked == 0 && !pcm_thread_exit)
            {
                size_t chunk = remaining > 4096 ? 4096 : (remaining & ~3);
                if (chunk == 0)
                    break;

                bool bt_active = y1_bt_a2dp_is_streaming();
                if (bt_active && audio_output_enabled)
                    y1_audio_set_output_enabled(false);
                else if (!bt_active && (!audio_output_enabled || !y1_audio_is_open()) && dma_stopped == 0)
                    y1_audio_set_output_enabled(true);

                ssize_t written;
                if (bt_active) {
                    y1_bt_a2dp_write_pcm(buf, chunk, current_sampr);
                    if (current_sampr)
                        usleep((useconds_t)((chunk / 4) * 1000000UL / current_sampr));
                    written = (ssize_t)chunk;
                } else {
                    written = y1_audio_write_pcm(buf, chunk);
                }

                if (written > 0)
                {
                    if ((size_t)written > remaining)
                        written = (ssize_t)remaining;
                    remaining -= (size_t)written;
                    buf += written;
                }
                else if (written < 0 && (errno == EAGAIN || errno == EINTR))
                {
                    usleep(1000);
                }
                else
                {
                    usleep(2000);
                    break;
                }
            }

            if (dma_stopped == 1 || pcm_thread_exit)
            {
                pcm_size = 0;
                pcm_data = NULL;
            }
            else
            {
                pcm_data = buf;
                pcm_size = remaining;
            }
        }
        else
        {
            usleep(2000);
        }
    }

    return NULL;
}

static int play_lock_recursion = -10000;

static void sink_lock(void)
{
    ++play_lock_recursion;
    if (play_lock_recursion == 1)
    {
        pthread_mutex_lock(&pcm_mtx);
        dma_locked = 1;
        pthread_mutex_unlock(&pcm_mtx);
    }
}

static void sink_unlock(void)
{
    --play_lock_recursion;
    if (play_lock_recursion == 0)
    {
        pthread_mutex_lock(&pcm_mtx);
        dma_locked = 0;
        pthread_cond_signal(&pcm_cond);
        pthread_mutex_unlock(&pcm_mtx);
    }
}

static void sink_dma_init(void)
{
    if (!y1_audio_is_open()) {
        if (y1_audio_open(current_sampr) == 0) {
            audio_output_enabled = true;
            y1_audio_set_hp_analog_gain(vol_cb_to_hp_idx(pending_vol_l),
                                        vol_cb_to_hp_idx(pending_vol_r));
        }
    }

    pcm_thread_exit = false;
    dma_stopped = 1;
    dma_locked = 1;
    play_lock_recursion = -10000;
    pthread_create(&pcm_thread, NULL, pcm_worker, NULL);
    struct sched_param sp;
    sp.sched_priority = 80;
    pthread_setschedparam(pcm_thread, SCHED_FIFO, &sp);
}

static void sink_dma_postinit(void)
{
    play_lock_recursion = 0;
    dma_locked = 0;
    audiohw_postinit();
    y1_bt_init();
}

static void sink_set_freq(uint16_t freq)
{
    unsigned long sampr = (freq < HW_NUM_FREQ) ? hw_freq_sampr[freq] : freq;
    current_sampr = sampr;
    if (y1_audio_is_open() && audio_output_enabled) {
        y1_audio_set_sample_rate(sampr);
    }
}

static void sink_dma_start(const void *addr, size_t size)
{
    pcm_data = addr;
    pcm_size = size;
    y1_audio_set_deep_idle(false);

    pthread_mutex_lock(&pcm_mtx);
    dma_stopped = 0;
    pthread_mutex_unlock(&pcm_mtx);

    y1_audio_wake_lock(true);
    y1_audio_set_output_enabled(!y1_bt_a2dp_is_streaming());
    if (y1_audio_is_open() && audio_output_enabled)
    {
        sink_set_freq((uint16_t)current_sampr);
        y1_audio_set_dma_running(true);
    }

    pthread_mutex_lock(&pcm_mtx);
    pthread_cond_signal(&pcm_cond);
    pthread_mutex_unlock(&pcm_mtx);

    pcm_play_dma_status_callback(PCM_DMAST_STARTED);
}

static void sink_dma_stop(void)
{
    pthread_mutex_lock(&pcm_mtx);
    dma_stopped = 1;
    pcm_data = NULL;
    pcm_size = 0;
    pthread_mutex_unlock(&pcm_mtx);

    if (y1_audio_is_open())
    {
        y1_audio_set_dma_running(false);
        y1_audio_wake_lock(false);
    }
}

void audiohw_close(void)
{
    pthread_mutex_lock(&pcm_mtx);
    pcm_thread_exit = true;
    dma_stopped = 1;
    pcm_data = NULL;
    pcm_size = 0;
    pthread_cond_broadcast(&pcm_cond);
    pthread_mutex_unlock(&pcm_mtx);

    y1_audio_set_output_enabled(false);
}

#ifdef HAVE_RECORDING

static pthread_t rec_thread;
static volatile bool rec_thread_running = false;
static volatile bool rec_thread_exit = false;
static pthread_mutex_t rec_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t rec_cond = PTHREAD_COND_INITIALIZER;
static void *rec_pcm_data = NULL;
static size_t rec_pcm_size = 0;
static int rec_mic_gain_db = 12;
static int active_mic_source = Y1_MIC_INTERNAL;

int y1_get_effective_mic_source(void)
{
    int sel = global_settings.y1_mic_source;
    if (sel == Y1_MIC_AUTO) {
        if (y1_bt_is_connected())
            return Y1_MIC_BLUETOOTH;
        else if (headphones_has_mic())
            return Y1_MIC_HEADPHONE;
        else
            return Y1_MIC_INTERNAL;
    }
    return sel;
}

void y1_mic_source_changed(int source)
{
    (void)source;
    if (rec_thread_running) {
        audiohw_enable_recording(true);
    }
}

void pcm_rec_lock(void)
{
    pthread_mutex_lock(&rec_mtx);
}

void pcm_rec_unlock(void)
{
    pthread_mutex_unlock(&rec_mtx);
}

static void *rec_worker(void *arg)
{
    (void)arg;
    while (!rec_thread_exit)
    {
        pthread_mutex_lock(&rec_mtx);
        while (!rec_thread_running && !rec_thread_exit)
        {
            pthread_cond_wait(&rec_cond, &rec_mtx);
        }
        pthread_mutex_unlock(&rec_mtx);

        if (rec_thread_exit)
            break;

        if (rec_pcm_data && rec_pcm_size > 0)
        {
            ssize_t bytes_read = -1;
            if (active_mic_source == Y1_MIC_BLUETOOTH)
            {
                bytes_read = y1_bt_read_mic_pcm(rec_pcm_data, rec_pcm_size > 4096 ? 4096 : rec_pcm_size);
            }

            if (bytes_read <= 0)
            {
                size_t fill_chunk = rec_pcm_size > 4096 ? 4096 : rec_pcm_size;
                memset(rec_pcm_data, 0, fill_chunk);
                usleep(20000);
                bytes_read = (ssize_t)fill_chunk;
            }

            rec_pcm_data = (char *)rec_pcm_data + bytes_read;
            rec_pcm_size -= (size_t)bytes_read;

            if (rec_pcm_size == 0)
            {
                if (!pcm_rec_dma_complete_callback(PCM_DMAST_OK, &rec_pcm_data, &rec_pcm_size))
                {
                    rec_thread_running = false;
                    rec_pcm_data = NULL;
                    rec_pcm_size = 0;
                }
                else
                {
                    pcm_rec_dma_status_callback(PCM_DMAST_STARTED);
                }
            }
        }
        else
        {
            usleep(5000);
        }
    }
    return NULL;
}

void pcm_rec_dma_init(void)
{
    static bool rec_inited = false;
    if (!rec_inited)
    {
        rec_thread_exit = false;
        rec_thread_running = false;
        pthread_create(&rec_thread, NULL, rec_worker, NULL);
        rec_inited = true;
    }
}

void pcm_rec_dma_close(void)
{
    pcm_rec_dma_stop();
}

void pcm_rec_dma_start(void *start, size_t size)
{
    pthread_mutex_lock(&rec_mtx);
    rec_pcm_data = start;
    rec_pcm_size = size;
    rec_thread_running = true;
    pthread_cond_signal(&rec_cond);
    pthread_mutex_unlock(&rec_mtx);

    pcm_rec_dma_status_callback(PCM_DMAST_STARTED);
}

void pcm_rec_dma_stop(void)
{
    pthread_mutex_lock(&rec_mtx);
    rec_thread_running = false;
    rec_pcm_data = NULL;
    rec_pcm_size = 0;
    pthread_mutex_unlock(&rec_mtx);
    if (active_mic_source == Y1_MIC_BLUETOOTH)
    {
        y1_bt_stop_mic();
    }
}

const void *pcm_rec_dma_get_peak_buffer(void)
{
    return rec_pcm_data;
}

void audiohw_enable_recording(bool source_mic)
{
    if (!source_mic)
    {
        active_mic_source = -1;
        y1_audio_enable_recording_path(Y1_REC_SOURCE_LINE_FM);
        y1_log("[pcm-eac] FM Radio / Line recording hardware path enabled\n");
        return;
    }

    active_mic_source = y1_get_effective_mic_source();

    if (active_mic_source == Y1_MIC_BLUETOOTH)
    {
        y1_audio_disable_recording_path();
        y1_bt_start_mic();
        y1_log("[pcm-eac] Bluetooth Handset Microphone selected\n");
        return;
    }

    if (active_mic_source == Y1_MIC_HEADPHONE) {
        y1_audio_enable_recording_path(Y1_REC_SOURCE_HEADPHONE_MIC);
        y1_log("[pcm-eac] Wired Headphone Mic (ACC_MIC) selected\n");
    } else {
        y1_audio_enable_recording_path(Y1_REC_SOURCE_INTERNAL_MIC);
        y1_log("[pcm-eac] Built-in Internal Mic (MIC_P1) selected\n");
    }
}

void audiohw_disable_recording(void)
{
    if (active_mic_source == Y1_MIC_BLUETOOTH)
    {
        y1_bt_stop_mic();
    }
    y1_audio_disable_recording_path();
    active_mic_source = Y1_MIC_INTERNAL;
    y1_log("[pcm-eac] Microphone / Recording hardware path disabled\n");
}

void audiohw_set_recvol(int left, int right, int type)
{
    (void)right;
    (void)type;
    rec_mic_gain_db = left;
    y1_audio_set_recording_pga_db(left);
}

void audiohw_set_monitor(bool enable)
{
    (void)enable;
}

#endif /* HAVE_RECORDING */

struct pcm_sink builtin_pcm_sink = {
    .caps = {
        .samprs       = hw_freq_sampr,
        .num_samprs   = HW_NUM_FREQ,
        .default_freq = HW_FREQ_DEFAULT,
        .volume_type  = PCM_NATIVE_VOLUME_TYPE,
    },
    .ops = {
        .init     = sink_dma_init,
        .postinit = sink_dma_postinit,
        .set_freq = sink_set_freq,
        .lock     = sink_lock,
        .unlock   = sink_unlock,
        .play     = sink_dma_start,
        .stop     = sink_dma_stop,
    },
};
