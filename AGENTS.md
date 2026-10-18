# picoLCD repository reference

This fork combines the legacy Mini-Box USB LCD SDK with a Linux 20x2 panel
service for Kodi/RetroArch. This file describes the current implementation;
Git history contains the earlier implementation and deployment notes.

## Source map

| Area | Files |
| --- | --- |
| Panel loop, LCD rows, physical buttons, IR mappings, launcher | `src/panel.c` |
| Linux keyboard/mouse backend | `src/input.c`, `src/input.h` |
| Optional display worker and parent supervision | `src/display-probe.c`, `src/display-monitor.c` |
| Kodi/RetroArch metadata and playback status | `src/media.c`, `src/media.h` |
| Ortek keyboard/mouse decoding and movement | `lib/ortek.c`, `include/ortek.h` |
| HP RC6 and original Xbox DVD decoding | `include/rc6-mce.h`, `include/xbox-dvd.h` |
| Legacy USB driver, EEPROM/splash support | `lib/`, `src/picolcd-util.c` |
| Regression suite and signal fixtures | `build-check.sh`, `test-*.c`, `test-data/` |

## Build and verification

Build and run checks on Linux. Required: GCC, pkg-config, legacy libusb-0.1
development files (`libusb` pkg-config module), D-Bus development files
(`dbus-1`), json-c >= 0.15, pthreads, and `dbus-run-session`. X11/XRandR
development files (`xrandr` >= 1.3) enable the optional display helper and its
tests. libxdo is not required. NVML is loaded dynamically at runtime; no NVIDIA
SDK is needed. Current input and worker code uses Linux interfaces and GNU
libc APIs; legacy macOS material does not establish portability of this fork.

```sh
sh build-check.sh                              # expects CPU and NVML GPU sensors
PANEL_TEST_GPU=unavailable sh build-check.sh    # expects CPU, GPU unavailable
```

The second command is for hosts without usable NVML GPU temperature, such as
integrated-graphics systems; it asserts GPU reads fail. It is not a generic
sensor-test bypass. Hosts with working NVML GPU temperature use the default mode.
The script builds a private binary/library in
`build/`, runs ASan/UBSan regression tests and real sensor checks, and writes
`build/release.sha256`. Tests cover input ownership/failure handling, all 46
Ortek mappings, IR packet splits and capture replay, display overlays, media
parsing/timeouts, USB timeouts, mocked systemd launch and EEPROM rollback.
The suite does not access the real LCD, write its EEPROM, or launch RetroArch.
Legacy SDK warnings remain; keep new panel code free of warnings.

`build-check.sh` compiles but does not run the live uinput test. To verify real
kernel events, run `sudo ./build/test-input-live`; it grabs only its own newly
created devices and checks input with an unavailable X server. It requires
uinput and input-event access. For read-only live display sampling, use:

```sh
DISPLAY=:0 XAUTHORITY=/path/to/session/.Xauthority ./build/test-panel --display
```

Substitute the display and authority file for the active graphical session.
The diagnostic uses the sibling helper and does not access the LCD or change
display settings. `ldd build/picolcd` must not show X11, XRandR or libxdo;
`build-check.sh` enforces this. Autotools configuration is in `configure.in`
and `src/Makefile.am`; `sh autogen.sh` generates/configures the build before
`make`. The tested service installation path is the private build above.

## Input and process boundaries

Physical buttons and all supported IR remotes use `picoLCD Virtual Keyboard`
and `picoLCD Virtual Mouse` via Linux uinput. Key strings are parsed against
the bounded table in `src/input.c`; names such as `Return`, `ctrl+shift+t`
and `XF86AudioPlay` do not invoke libxdo or X keysym lookup. Unknown names are
rejected. Modifiers and mouse buttons are reference-counted; preserve shared
ownership so taps cannot release another input path's held keys/buttons.
Events use SYN_REPORT. Write failures destroy devices and trigger a two-second
retry. The root-run panel service supplies device access; do not loosen
system-wide input permissions to work around a service issue.

Input does not depend on DISPLAY, XAUTHORITY, a window manager or an X server.
Only the optional `picolcd-display-probe` links X11/XRandR. It is found beside
the running executable, samples every 500ms, and defaults to DISPLAY `:0` and
XAUTHORITY `/home/kodi/.Xauthority` unless supplied in its environment. The
parent consumes its pipe without waiting for X, retries after two seconds,
and kills workers that stop reporting for three seconds. Workers inherit
standard descriptors only, not USB/uinput/D-Bus descriptors. Losing X or the
helper leaves the main process and input alive; display sampling becomes
unavailable. Avoid reintroducing Xlib into the parent or input backend.

`numbersign` follows `/etc/default/keyboard`: GB uses the native hash-key
position, other layouts use US Shift+3. `PICOLCD_KEYBOARD_LAYOUT=gb` or `us`
overrides selection. Ordinary digits use the number row, independent of NumLock.

Both physical button slots are tracked; duplicate reports and unknown IDs are
ignored. F1 (button ID 3) requests `retroarch.service` on release using system
D-Bus StartUnit, asynchronously with a two-second reply timeout. Held panel
keys and Ortek mouse buttons release before the request. No shell command is
used for launching; the optional display helper is a separate spawned process.
The service manager determines application switching and RetroArch's user.

## IR reference

All decoders consume signed little-endian 16-bit picoLCD durations and preserve
partial frames across USB reports. They run independently from the same IR
events. Preserve timing/polarity/address/checksum checks and repeat suppression.
No LIRC daemon or separate remote-specific receiver is required for these paths.
There is no implemented wake-from-off support.

### Ortek VRC-1100

`picolcd-ortek.map` supplies 46 entries for `/etc/picolcd-ortek.map`. Each line
is a decimal command (0-63), repeat flag (0/1), and supported key string or
`mouse:1`/`mouse:3`. Up (21), Down (32) and Enter (34) are built-in fallbacks.
Power (3) sends `s` for Kodi's power menu. The USB receiver `05a4:9881` is
checked once per second: its presence suppresses only Ortek LCD input to avoid
duplicates, including mouse events. HP/Xbox input remains enabled.

Keyboard: about 2ms leader mark, 1ms space, 16 LSB-first Manchester bits,
address 10, press/hold/release phase, six command bits, checksum
`4 + popcount(low 12 bits)`. Repeatable keys wait 400ms then emit at most
every 100ms; release ends the press and gaps over 300ms start a fresh press.
Mouse: same leader with about 480us header space, 16 Manchester bits, low
five bits 20, two button bits, five movement bits, checksum
`3 + popcount(low 12 bits)`. Direction is zero for stop or 16+angle, with
16 angles clockwise from up; the final idle space may be omitted.

Mouse movement uses raw relative input, paced at 24ms. Cardinal steps are
2, 4, 6 and 8 units with gain transitions at 0.696, 0.992 and 1.288 seconds.
The old threefold X-warp compensation is gone; screen movement follows system
pointer handling. Stop/gaps reset acceleration; a 200ms watchdog stops motion
and releases buttons. Dragging is supported. Buttons also release on orderly
exit, launch and dongle takeover. Active mouse USB reads use 12ms timeouts,
otherwise 200ms. `sudo python3 learn-ortek.py --output /path/to/learned-ortek.map`
collects three minutes of LCD journal commands and USB events on a Linux host
with the panel service and Ortek USB receiver. Output defaults to
`learned-ortek.map` in the working directory; it is never installed automatically.
Review results; ambiguous/conflicting pairs are omitted.

### HP TSGH-IR07 (RC6 MCE)

Mode 6A, customer/device `0x800f04`, 444us half bits and a double-width trailer.
The decoder compensates for about 75us mark extension and completes the final
idle half-bit omitted by firmware. It checks Manchester pairs, header, address,
timing and stale fragments. The fixture contains 18 frames for arrows, OK,
Back, Play and Pause with both toggle states. Other mappings use standard MCE
codes and do not have the same captured-button evidence.

Mappings in `hp_received` include arrows, Enter, Escape, playback/seek/skip,
digits, volume/mute, information, guide and Home; Power sends `s`. Arrows,
volume/channel and forward/rewind repeat after 400ms, at most every 100ms.
Toggle changes recognize fresh taps immediately; playback toggles and Power
do not repeat while held. Unmapped commands log without injecting input.

### Original Xbox DVD remote

RCA DVD signal: 4ms leader mark/space, 24 MSB-first bits, 500us marks,
1ms/2ms spaces (one/zero), final mark. A 12-bit address/command is followed
by its complement; address `0xa` is required. The decoder checks the final
mark and complement, tolerates observed mark extension, and expires partial
frames after 300ms. The `test-data/xbox-dvd-capture.log` fixture contains
183 USB packets / 35 frames for
Up, Down, Left, Right, Select, Back, Play and Stop; other mappings follow
the referenced Linux keymap.

Mappings in `xbox_received`: arrows, Select=Enter, Back=Escape, Display=`i`,
Play=`p`, Pause=Space, Stop=`x`, Rewind=`r`, Forward=`f`, previous/next track,
digits, Menu=`m`, Title=`c`, Angle=`z`. The last three are Kodi menu/context/
aspect mappings. Arrows repeat after 400ms, at most every 100ms; other keys
act once while held. A different command or gap over 180ms starts a new
press. There is no toggle bit; very rapid identical taps may merge.

Protocol/code references (implementation and fixtures remain authoritative):

- [Ortek protocol](https://github.com/peterhinch/micropython_ir/blob/master/ir_rx/mce.py)
- [Linux RC6 decoder](https://github.com/torvalds/linux/blob/master/drivers/media/rc/ir-rc6-decoder.c)
- [Linux MCE keymap](https://github.com/torvalds/linux/blob/master/drivers/media/rc/keymaps/rc-rc6-mce.c)
- [Linux Xbox DVD keymap](https://github.com/torvalds/linux/blob/master/drivers/media/rc/keymaps/rc-xbox-dvd.c)

## LCD, media and sensors

Top row: RetroArch loaded game takes precedence over Kodi playback title;
otherwise show the date/time. Paused content retains its title. Episode,
music and movie formatting is in `src/media.c`, with title/label/path fallbacks.
Long text scrolls every 400ms with a three-space gap; content changes reset
scrolling. Non-ASCII characters become `?`. Rows are padded to 20 characters
and sent only when changed; bottom-row text is centred.

Bottom row: Kodi volume/mute and display-mode overlays last three seconds;
volume takes priority and defers the latest mode popup. Playback shows time,
pause or live state; idle/RetroArch shows temperatures. The first volume and
display samples are silent. Display selection prefers an active primary,
then a connected active output, and handles fractional/interlaced/double-scan
timings without changing settings. `/run/picolcd-display-mode` stores the last
mode and unfinished popup across abrupt panel exits; normal shutdown removes
it and boot clears `/run`. Worker/X loss alone does not restart the panel.

Media sockets run in a background thread with 500ms operation deadlines,
using loopback Kodi TCP 9090 and RetroArch UDP 55355. Kodi notifications
trigger metadata refreshes, with a ten-second fallback (two seconds without
the notification connection); time advances from a monotonic sample and
freezes while paused. RetroArch status is queried about once per second.
RetroArch requires `network_cmd_enable = "true"` and port 55355; enabling its
command interface affects the application's listeners, while this panel sends
read-only queries to loopback. Do not change host app settings implicitly.

CPU temperature uses `x86_pkg_temp` thermal-zone discovery. NVML supplies GPU
temperature when available; otherwise aggregate `/proc/stat` CPU usage is
shown, excluding idle/I/O wait. Failed/initial readings show placeholders.
Normal sensor sampling is every five seconds even while hidden by playback;
invalid usage counters retry after one second. USB errors exit unsuccessfully
for service recovery. SIGTERM/SIGINT/SIGQUIT release input and free resources.

## Installation and rollback

On the target host, build and check first, then run `sudo sh install-panel.sh`
from that source tree. Compile separately for each host's libc/libraries.
The installer verifies `build/release.sha256`, loads uinput and writes
`/etc/modules-load.d/picolcd-uinput.conf`. It saves the old executable link and
service description under `/var/backups/picolcd-panel-TIMESTAMP`, installs a
private binary/library and optional helper under
`/usr/local/lib/picolcd-panel/TIMESTAMP`, atomically switches
`/usr/local/bin/picolcd`, and restarts only `panel.service`. It checks activation
and restores the old executable if activation fails. It retains existing
service configuration and the shared system libpicolcd.

Inspect actual state with `readlink -f /usr/local/bin/picolcd`,
`systemctl status panel.service`, `systemctl cat panel.service` and
`journalctl -u panel.service`; old release timestamps are not current-state
evidence. For rollback, use the specific backup printed by the installer:

```sh
sudo systemctl stop panel.service
sudo cp -a /var/backups/picolcd-panel-TIMESTAMP/picolcd /usr/local/bin/.picolcd-restore
sudo mv -Tf /usr/local/bin/.picolcd-restore /usr/local/bin/picolcd
sudo systemctl start panel.service
```

`install-display-recovery.sh` separately backs up/installs a service drop-in
with RestartSec=1, StartLimitIntervalSec=60 and StartLimitBurst=20, then reloads
systemd and clears failed state. It is not needed to make input survive X loss.
Automated/mock tests do not prove physical LCD appearance or remote behavior;
verify relevant hardware behavior after deployment.

## Persistent power-on splash

`cat-splash.txt`: centre (2s), left (1s), blink (1s), right (1s), centre (1s),
with backlight enabled. `sudo sh install-cat-splash.sh` uses the checked staged
binary, stops the panel, backs up all 256 EEPROM bytes under
`/var/backups/picolcd-splash-XXXXXXXX/eeprom.bin`, writes/verifies the splash,
and restarts the service only if previously active. It does not install a
panel release. The writer bounds parsing, serializes five 47-byte records,
checks acknowledgements/read-back, preserves bytes outside the 235-byte splash
area and restores the backup on write failure. Identical data is not rewritten;
the firmware-programming interface is not used.

Keep the backup and cycle USB power to verify persistence/appearance; an OS
reboot may leave USB powered. To restore, stop the panel, run the staged
`sudo ./build/picolcd eeprom-restore /var/backups/picolcd-splash-XXXXXXXX/eeprom.bin`,
then restore the service's previous running state.
