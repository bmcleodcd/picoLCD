# Minibox panel fixes

The matching original source was `/home/kodi/dev/src/main.c`, identical to the
Windows checkout (SHA256 d416c7ac9c0deaea717c4556311cae342f6f560381a9377bc37a48e9f2754f99).

## Behaviour

- The date row scrolls left one character every 400 ms, with a three-space gap
  between repetitions. It shows the full weekday/month, date, year and time.
- When Kodi has an active audio/video player, its title replaces the date (falling
  back to its label, then file/path). Paused content retains its title. RetroArch's
  loaded game takes precedence, including while paused/in menus. With no content,
  or when status is unavailable, the row returns to the date. Long titles scroll;
  short titles stay still. A content change resets scrolling to the beginning.
- A background thread queries local Kodi JSON-RPC (TCP 9090) and RetroArch status
  (UDP 55355) approximately once per second with bounded 500 ms network waits.
  The USB/button loop never waits for these queries. json-c handles streamed JSON
  and notifications. Non-ASCII characters become `?` on this character LCD.
- RetroArch needs `network_cmd_enable = "true"` and `network_cmd_port = "55355"`
  in its config, effective on its next start. This has been enabled on minibox with
  a backup of the previous config. RetroArch's command interface listens on all
  network interfaces; the panel itself sends only read-only queries to localhost.
- CPU temperature shows one decimal; GPU temperature adds a cosmetic `.0` to its integer NVML reading.
- Rows are space-padded and sent only when changed. CPU/GPU samples run immediately,
  then every five monotonic seconds; clock comparison runs once per second.
- CPU sensor is discovered by `x86_pkg_temp` type. Failed sensor reads show `--`.
- NVML is loaded once and reused, with retries after errors. No NVIDIA SDK needed.
- Both button slots are tracked, duplicate reports ignored, old keys released before
  new presses, and unknown IDs ignored. F1 starts RetroArch on release.
- RetroArch uses asynchronous systemd StartUnit over D-Bus, with a two-second reply
  timeout. The panel service's existing root identity provides authorization; the
  RetroArch service continues to run as kodi. No `system`, `popen`, or subprocesses.
- X connections close before requesting RetroArch, and failed connections retry
  every two seconds. An unexpected loss of an established X connection is fatal to
  Xlib; the process exits unsuccessfully so the existing Restart=on-failure service
  reconnects. There is no attempt to reuse a dead Xlib Display pointer.
- USB reads in panel mode wait at most 200 ms. Existing library callers retain their
  ten-second default. Timeout reads allocate nothing, malformed reports are rejected,
  and USB errors exit unsuccessfully for service recovery.
- SIGTERM/SIGINT/SIGQUIT release held keys and free resources.
- IR decoder resets its state in place, fixing its inherited dangling-pointer bug
  and elapsed-time arithmetic across second boundaries.

## Build and verification

Linux dependencies: C compiler, libusb-0.1 development package, libxdo/X11 development
packages, pkg-config, libdbus-1 and json-c >= 0.15 development packages. NVML is an optional runtime
dependency. Run `sh build-check.sh` to build a private binary/library and execute
AddressSanitizer/UndefinedBehaviorSanitizer checks, an isolated session-bus launcher
test, actual sensor queries, and a six-second loop test with simulated USB timeouts
and unavailable X, including date/Kodi/RetroArch/date transitions. Media tests cover
title/path fallback, paused/unloaded content, fragmented/concatenated JSON, query
timeouts, and worker shutdown. The live panel is not accessed by these tests.

The legacy SDK emits warnings in unrelated font/widget/parser code. The custom
panel module builds without warnings. This does not claim to repair all SDK code.

## Installation

On minibox, after the checks pass:

    sudo sh /home/kodi/picolcd-review-build/install-panel.sh

The installer backs up `/usr/local/bin/picolcd` under `/var/backups`, installs a
versioned binary and private library under `/usr/local/lib/picolcd-panel`, switches
the executable symlink, and restarts only panel.service. A failed activation restores
the old executable. It does not replace the shared `/usr/local/lib/libpicolcd` library.

Manual rollback (substitute the backup directory printed by the installer):

    sudo systemctl stop panel.service
    sudo cp -a /var/backups/picolcd-panel-TIMESTAMP/picolcd /usr/local/bin/.picolcd-restore
    sudo mv -Tf /usr/local/bin/.picolcd-restore /usr/local/bin/picolcd
    sudo systemctl start panel.service

Physical button operation, LCD appearance, and the actual Kodi/RetroArch transition
still need checking after installation. The automated launcher test uses a mock
systemd endpoint on a separate bus and does not launch or stop media applications.
