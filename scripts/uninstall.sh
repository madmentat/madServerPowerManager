#!/usr/bin/env bash
set -euo pipefail

if [[ ${EUID} -ne 0 ]]; then
    echo "Запусти от root: sudo $0" >&2
    exit 1
fi

timestamp=$(date -u +%Y%m%dT%H%M%SZ)
backup="/root/madspm-uninstall-$timestamp"
install -d -m 0700 "$backup"
systemctl disable --now mad-server-power-manager.service 2>/dev/null || true
[[ -d /etc/mad-server-power-manager ]] &&
    cp -a /etc/mad-server-power-manager "$backup/"
[[ -d /var/lib/mad-server-power-manager ]] &&
    cp -a /var/lib/mad-server-power-manager "$backup/"
rm -f /etc/systemd/system/mad-server-power-manager.service
rm -f /usr/local/bin/mad-server-power-manager /usr/local/bin/plugctl
systemctl daemon-reload

echo "Бинарники и unit удалены. Конфиг и state сохранены в $backup."
echo "Пользователь madspm и каталоги данных намеренно не удалены."
echo "Конфигурация NUT не изменялась."
