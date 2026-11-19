🌐 [한국어](krREADME.md) | **English**

# Rockbox for Innioasis Y1 (Hosted Linux)

Hosted Rockbox port targeting the Innioasis Y1 (`innioasis/y1`) running on minimal Linux. Hardware access (framebuffer display, `/dev/eac` audio, input devices, power/wakelock, FM radio, and USB mode switching) is handled through `vendor/liby1`.

---

## Button Controls

| Control | Action | Function |
| :--- | :--- | :--- |
| Click Wheel | Rotate | Scroll lists / adjust volume in WPS |
| Center Button | Short Press | Select / activate menu item |
| Center Button | Long Press (1s) | Open Context Menu for active track/file |
| Center Button | Long Press (1s on USB screen) | Eject Mass Storage and switch to Charge-Only mode |
| Menu Button | Short Press | Return to parent menu / back (120ms release threshold) |
| Menu Button | Double-Click | Toggle Key Lock (HOLD mode) on / off |
| Menu Button | Long Press (280ms) | Open Rockbox Quick Screen settings |
| Play Button | Short Press | Play / Pause toggle |
| Play Button | Long Press | Stop playback |
| Left Button | Short Press | Rewind to track start if $\ge 3\text{s}$; skip to previous track if $< 3\text{s}$ |
| Left Button | Long Press | Seek backward |
| Right Button | Short Press | Skip to next track |
| Right Button | Long Press | Seek forward |
| Headphone Remote (MFB) | In-line Clicks | Single, double, triple clicks & hold actions (configurable in Settings) |

---

## HOLD & Backlight Subsystem

- **Toggling HOLD (Lock / Unlock):**
  - Double-clicking **MENU** toggles HOLD mode.
  - Double-click detection uses a 350ms window when locked; single-click MENU fires after a 120ms window when unlocked.
- **When Locked (HOLD Active):**
  - **Button Protection:** Wheel and button inputs are blocked unless exempted via *Advanced Key Lock* (Volume, Play/Pause, or Track Skip).
  - **Hold Light Setting:**
    - `Off` *(Default)*: Button presses and wheel movement are ignored; the display remains off.
    - `On` *(or Brightness %)*: Pressing a button wakes the backlight without triggering playback commands.
  - **Backlight on Lock Timeout:** Configurable up to 5 hours (`Off`, `Always On`, `1s`–`45s`, `1m`–`45m`, `1h`–`5h`).
  - **Backlight on Lock Rules:** `Always`, `Playing only`, or `WPS only`.
- **When Unlocked (Normal Operation):**
  - **First Keypress Enables Backlight Only:**
    - `No` *(Default)*: Pressing a button while the backlight is off executes the command and turns on the backlight.
    - `Yes`: The first button press while dark only turns on the backlight; subsequent presses execute commands.
  - **Backlight Idle Timeout:** Controlled by the standard Rockbox Backlight Timeout setting.

---

## Building Rockbox (Target Only)

Requires `arm-linux-gnueabihf-gcc` and standard Rockbox build dependencies (`perl`, `python3`, `zip`):

```bash
mkdir -p build_y1 && cd build_y1
../tools/configure --target=innioasisy1 --type=N
make -j$(nproc)
make zip
```

---

## Credits
https://github.com/nuxcodes/rockpod

## License
Built on the open-source [Rockbox](https://www.rockbox.org) project under the GNU General Public License v2 (GPL-2.0). Not affiliated with Innioasis.
