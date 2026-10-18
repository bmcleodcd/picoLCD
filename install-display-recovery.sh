#!/bin/sh
# Changes by bmcleodcd: 2026-10-18.
# Modification/licence notice added by bmcleodcd: 2026-10-18.
# This file is part of picoLCD and is distributed under the GNU General
# Public License, version 2. See COPYING for the terms and warranty disclaimer.
set -eu
test "$(id -u)" = 0
backup=$(mktemp -d /var/backups/panel-display-recovery-XXXXXXXX)
target=/etc/systemd/system/panel.service.d/40-display-recovery.conf
mkdir -p /etc/systemd/system/panel.service.d
if test -f "$target"; then cp -p "$target" "$backup/40-display-recovery.conf"; fi
systemctl cat panel.service > "$backup/panel.service.txt"
cat > "$target" <<'EOF'
[Unit]
StartLimitIntervalSec=60
StartLimitBurst=20

[Service]
RestartSec=1
EOF
systemctl daemon-reload
systemctl reset-failed panel.service
echo "Recovery configuration backup: $backup"
