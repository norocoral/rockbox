🌐 [한국어](krREADME.md) | **English**

# `liby1` — Innioasis Y1 Hardware Abstraction Layer

`liby1` is a tiny, zero-dependency C library that encapsulates all hardware drivers, low-level MediaTek MT6572 / MT6323 PMIC operations, and kernel interfaces for the **Innioasis Y1** digital audio player.

It enables C/C++ developers to write standalone software, games, emulators, or alternative media players for the Y1 without reverse-engineering kernel ioctls, PMIC registers, or sysfs paths. Rockbox itself uses `liby1` under the hood.

> [!TIP]
> For the complete API reference, code examples, and architecture breakdown, read the **[`liby1` Developer Manual & Guide](../docs/LIBY1_GUIDE.md)** ([한국어](../docs/kr/LIBY1_GUIDE.md)).

---

## SD Card `.system` Directory Layout

All system, hardware, library, and debug files live in `/mnt/sdcard/.system` (symlinked at `/.system`), keeping application directories (like `/mnt/sdcard/.rockbox`) completely clean:

| Path | Description |
| :--- | :--- |
| `/mnt/sdcard/.system/lib/liby1.so` | Shared `liby1` hardware abstraction library |
| `/mnt/sdcard/.system/lib/liby1.a` | Static `liby1` archive for offline/on-device linking |
| `/mnt/sdcard/.system/include/liby1.h` | Public C header for `liby1` |
| `/mnt/sdcard/.system/lib/liby1video.so` | H.264 / AAC NEON hardware video decoder library |
| `/mnt/sdcard/.system/bin/` | System binaries (e.g. `ffmpeg`) |
| `/mnt/sdcard/.system/bt/` | MT6572 Bluetooth kernel modules (`mtk_stp_*.ko`) & firmware |
| `/mnt/sdcard/.system/bt_paired.dat` | Persistent Bluetooth paired device database |
| `/mnt/sdcard/.system/ramdisk_android.cpio.gz` | Stock Android ramdisk for dual-booting from Bootloader Menu |
| `/mnt/sdcard/.system/*.raw` | 480x360 BGRA hardware splash & bootloader menu screens |
| `/mnt/sdcard/.system/y1_debug.txt` | System and hardware diagnostic log |
| `/mnt/sdcard/.system/app` | Optional standalone custom C application launched at boot |

---

## Subsystems Provided by `liby1`

1. **Core, Sysfs, Logging, Battery & Power (`y1_core.c`)**
   - `y1_init()`, `y1_shutdown()`
   - `y1_battery_percent()`, `y1_battery_voltage_mv()`, `y1_battery_current_ma()`, `y1_battery_is_charging()`, `y1_power_usb_online()`
   - `y1_log()`, `y1_logf()`, `y1_sys_poweroff()`, `y1_sys_reboot()`
2. **Display & Backlight (`y1_display.c`)**
   - `y1_fb_init()`, `y1_fb_present_rgb565()`, `y1_fb_present_xrgb8888()`, `y1_fb_blit_rgb565_rect()`
   - `y1_fb_blit_rgb565_classic_scaled()` (smooth 320x240 -> 480x360 2x2-to-3x3 area-averaged hardware blit)
   - `y1_fb_display_raw_screen()`
   - `y1_backlight_set_percent()`, `y1_backlight_set_raw()`
   - `y1_display_enter_early_suspend()` / `y1_display_exit_early_suspend()` (powers down LCD clocks, VSync, touch, and Mali GPU in `mem` sleep state)
3. **CPU Frequency & Dual-Core Hotplug (`y1_cpu.c`)**
   - `y1_cpu_set_mode()` (`Y1_CPU_MODE_DYNAMIC`, `POWERSAVE` 598MHz, `BALANCED` 1.0GHz, `PERFORMANCE` 1.2GHz, `TURBO` 1.3GHz)
   - `y1_cpu_set_dual_core()`, `y1_cpu_set_screen_off_clamp()`, `y1_cpu_get_freq_mhz()`
4. **Input, Click-Wheel & 3.5mm Headphone Jack (`y1_input.c`)**
   - `y1_input_init()`, `y1_input_poll()`, `y1_input_map_keycode()`, `y1_input_set_touch_scanning()`
   - `y1_jack_is_inserted()`, `y1_jack_has_mic()`, `y1_jack_get_raw_state()`
5. **Audio DAC, Headphone Amp & Microphone ADC (`y1_audio.c`)**
   - `y1_audio_open()`, `y1_audio_set_sample_rate()` (8kHz - 48kHz), `y1_audio_set_hp_analog_gain()` (0..7 in `MT6323_AUDTOP_CON5`)
   - `y1_audio_write_pcm()` (4-byte stereo frame-aligned DMA ring buffer writer)
   - `y1_audio_enable_recording_path()` (Internal Mic, TRRS Headset Mic, FM/Line-In)
   - `y1_pmic_read_reg()`, `y1_pmic_write_reg()`, `y1_afe_write_reg()`
6. **FM Radio Tuner (`y1_tuner.c`)**
   - `y1_fm_power()`, `y1_fm_tune()`, `y1_fm_set_mute()`, `y1_fm_get_rssi()`, `y1_fm_is_tuned()`
7. **USB Mass Storage Gadget (`y1_usb.c`)**
   - `y1_usb_attach_mass_storage()`, `y1_usb_detach_mass_storage()`, `y1_usb_check_host_ejected()`

---

## Building Standalone C Programs with `liby1`

```bash
# Build liby1.a, liby1.so, and examples/hello_y1
make -C hosted_linux_port/liby1 CROSS_COMPILE=arm-linux-gnueabihf-

# Compile your own C program against liby1
arm-linux-gnueabihf-gcc -O2 -Ihosted_linux_port/liby1/include \
    my_app.c hosted_linux_port/liby1/build/liby1.a -o my_app
```

---

## Related Documentation
- [liby1 Complete Developer Manual (English)](../docs/LIBY1_GUIDE.md)
- [liby1 개발자 매뉴얼 (한국어)](../docs/kr/LIBY1_GUIDE.md)
- [Modular Build System Guide (English)](../docs/BUILD_GUIDE.md)
- [모듈식 빌드 시스템 가이드 (한국어)](../docs/kr/BUILD_GUIDE.md)
