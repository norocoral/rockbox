🌐 **한국어** | [English](README.md)

# `liby1` — Innioasis Y1 하드웨어 추상화 레이어

`liby1`은 **Innioasis Y1** 디지털 오디오 플레이어의 모든 하드웨어 드라이버, 미디어텍 MT6572 / MT6323 PMIC 저수준 제어, 커널 인터페이스를 하나로 집약한 외부 의존성 제로의 독립형 경량 C 라이브러리입니다.

개발자는 커널 ioctl, PMIC 레지스터, sysfs 경로를 일일이 분석하거나 역공학할 필요 없이 Y1을 위한 독립형 소프트웨어, 게임, 에뮬레이터, 커스텀 오디오 플레이어를 쉽게 제작할 수 있습니다. Rockbox 역시 내부적으로 `liby1`을 기반으로 작동합니다.

> [!TIP]
> 전체 API 레퍼런스, 코드 예제 및 아키텍처 분석은 **[`liby1` 개발자 매뉴얼](../docs/kr/LIBY1_GUIDE.md)** ([English](../docs/LIBY1_GUIDE.md))에서 확인하실 수 있습니다.

---

## SD 카드 `.system` 디렉터리 구조

모든 시스템, 하드웨어, 라이브러리 및 진단 파일은 `/mnt/sdcard/.system/`(`/.system`에 심볼릭 링크됨)에 저장되어, 애플리케이션 디렉터리(예: `/mnt/sdcard/.rockbox`)를 깨끗하게 격리합니다:

| 경로 | 설명 |
| :--- | :--- |
| `/mnt/sdcard/.system/lib/liby1.so` | `liby1` 동적 공유 하드웨어 추상화 라이브러리 |
| `/mnt/sdcard/.system/lib/liby1.a` | 오프라인/기기 내 정적 링크를 위한 `liby1` 정적 라이브러리 |
| `/mnt/sdcard/.system/include/liby1.h` | `liby1` 공개 C 헤더 파일 |
| `/mnt/sdcard/.system/lib/liby1video.so` | H.264 / AAC NEON 하드웨어 비디오 가속 디코더 |
| `/mnt/sdcard/.system/bin/` | 시스템 실행 파일 (예: `ffmpeg`) |
| `/mnt/sdcard/.system/bt/` | MT6572 블루투스 커널 모듈(`mtk_stp_*.ko`) 및 펌웨어 |
| `/mnt/sdcard/.system/bt_paired.dat` | 페어링된 블루투스 기기 데이터베이스 |
| `/mnt/sdcard/.system/ramdisk_android.cpio.gz` | 부트로더 메뉴 듀얼 부팅용 순정 안드로이드 램디스크 |
| `/mnt/sdcard/.system/*.raw` | 480x360 BGRA 하드웨어 스플래시 및 부트로더 메뉴 화면 |
| `/mnt/sdcard/.system/y1_debug.txt` | 시스템 및 하드웨어 진단 로그 |
| `/mnt/sdcard/.system/app` | 부팅 시 즉시 실행 가능한 선택적 독립형 커스텀 애플리케이션 |

---

## `liby1` 제공 서브시스템

1. **코어, Sysfs, 로깅, 배터리 및 전원 (`y1_core.c`)**
   - `y1_init()`, `y1_shutdown()`
   - `y1_battery_percent()`, `y1_battery_voltage_mv()`, `y1_battery_current_ma()`, `y1_battery_is_charging()`, `y1_power_usb_online()`
   - `y1_log()`, `y1_logf()`, `y1_sys_poweroff()`, `y1_sys_reboot()`
2. **디스플레이 및 백라이트 (`y1_display.c`)**
   - `y1_fb_init()`, `y1_fb_present_rgb565()`, `y1_fb_present_xrgb8888()`, `y1_fb_blit_rgb565_rect()`
   - `y1_fb_blit_rgb565_classic_scaled()` (매끄러운 320x240 -> 480x360 2x2->3x3 면적 평균 하드웨어 블릿)
   - `y1_fb_display_raw_screen()`
   - `y1_backlight_set_percent()`, `y1_backlight_set_raw()`
   - `y1_display_enter_early_suspend()` / `y1_display_exit_early_suspend()` (`mem` 절전 모드 시 LCD 클록, VSync, 터치, Mali GPU 전원 차단)
3. **CPU 주파수 및 듀얼코어 핫플러그 (`y1_cpu.c`)**
   - `y1_cpu_set_mode()` (`DYNAMIC`, `POWERSAVE` 598MHz, `BALANCED` 1.0GHz, `PERFORMANCE` 1.2GHz, `TURBO` 1.3GHz)
   - `y1_cpu_set_dual_core()`, `y1_cpu_set_screen_off_clamp()`, `y1_cpu_get_freq_mhz()`
4. **입력: 클릭 휠, 버튼 및 3.5mm 이어폰 단자 (`y1_input.c`)**
   - `y1_input_init()`, `y1_input_poll()`, `y1_input_map_keycode()`, `y1_input_set_touch_scanning()`
   - `y1_jack_is_inserted()`, `y1_jack_has_mic()`, `y1_jack_get_raw_state()`
5. **오디오 DAC, 헤드폰 앰프 및 마이크 ADC (`y1_audio.c`)**
   - `y1_audio_open()`, `y1_audio_set_sample_rate()` (8kHz - 48kHz), `y1_audio_set_hp_analog_gain()` (`MT6323_AUDTOP_CON5`의 0..7)
   - `y1_audio_write_pcm()` (4바이트 스테레오 프레임 정렬 DMA 링 버퍼 쓰기)
   - `y1_audio_enable_recording_path()` (내장 마이크, 4극 헤드셋 마이크, 라인 입력/FM)
   - `y1_pmic_read_reg()`, `y1_pmic_write_reg()`, `y1_afe_write_reg()`
6. **FM 라디오 튜너 (`y1_tuner.c`)**
   - `y1_fm_power()`, `y1_fm_tune()`, `y1_fm_set_mute()`, `y1_fm_get_rssi()`, `y1_fm_is_tuned()`
7. **USB 대용량 저장소 가젯 (`y1_usb.c`)**
   - `y1_usb_attach_mass_storage()`, `y1_usb_detach_mass_storage()`, `y1_usb_check_host_ejected()`

---

## `liby1`을 활용한 독립형 C 프로그램 빌드

```bash
# liby1.a, liby1.so 및 hello_y1 예제 빌드
make -C hosted_linux_port/liby1 CROSS_COMPILE=arm-linux-gnueabihf-

# liby1 정적 라이브러리와 링크하여 직접 작성한 C 프로그램 컴파일
arm-linux-gnueabihf-gcc -O2 -Ihosted_linux_port/liby1/include \
    my_app.c hosted_linux_port/liby1/build/liby1.a -o my_app
```

---

## 관련 기술 문서
- [liby1 개발자 매뉴얼 (한국어)](../docs/kr/LIBY1_GUIDE.md)
- [liby1 Complete Developer Manual (English)](../docs/LIBY1_GUIDE.md)
- [모듈식 빌드 시스템 가이드 (한국어)](../docs/kr/BUILD_GUIDE.md)
- [Modular Build System Guide (English)](../docs/BUILD_GUIDE.md)

