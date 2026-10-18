# HP TSGH-IR07 through the picoLCD receiver

Tinybox receives this remote through its LCD without an HP USB receiver.
The capture in test-data/hp-rc6-capture.log contains 18 valid MCE frames:
Up, Down, Left, Right, OK, Back, Play and Pause, with both toggle states.

include/rc6-mce.h decodes mode 6A, customer/device code 0x800f04, 444us half
bits and the double-width trailer. It compensates for approximately 75us
mark extension, preserves frames across USB reports and checks Manchester
pairs, timing, header, address and stale fragments. Firmware omits the final
idle space, which is completed when the final mark ends.

HP and Ortek decoders run independently using native uinput. Ortek dongle
suppression does not disable HP. Arrows, volume and channel keys repeat after
400ms, at most every 100ms. Toggle changes recognize fresh taps immediately.
Power, playback and mute do not repeat while held.

Mappings: arrows, OK/Enter, Back (Escape), Play (P), Pause (Space), Stop (X),
seek/skip, digits, volume/mute, information, guide and Home. Power sends S,
opening Kodi's shutdown menu. Only the eight captured buttons have live signal
verification; other mappings follow standard MCE codes. Unmapped keys log
without injecting input. Wake-from-off is not implemented.

References:
- https://github.com/torvalds/linux/blob/master/drivers/media/rc/ir-rc6-decoder.c
- https://github.com/torvalds/linux/blob/master/drivers/media/rc/keymaps/rc-rc6-mce.c

The ASan/UBSan test covers 256 commands, both toggles, every packet split,
ideal/observed timing, invalid timings/addresses, stale fragments, repeat
rules and replay of all 18 live frames. The full build-check.sh suite passed
on tinybox with PANEL_TEST_GPU=unavailable.

Installed from /home/kodi/picolcd-hp-rc6-review into
/usr/local/lib/picolcd-panel/20261018-192753. Rollback executable:
/var/backups/picolcd-panel-20261018-192753/picolcd.
User confirmed tinybox works correctly in Kodi. Minibox also passed the full
suite, including live CPU/GPU checks, and was installed from
/home/kodi/picolcd-hp-rc6-review into
/usr/local/lib/picolcd-panel/20261018-183055. Its rollback executable is
/var/backups/picolcd-panel-20261018-183055/picolcd. Both services are active
with uinput keyboard/mouse created. Minibox's physical response has not yet
been confirmed by the user.
