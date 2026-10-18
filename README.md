# picoLCD

Linux driver and SDK for Mini-Box USB LCD displays, with a Kodi/RetroArch panel
service for Linux media-center systems. The legacy library includes 20x2, 20x4
and 256x64 drivers; the custom panel behavior targets the 20x2 display.

The panel shows date/media titles, playback status, volume/display changes and
temperatures. Physical buttons and Ortek VRC-1100, HP RC6 MCE and original Xbox
DVD remotes use native Linux uinput keyboard/mouse devices. The built-in LCD
IR receiver handles these remotes without separate remote-specific receivers.
An optional XRandR helper supplies display-mode information; input works
independently of X11. The main panel binary does not link libxdo, X11 or XRandR.

## Licence and attribution

This fork is distributed under the GNU General Public License, version 2
(GPLv2). See [COPYING](COPYING) for the full licence and warranty disclaimer.
The original picoLCD SDK is by Nicu Pavel / iTuner Corporation (Mini-Box);
original copyright notices and [AUTHORS](AUTHORS) are retained.

Changes by bmcleodcd, 2023-2026, add the Linux media-center panel and related
features described in [ChangeLog](ChangeLog). This is a modified version of
the original SDK. Modification/licence notices were added on 2026-10-18.
Redistributed modified versions remain subject to GPLv2; distribution of
binaries must satisfy its corresponding-source requirements.

## Build and maintenance

See [AGENTS.md](AGENTS.md) for dependencies, architecture, remote protocols,
configuration, validation, installation, rollback and EEPROM maintenance.
The checked Linux build creates private artifacts without installing them:

```sh
sh build-check.sh
# For a host with CPU temperature but no NVML GPU temperature:
PANEL_TEST_GPU=unavailable sh build-check.sh
```

After a successful check, `sudo sh install-panel.sh` installs the versioned
panel release and restarts `panel.service`. This installer is for the existing
systemd panel setup; inspect the host's service configuration before using it.

## Legacy SDK material

- [Library development guide](doc/libusblcd-development-guide.txt)
- [Command-line client guide](doc/usblcd-client-guide.txt)
- [Examples](doc/examples/)
- [Historical integration patches](patches/)
- [Historical macOS kext notes](kext/README.md)

Those guides/examples describe the inherited SDK and may predate this fork's
Linux panel service. They are not evidence of current macOS/Windows support
or of the current input architecture. Historical implementation and deployment
notes are retained in Git history.
