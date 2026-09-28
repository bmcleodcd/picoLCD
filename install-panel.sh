#!/bin/sh
# Run only after build-check.sh has passed. Installs a private library so other
# libpicolcd consumers are unaffected. Existing service configuration is retained.
set -eu
cd "$(dirname "$0")"
if [ "$(id -u)" -ne 0 ]; then
    echo "Run: sudo sh /home/kodi/picolcd-review-build/install-panel.sh" >&2
    exit 1
fi
sha256sum -c build/release.sha256
stamp=$(date +%Y%m%d-%H%M%S)
release=/usr/local/lib/picolcd-panel/$stamp
backup=/var/backups/picolcd-panel-$stamp
mkdir -p "$release" "$backup"
cp -a /usr/local/bin/picolcd "$backup/picolcd"
systemctl cat panel.service > "$backup/panel.service.txt"
install -m 755 build/picolcd build/libpicolcd.so.0 "$release/"
rollback() {
    systemctl stop panel.service || true
    cp -a "$backup/picolcd" /usr/local/bin/.picolcd-restore
    mv -Tf /usr/local/bin/.picolcd-restore /usr/local/bin/picolcd
    systemctl start panel.service
}
switched=0
trap 'if [ "$switched" = 1 ]; then rollback; fi' EXIT
trap 'exit 1' HUP INT TERM
switched=1
systemctl stop panel.service
ln -s "$release/picolcd" /usr/local/bin/.picolcd-new
mv -Tf /usr/local/bin/.picolcd-new /usr/local/bin/picolcd
systemctl start panel.service
restarts=$(systemctl show -p NRestarts --value panel.service)
sleep 8
systemctl is-active --quiet panel.service
[ "$(systemctl show -p NRestarts --value panel.service)" = "$restarts" ]
pid=$(systemctl show -p MainPID --value panel.service)
[ "$(readlink /proc/"$pid"/exe)" = "$release/picolcd" ]
switched=0
trap - EXIT HUP INT TERM
echo "Installed: $release"
echo "Rollback binary: $backup/picolcd"
systemctl --no-pager --full status panel.service
