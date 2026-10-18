# Native Linux input for picoLCD

Remote IR and physical panel buttons now use two uinput devices:
`picoLCD Virtual Keyboard` and `picoLCD Virtual Mouse`. The main panel binary
links neither libxdo nor X11/XRandR, and input does not depend on an X server,
DISPLAY, XAUTHORITY or a window-manager session. Linux uinput access is required;
the existing root-run panel service supplies it. The installer ensures uinput
is loaded and writes /etc/modules-load.d/picolcd-uinput.conf for boot.

The existing 46-button /etc/picolcd-ortek.map format is retained. Familiar
names such as Return, ctrl+shift+t and XF86AudioPlay are parsed by our bounded
keycode table; they no longer invoke libxdo or X keysym lookup. Unknown names
are rejected by the backend. The keyboard device advertises only supported
keycodes. Modifiers and mouse buttons are reference-counted so an IR tap
cannot release a key/button already held by the panel or mouse-pad path.
Each press/release/movement is framed with SYN_REPORT. Input write failures
cause device destruction and a bounded retry rather than a stuck held input.

Power still maps command 3 to S, opening Kodi's power menu. The # key follows
the OS layout: /etc/default/keyboard selects the GB key position or US Shift+3.
PICOLCD_KEYBOARD_LAYOUT=gb/us can override that selection. Both deployed boxes
were checked to use GB. Ordinary letter shortcuts and digits retain their
native key positions. The previous threefold X-warp speed compensation is
removed: raw mouse steps now match the USB receiver and pass through the normal
system pointer handling. Existing direction acceleration and the 200ms
lost-release watchdog remain, including button holds and dragging.

The optional `picolcd-display-probe` companion retains the resolution/refresh
LCD popup. It alone links X11/XRandR. The parent reads its pipe without waiting
for X, restarts failed workers, and kills a worker that stops reporting for
three seconds. Closing or losing X cannot terminate the parent or destroy its
input devices. The worker inherits only standard descriptors, not USB, uinput
or D-Bus descriptors. Without the helper or XRandR libraries, input, media,
temperature, physical buttons and game launching continue; resolution sampling
is simply unavailable. Autotools and build-check.sh build this helper only
when XRandR development files are available.

Validation includes the full existing sanitizer/regression suite, all 46 map
translations, malformed chords, event order, shared modifiers/buttons, drag,
input-write failure handling, and real isolated kernel event delivery. The
live test grabs only the virtual devices it creates, so its test shortcuts
never reach Kodi. It also proves input works with DISPLAY pointing at a
nonexistent X server. The production helper was separately terminated on each
machine: both panel PIDs survived without restarts, and their input devices
remained present. The live --display diagnostic still works through the worker.

Deployed 18 October 2026:

- Tinybox: /usr/local/lib/picolcd-panel/20261018-174300
- Minibox: /usr/local/lib/picolcd-panel/20261018-164405

Minibox was compiled locally from the tested Tinybox sources to match its own
libc and other system libraries. Both staging trees are
/home/kodi/picolcd-uinput-review. Both services were active with NRestarts=0.
Existing service units, backlight settings, Kodi, RetroArch and the 46-button
map were preserved. No OS-wide input permissions were loosened.

Each installer backup is under /var/backups/picolcd-panel-<release timestamp>.
Tinybox's original pre-migration release is 20261018-153532 (first migration
backup: 20261018-173920); its final backup points to an earlier uinput build.
Minibox's 20261018-164405 backup points to the previous X11-based 20261018-161042
release. Restore the chosen saved executable symlink while panel.service is
stopped, then start the service. The modules-load file is harmless if rolling
back to the X11 input version.
