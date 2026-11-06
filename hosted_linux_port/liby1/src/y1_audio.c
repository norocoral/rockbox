/***************************************************************************
 * liby1 - MT6572 AFE (/dev/eac) & MT6323 PMIC DAC / Headphone Amp / ADC
 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/sysmacros.h>

#ifndef makedev
#define makedev(maj, min) (((maj) << 8) | (min))
#endif

#include "liby1.h"

#define SET_SPEAKER_OFF          0x400443a2
#define SET_HEADPHONE_ON         0x400443a4
#define SET_HEADPHONE_OFF        0x400443a5
#define SET_EARPIECE_OFF         0x400443a7
#define AUDDRV_ALLOCATE_DL1_BUF  0xc0044310
#define AUDDRV_FREE_DL1_BUF      0xc0044311
#define FREE_MEMIF_BUF           0xc004431f
#define SET_MEMIF_FP             0xc0044320
#define FREE_MEMIF_FP            0xc0044321
#define AUDDRV_SET_AFEREG        0xc0044300
#define AUDDRV_SET_ANAREG        0xc0044302
#define AUDDRV_GET_ANAREG        0xc0044303
#define AUD_DRV_AFE_CLK          0xc0044355
#define AUD_DRV_AIF_CLK          0xc0044351

#define MT6323_AUDTOP_CON5       0x070a

struct y1_reg_ctl {
    uint32_t offset;
    uint32_t value;
    uint32_t mask;
};

static int eac_fd = -1;
static unsigned long current_sampr = 44100;
static unsigned long programmed_sampr = 0;
static int current_hp_idx_l = -1;
static int current_hp_idx_r = -1;
static int saved_hp_idx_l = 7;
static int saved_hp_idx_r = 7;
static bool deep_idle_enabled = true;
static bool cpu_powerdown_enabled = true;
static int wakelock_fd = -1;

static inline int set_afereg(int fd, uint32_t offset, uint32_t val, uint32_t mask)
{
    struct y1_reg_ctl reg = { offset, val, mask };
    return ioctl(fd, AUDDRV_SET_AFEREG, &reg);
}

static inline int set_anareg(int fd, uint32_t offset, uint32_t val, uint32_t mask)
{
    struct y1_reg_ctl reg = { offset, val, mask };
    return ioctl(fd, AUDDRV_SET_ANAREG, &reg);
}

static inline uint32_t get_anareg(int fd, uint32_t offset)
{
    struct y1_reg_ctl reg = { offset, 0, ~0U };
    if (ioctl(fd, AUDDRV_GET_ANAREG, &reg) == 0)
        return reg.value;
    return 0;
}

static inline void top_ctl_trigger(int fd)
{
    uint32_t val = get_anareg(fd, 0x4016);
    uint32_t toggle = (val ^ 0x300) & 0x300;
    set_anareg(fd, 0x4016, toggle, 0x300);
}

static void audio_clocks(int on)
{
    if (eac_fd < 0)
        return;
    ioctl(eac_fd, AUD_DRV_AFE_CLK, on);
    ioctl(eac_fd, AUD_DRV_AIF_CLK, on);
}

static void ensure_eac_node(void)
{
    if (access("/dev/eac", F_OK) == 0)
        return;

    int maj = 10, min = -1;
    FILE *fp = fopen("/sys/class/misc/eac/dev", "r");
    if (fp) {
        if (fscanf(fp, "%d:%d", &maj, &min) == 2 && min >= 0) {
            y1_logf("[liby1-audio] Discovered /sys/class/misc/eac/dev: major %d, minor %d\n", maj, min);
            unlink("/dev/eac");
            mknod("/dev/eac", S_IFCHR | 0666, makedev(maj, min));
        }
        fclose(fp);
    }
}

int y1_pmic_read_reg(uint32_t offset)
{
    if (eac_fd < 0)
        return -1;
    struct y1_reg_ctl reg = { offset, 0, ~0U };
    if (ioctl(eac_fd, AUDDRV_GET_ANAREG, &reg) == 0)
        return (int)reg.value;
    return -1;
}

int y1_pmic_write_reg(uint32_t offset, uint32_t val, uint32_t mask)
{
    if (eac_fd < 0)
        return -1;
    return set_anareg(eac_fd, offset, val, mask);
}

int y1_afe_write_reg(uint32_t offset, uint32_t val, uint32_t mask)
{
    if (eac_fd < 0)
        return -1;
    return set_afereg(eac_fd, offset, val, mask);
}

void y1_pmic_tune_accdet_debounce(void)
{
    if (eac_fd >= 0) {
        set_anareg(eac_fd, 0x0786, 0x0180, 0xffff);
        set_anareg(eac_fd, 0x0788, 0x0180, 0xffff);
    }
}

bool y1_audio_is_open(void)
{
    return eac_fd >= 0;
}

int y1_audio_get_fd(void)
{
    return eac_fd;
}

int y1_audio_open(unsigned long sample_rate)
{
    if (sample_rate == 0)
        sample_rate = current_sampr ? current_sampr : 44100;
    current_sampr = sample_rate;

    if (eac_fd >= 0)
        return 0;

    ensure_eac_node();
    eac_fd = open("/dev/eac", O_RDWR | O_CLOEXEC);
    if (eac_fd < 0) {
        y1_logf("[liby1-audio] ERROR: open(/dev/eac) failed: %s (errno=%d)\n", strerror(errno), errno);
        return -1;
    }

    /* 1. Turn on Audio Clocks (AFE & AIF) */
    audio_clocks(1);

    /* 2. Reset and allocate coherent DL1 DMA ring buffer (32768 bytes) */
    ioctl(eac_fd, FREE_MEMIF_FP, 0);
    ioctl(eac_fd, AUDDRV_FREE_DL1_BUF, 0);
    ioctl(eac_fd, FREE_MEMIF_BUF, 0);
    ioctl(eac_fd, AUDDRV_ALLOCATE_DL1_BUF, 32768);
    ioctl(eac_fd, SET_MEMIF_FP, 0);

    /* 3. Set up AFE Digital Interconnections: DL1 -> I2S Out */
    set_afereg(eac_fd, 0x24, 1 << 21, 1 << 21);
    set_afereg(eac_fd, 0x28, 1 << 6,  1 << 6);
    set_afereg(eac_fd, 0x10c, 0xf74f0000, ~0U);
    set_afereg(eac_fd, 0x124, 1, 1);
    set_afereg(eac_fd, 0x4c4, 0, 0x10);
    ioctl(eac_fd, SET_SPEAKER_OFF, 0);
    ioctl(eac_fd, SET_EARPIECE_OFF, 0);

    /* 4. Power up MT6323 PMIC Audio Clocks & AFE master */
    set_anareg(eac_fd, 0x106, 1, 0xffff);
    set_anareg(eac_fd, 0x10c, 0x100, 0x100);
    set_afereg(eac_fd, 0x10, 1, 1);

    /* 5. Program hardware sample rate */
    programmed_sampr = 0;
    y1_audio_set_sample_rate(current_sampr);

    /* 6. Power up Downlink DAC & analog L/R paths */
    set_anareg(eac_fd, 0x700, 0x7800, 0x7f80);
    set_anareg(eac_fd, 0x704, 0x00f0, 0x00ff);
    set_anareg(eac_fd, 0x702, 0x0500, 0x0700);
    set_anareg(eac_fd, 0x700, 0x0050, 0x0070);
    set_anareg(eac_fd, 0x710, 0x0008, 0x0008);
    set_anareg(eac_fd, 0x700, 0x0180, 0x0180);
    set_anareg(eac_fd, 0x704, 0x0003, 0x0003);

    /* 7. Configure MT6323 PMIC Headphone Amplifier with pop suppression */
    set_anareg(eac_fd, 0x70c, 0xf7f2, 0xffff);
    set_anareg(eac_fd, 0x700, 0x7000, 0xf000);
    set_anareg(eac_fd, 0x70a, 0x0014, 0xffff);
    set_anareg(eac_fd, 0x708, 0x007c, 0xffff);
    usleep(10000);
    set_anareg(eac_fd, 0x70c, 0xf5ba, 0xffff);

    /* 8. Ensure 0 dB unity digital downlink gain */
    set_anareg(eac_fd, 0x4006, 0, 0xffff);
    set_anareg(eac_fd, 0x4008, 0, 0xffff);

    current_hp_idx_l = -1;
    current_hp_idx_r = -1;
    y1_audio_set_hp_analog_gain(saved_hp_idx_l, saved_hp_idx_r);

    set_anareg(eac_fd, 0x4014, 1, 1);
    top_ctl_trigger(eac_fd);
    set_anareg(eac_fd, 0x4000, 1, 1);
    ioctl(eac_fd, SET_HEADPHONE_ON, 3);

    /* Enable DL1 DMA */
    set_afereg(eac_fd, 0x10, 1 << 1, 1 << 1);

    y1_log("[liby1-audio] MT6323 PMIC Digital Audio Codec & Headphone Amp configured.\n");
    return 0;
}

void y1_audio_close(void)
{
    if (eac_fd < 0)
        return;

    set_afereg(eac_fd, 0x10, 0, 1 << 1);
    ioctl(eac_fd, SET_HEADPHONE_OFF, 0);
    set_anareg(eac_fd, 0x4014, 0, 1);
    set_anareg(eac_fd, 0x4000, 0, 1);
    top_ctl_trigger(eac_fd);

    /* Power down MT6323 analog headphone amp, charge pump, preamps, and DACs */
    set_anareg(eac_fd, 0x70a, 0x0000, 0xffff);
    set_anareg(eac_fd, 0x708, 0x0000, 0xffff);
    set_anareg(eac_fd, 0x70c, 0x0000, 0xffff);
    set_anareg(eac_fd, 0x704, 0x0000, 0x00ff);
    set_anareg(eac_fd, 0x702, 0x0000, 0x0700);
    set_anareg(eac_fd, 0x700, 0x0000, 0xfffa);
    set_anareg(eac_fd, 0x710, 0x0000, 0x0008);

    /* Gate PMIC 13MHz audio clock */
    set_anareg(eac_fd, 0x10c, 0x0000, 0x0100);
    set_anareg(eac_fd, 0x106, 0x0000, 0xffff);
    set_afereg(eac_fd, 0x10, 0, 1 << 0);

    /* Release DL1 DMA buffer and close /dev/eac so AudDrv_release() drops kernel audio wakelocks */
    ioctl(eac_fd, FREE_MEMIF_FP, 0);
    ioctl(eac_fd, AUDDRV_FREE_DL1_BUF, 0);
    ioctl(eac_fd, FREE_MEMIF_BUF, 0);
    audio_clocks(0);
    close(eac_fd);
    eac_fd = -1;

    y1_audio_wake_lock(false);
    programmed_sampr = 0;
    current_hp_idx_l = -1;
    current_hp_idx_r = -1;
}

bool y1_audio_set_sample_rate(unsigned long sampr)
{
    current_sampr = sampr;
    if (eac_fd < 0)
        return true;
    if (sampr == programmed_sampr)
        return true;

    uint32_t dac_con1;
    uint32_t rate_code;
    uint32_t pmic_newif_rate;

    y1_logf("[liby1-audio] program_hw_sampr: %lu Hz\n", sampr);

    switch (sampr) {
    case 48000:
        dac_con1 = 0x83001803;
        rate_code = 10;
        pmic_newif_rate = 8;
        break;
    case 32000:
        dac_con1 = 0x63001803;
        rate_code = 8;
        pmic_newif_rate = 6;
        break;
    case 24000:
        dac_con1 = 0x53001803;
        rate_code = 6;
        pmic_newif_rate = 5;
        break;
    case 22050:
        dac_con1 = 0x43001803;
        rate_code = 5;
        pmic_newif_rate = 4;
        break;
    case 16000:
        dac_con1 = 0x33001823;
        rate_code = 4;
        pmic_newif_rate = 3;
        break;
    case 12000:
        dac_con1 = 0x23001803;
        rate_code = 2;
        pmic_newif_rate = 2;
        break;
    case 11025:
        dac_con1 = 0x13001803;
        rate_code = 1;
        pmic_newif_rate = 1;
        break;
    case 8000:
        dac_con1 = 0x03001823;
        rate_code = 0;
        pmic_newif_rate = 0;
        break;
    case 44100:
    default:
        dac_con1 = 0x73001803;
        rate_code = 9;
        pmic_newif_rate = 7;
        break;
    }

    /* Pause DL1 DMA and gate PMIC digital paths during divider change */
    set_afereg(eac_fd, 0x10, 0, 1 << 1);
    set_anareg(eac_fd, 0x4014, 0, 1);
    set_anareg(eac_fd, 0x4000, 0, 1);

    /* Flush and reset DL1 DMA ring buffer pointers */
    ioctl(eac_fd, FREE_MEMIF_FP, 0);
    ioctl(eac_fd, AUDDRV_FREE_DL1_BUF, 0);
    ioctl(eac_fd, AUDDRV_ALLOCATE_DL1_BUF, 32768);
    ioctl(eac_fd, SET_MEMIF_FP, 0);

    set_afereg(eac_fd, 0x24, 1 << 21, 1 << 21);
    set_afereg(eac_fd, 0x28, 1 << 6,  1 << 6);

    bool ok = true;
    if (set_afereg(eac_fd, 0x14, rate_code, 0x0f) < 0) ok = false;
    if (set_afereg(eac_fd, 0x3a0, (rate_code << 4) | 1, 0xf1) < 0) ok = false;
    if (set_afereg(eac_fd, 0x3ac, 1024, ~0U) < 0) ok = false;
    if (set_afereg(eac_fd, 0x108, dac_con1, ~0U) < 0) ok = false;
    if (set_afereg(eac_fd, 0x34, (rate_code << 8) | 0x09, ~0U) < 0) ok = false;
    if (set_anareg(eac_fd, 0x4024, (pmic_newif_rate << 12) | 0x330, 0xffff) < 0) ok = false;
    if (set_anareg(eac_fd, 0x4002, rate_code, 0x0f) < 0) ok = false;

    set_anareg(eac_fd, 0x4006, 0, 0xffff);
    set_anareg(eac_fd, 0x4008, 0, 0xffff);
    set_anareg(eac_fd, 0x4014, 1, 1);
    top_ctl_trigger(eac_fd);
    set_anareg(eac_fd, 0x4000, 1, 1);

    usleep(5000);
    set_afereg(eac_fd, 0x10, 1 << 1, 1 << 1);

    if (ok)
        programmed_sampr = sampr;
    return ok;
}

unsigned long y1_audio_get_sample_rate(void)
{
    return current_sampr;
}

void y1_audio_set_hp_analog_gain(int idx_l, int idx_r)
{
    if (idx_l < 0) idx_l = 0;
    if (idx_l > 7) idx_l = 7;
    if (idx_r < 0) idx_r = 0;
    if (idx_r > 7) idx_r = 7;

    saved_hp_idx_l = idx_l;
    saved_hp_idx_r = idx_r;

    if (eac_fd < 0)
        return;

    if (idx_l == current_hp_idx_l && idx_r == current_hp_idx_r)
        return;

    current_hp_idx_l = idx_l;
    current_hp_idx_r = idx_r;

    /* Bits [14:12] = HPL Gain (0..7), Bits [10:8] = HPR Gain (0..7).
     * Mask 0x7700 preserves HP mux/power bits [4:0] (0x0014) without pop. */
    uint32_t con5_gain = ((uint32_t)(idx_l & 0x7) << 12) |
                         ((uint32_t)(idx_r & 0x7) << 8);
    set_anareg(eac_fd, MT6323_AUDTOP_CON5, con5_gain, 0x7700);
}

void y1_audio_set_dma_running(bool running)
{
    if (eac_fd < 0)
        return;
    if (running)
        set_afereg(eac_fd, 0x10, (1 << 0) | (1 << 1), (1 << 0) | (1 << 1));
    else
        set_afereg(eac_fd, 0x10, 0, 1 << 1);
}

ssize_t y1_audio_write_pcm(const void *pcm_stereo_s16, size_t bytes)
{
    if (eac_fd < 0 || !pcm_stereo_s16 || bytes == 0)
        return -1;

    ssize_t written = write(eac_fd, pcm_stereo_s16, bytes);
    if (written > 0) {
        size_t unaligned = (size_t)written & 3;
        if (unaligned != 0) {
            static const uint8_t pad[4] = {0, 0, 0, 0};
            size_t pad_len = 4 - unaligned;
            (void)!write(eac_fd, pad, pad_len);
            written += (ssize_t)pad_len;
            if ((size_t)written > bytes)
                written = (ssize_t)bytes;
        }
    }
    return written;
}

bool y1_audio_set_deep_idle(bool allow_cpu_powerdown)
{
    const char *cpu_powerdown = allow_cpu_powerdown ? "cpupdn 1\n" : "cpupdn 0\n";
    bool success = true;

    if (cpu_powerdown_enabled != allow_cpu_powerdown) {
        if (y1_sysfs_write("/sys/power/dpidle_state", cpu_powerdown))
            cpu_powerdown_enabled = allow_cpu_powerdown;
        else
            success = false;
    }

    if (!deep_idle_enabled) {
        if (y1_sysfs_write("/sys/power/dpidle_state", "1\n"))
            deep_idle_enabled = true;
        else
            success = false;
    }

    return success;
}

void y1_audio_wake_lock(bool hold)
{
    if (wakelock_fd < 0)
        wakelock_fd = open("/sys/power/wake_lock", O_WRONLY | O_CLOEXEC);
    if (wakelock_fd < 0)
        return;

    if (hold) {
        (void)!write(wakelock_fd, "y1play\n", 7);
    } else {
        y1_sysfs_write("/sys/power/wake_unlock", "y1play\n");
    }
}

void y1_audio_enable_recording_path(enum y1_audio_rec_source source)
{
    if (eac_fd < 0)
        return;

    set_anareg(eac_fd, 0x106, 1, 0xffff);

    if (source == Y1_REC_SOURCE_LINE_FM) {
        set_anareg(eac_fd, 0x700, 0x0000, 0x0006); /* Mic biases off */
        set_anareg(eac_fd, 0x704, 0x0010, 0x0030); /* Line-in ADC mux */
        set_anareg(eac_fd, 0x710, 0x0008, 0x0008);
        set_anareg(eac_fd, 0x4014, 1, 1);
        top_ctl_trigger(eac_fd);
        return;
    }

    if (source == Y1_REC_SOURCE_HEADPHONE_MIC) {
        /* Headphone TRRS Mic: ACC_MIC input, MICBIAS1 enable */
        set_anareg(eac_fd, 0x700, 0x0004, 0x0006);
        set_anareg(eac_fd, 0x704, 0x0005, 0x000f);
    } else {
        /* Built-in Main Mic: MIC_P1 input, MICBIAS0 enable */
        set_anareg(eac_fd, 0x700, 0x0002, 0x0006);
        set_anareg(eac_fd, 0x704, 0x0000, 0x000f);
    }
    set_anareg(eac_fd, 0x700, 0x0100, 0x0100);
    set_anareg(eac_fd, 0x702, 0x1000, 0x1000);
    set_anareg(eac_fd, 0x710, 0x0008, 0x0008);
    set_anareg(eac_fd, 0x4014, 1, 1);
    top_ctl_trigger(eac_fd);
}

void y1_audio_disable_recording_path(void)
{
    if (eac_fd < 0)
        return;
    set_anareg(eac_fd, 0x700, 0x0000, 0x0106);
    set_anareg(eac_fd, 0x702, 0x0000, 0x1000);
    top_ctl_trigger(eac_fd);
}

void y1_audio_set_recording_pga_db(int gain_db)
{
    if (eac_fd < 0)
        return;
    uint32_t pga = (uint32_t)((gain_db / 6) & 0x7);
    set_anareg(eac_fd, 0x702, pga << 8, 0x0700);
    set_anareg(eac_fd, 0x700, pga << 4, 0x0070);
}

