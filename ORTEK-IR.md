# VRC-1100 remote through the picoLCD receiver

The panel supports the VRC-1100's keyboard and mouse IR reports. It uses the
existing X11/libxdo connection for input. No LIRC daemon is required. The
USB 05a4:9881 receiver uses hid-generic; its firmware handles IR decoding.

## Configuration

Install tinybox-ortek.map as /etc/picolcd-ortek.map and restart panel.service.
It supplies 46 mappings learned from simultaneous LCD IR and USB HID captures.
Each line is a decimal command, repeat flag (0/1), and libxdo key sequence:

    3 0 s
    21 1 Up
    32 1 Down
    34 0 Return

Power (3) sends S, which opens Kodi's ShutdownMenu with the verified stock
keymap. Digits use ordinary number keys, independent of NumLock. The # key
sends numbersign instead of the dongle's Windows Alt+35 macro. mouse:1 and
mouse:3 mappings generate mouse clicks. Up/Down/OK are built-in fallbacks.

While USB receiver 05a4:9881 is connected, the LCD decodes and logs commands
but suppresses input injection to prevent doubled actions. Unplug the dongle
to use LCD input; reconnecting it restores USB input within about a second.

## Keyboard protocol

Keyboard reports have a roughly 2ms mark, 1ms space and a 16-bit LSB-first
Manchester payload. The observed address is 10, followed by press/hold/release
phase and six command bits. The checksum is 4 + popcount(low 12 bits).
The leading fixed bit is implicit in this representation. Reference:
https://github.com/peterhinch/micropython_ir/blob/master/ir_rx/mce.py

The decoder preserves partial frames across USB reports and validates timing,
polarity, address, phase and checksum. Key taps cannot leave a key held.
Repeatable mappings wait 400ms, then repeat at most every 100ms. Toggle actions
such as Power, play/pause and mute do not repeat during a held press.

learn-ortek.py pairs panel journal commands with the Ortek USB receiver's
input events using their original realtime timestamps. It runs for three
minutes as root, writes /home/kodi/ir-signal-test/learned-ortek.map and never
installs it. Keep presses separated and review the output. Ambiguous or
conflicting pairs are omitted. It is a diagnostic tool for the Tinybox setup.

## Mouse protocol

The mouse report format was inferred from synchronized raw IR/USB recordings.
It has the same leader but a single 480us header space and 16 Manchester bits:
low five bits fixed at 20, two button-state bits, a five-bit movement field,
and checksum 3 + popcount(low 12 bits). Movement is 0 for stop or 16+angle,
with 16 angles clockwise from up. The final idle space may be omitted.
Keyboard and mouse reports use separate decoders.

Movement is paced at 24ms with direction ratios matched to USB reports.
After live feedback, displacement is scaled threefold: cardinal movement
starts at 6 pixels per step and accelerates to 12, 18 and 24 at approximately
0.696, 0.992 and 1.288 seconds. Stop/gaps reset acceleration. Both buttons
support down/up and dragging. A 200ms packet-loss watchdog stops movement
and releases buttons. Buttons also release on orderly exit, game-launch X
teardown and USB-dongle takeover. Active mouse reads use 12ms timeouts; idle
reads retain the existing 200ms timeout.

## Validation and deployment

build-check.sh runs sanitizer tests for all keyboard commands and phases,
all mouse directions and button combinations, packet splits, bad checksums,
real capture replay, repeat suppression, pacing, acceleration, normal/lost
releases, all 46 mapping dispatches, dongle suppression and cleanup. It also
runs the existing panel regression suite. Tinybox requires
PANEL_TEST_GPU=unavailable because its GPU temperature API is unavailable.

Fixtures in test-data are the captured IR timings and USB reference events.
The keyboard capture contains five Up, five Down and five OK presses. The
mouse capture contains 146 valid reports. Live LCD-only operation was tested;
the user confirmed the mouse speed adjustment works well.

The installed Tinybox executable is in
/usr/local/lib/picolcd-panel/20261018-153532. Sources/build logs are in
/home/kodi/ortek-panel-review. The keyboard-only release is preserved by the
installer at /var/backups/picolcd-panel-20261018-153320/picolcd; the immediately
preceding slower mouse release is at
/var/backups/picolcd-panel-20261018-153532/picolcd. The original pre-IR executable
link is at /var/backups/picolcd-panel-20261018-151940/picolcd.

To roll back, stop panel.service, restore the chosen saved executable link
with cp -a to a temporary link in /usr/local/bin, atomically rename it to
/usr/local/bin/picolcd, then start panel.service. The Power mapping backup is
/home/kodi/ir-signal-test/ortek-map-before-power.

## Native input migration, 18 October 2026

The X11 injection architecture described above has been replaced by native
uinput keyboard and mouse devices. See UINPUT.md for current architecture,
validation, deployed releases and rollback notes. The 46-button map is unchanged.
The main panel process has no X11/libxdo linkage; resolution sampling is isolated
in an optional helper. Mouse speed now uses raw USB-equivalent steps through
the normal system input stack, removing the former X-warp compensation.
