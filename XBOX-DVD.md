# Original Xbox DVD remote through the picoLCD receiver

Tinybox's built-in LCD IR receiver captured this remote without an Xbox dongle.
The fixture test-data/xbox-tinybox-capture.log contains all 183 USB IR packets
from the initial capture: 35 complete valid frames for Up, Down, Left, Right,
Select, Back, Play and Stop, in that order. These eight commands match Linux's
Xbox DVD keymap. Other button codes follow that keymap and await physical tests.

include/xbox-dvd.h decodes the RCA DVD signal: 4ms leader mark and space,
24 MSB-first bits with 500us marks and 1ms/2ms spaces (one/zero), final mark,
12-bit address/command and its complement. Address 0xa is required. Timing,
polarity, complement and trailer are checked; partial frames expire after 300ms.
It tolerates the mark extension observed through the LCD demodulator. Decoder
state survives USB report boundaries. Existing HP and Ortek decoders run independently.

Arrows repeat after 400ms, at most once every 100ms. Other keys do not repeat
while held. A gap over 180ms or a different command starts a new press. This
remote has no toggle bit, so extremely rapid identical taps can merge.

Mappings: arrows, Select=Enter, Back=Escape, Display=information, Play=P,
Pause=Space, Stop=X, Rewind=R, Forward=F, skip=previous/next track, digits.
Menu=M (Kodi playback menu), Title=C (context menu), Angle=Z (zoom/aspect).
Those last three are intentional Kodi mappings. No power/wake command is added.

The ASan/UBSan test covers every command at each USB packet split, ideal and
distorted timing, stale/malformed frames, wrong address, bad complement, missing
trailer, repeat behavior, all 35 captured frames, and rejection of HP/Ortek captures.
The full existing build-check.sh suite passed on tinybox (GPU unavailable).

Tinybox installed from /home/kodi/picolcd-xbox-review into
/usr/local/lib/picolcd-panel/20261018-194700. Rollback executable:
/var/backups/picolcd-panel-20261018-194700/picolcd.
Service active, uinput keyboard/mouse created. Physical Kodi response is pending
user confirmation.

Minibox also passed the full suite, including real CPU/GPU checks, and installed
from /home/kodi/picolcd-xbox-review into
/usr/local/lib/picolcd-panel/20261018-184748. Rollback executable:
/var/backups/picolcd-panel-20261018-184748/picolcd.
Service active, uinput keyboard/mouse created; physical response pending.

Reference for button codes:
https://github.com/torvalds/linux/blob/master/drivers/media/rc/keymaps/rc-xbox-dvd.c
