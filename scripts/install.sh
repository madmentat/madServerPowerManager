#!/usr/bin/env bash
set -euo pipefail

if [[ ${EUID} -ne 0 ]]; then
    echo "Запусти от root: sudo $0" >&2
    exit 1
fi

cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
binary="${1:-build/mad-server-power-manager}"
plugctl_binary="${2:-build/plugctl}"
[[ -x "$binary" ]] || { echo "Не найден бинарник: $binary" >&2; exit 1; }
[[ -x "$plugctl_binary" ]] || { echo "Не найден бинарник: $plugctl_binary" >&2; exit 1; }

getent group madspm >/dev/null || groupadd --system madspm
id madspm >/dev/null 2>&1 || useradd --system --gid madspm \
    --home-dir /var/lib/mad-server-power-manager --shell /usr/sbin/nologin madspm

install -d -o root -g madspm -m 0750 /etc/mad-server-power-manager
install -d -o madspm -g madspm -m 0700 /var/lib/mad-server-power-manager
install -o root -g root -m 0755 "$binary" /usr/local/bin/mad-server-power-manager
install -o root -g root -m 0755 "$plugctl_binary" /usr/local/bin/plugctl

if [[ ! -f /etc/mad-server-power-manager/madServerPowerManager.ini ]]; then
    install -o root -g madspm -m 0640 config/madServerPowerManager.ini.example \
        /etc/mad-server-power-manager/madServerPowerManager.ini
fi
if [[ ! -f /etc/mad-server-power-manager/secrets.ini ]]; then
    install -o madspm -g madspm -m 0600 config/secrets.ini.example \
        /etc/mad-server-power-manager/secrets.ini
fi

if grep -Eq '^[[:space:]]*armed[[:space:]]*=[[:space:]]*true' \
        /etc/mad-server-power-manager/madServerPowerManager.ini; then
    echo "ОТКАЗ: installer не запускает систему с armed=true." >&2
    echo "Сначала вручную установи armed=false." >&2
    exit 1
fi

install -o root -g root -m 0644 systemd/mad-server-power-manager.service \
    /etc/systemd/system/mad-server-power-manager.service
systemctl daemon-reload
systemctl enable mad-server-power-manager.service

echo "Установлено в безопасном режиме. Проверь секреты и выполни:"
echo "  mad-server-power-manager --validate-config"
echo "  mad-server-power-manager --doctor"
echo "  systemctl start mad-server-power-manager"
