#!/bin/sh
# One-time EEPROM programming; does not install or replace the panel executable.
set -eu
cd "$(dirname "$0")"
if [ "$(id -u)" -ne 0 ]; then
    echo 'Run: sudo sh /home/kodi/picolcd-review-build/install-cat-splash.sh' >&2
    exit 1
fi
sha256sum -c build/release.sha256
backup=$(mktemp -d /var/backups/picolcd-splash-XXXXXXXX)
active=0
programming=0
if systemctl is-active --quiet panel.service; then active=1; fi
finish() {
    result=$?
    trap - EXIT
    set +e
    if [ "$programming" = 1 ]; then
        ./build/picolcd eeprom-restore "$backup/eeprom.bin"
    fi
    if [ "$active" = 1 ]; then systemctl start panel.service; fi
    exit "$result"
}
trap finish EXIT
trap 'exit 1' HUP INT TERM
systemctl stop panel.service
./build/picolcd eeprom-backup "$backup/eeprom.bin"
cp cat-splash.txt "$backup/cat-splash.txt"
programming=1
./build/picolcd splash cat-splash.txt
programming=0
./build/picolcd backlight 1
echo "Cat splash written and verified. Original EEPROM: $backup/eeprom.bin"
echo 'The power-on splash will appear after the LCD next loses and regains USB power.'
