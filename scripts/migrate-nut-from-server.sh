#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "Использование: sudo $0 --reference DIR [--apply]"
    echo "Без --apply выводится план, файлы не изменяются."
}

reference=""
apply=false
while (($#)); do
    case "$1" in
        --reference) reference="${2:?}"; shift 2 ;;
        --apply) apply=true; shift ;;
        *) usage; exit 2 ;;
    esac
done
[[ -n "$reference" ]] || { usage; exit 2; }

echo "Референс исходного сервера: $reference"
echo "Переносятся только ups.conf и параметры режима; секреты не копируются."
echo "Целевой драйвер: nutdrv_qx, VID:PID 0001:0000, protocol=hunnox."
if ! $apply; then
    echo "Dry-run завершён. Для применения добавь --apply."
    exit 0
fi
[[ ${EUID} -eq 0 ]] || { echo "Для --apply нужен root" >&2; exit 1; }

timestamp=$(date -u +%Y%m%dT%H%M%SZ)
backup="/root/madspm-backups/nut-migration-$timestamp"
install -d -m 0700 "$backup"
cp -a /etc/nut "$backup/"
install -o root -g nut -m 0640 "$reference/ups.conf" /etc/nut/ups.conf
systemctl restart nut-driver-enumerator.service
systemctl restart nut-driver@ups.service nut-server.service
upsc ups@127.0.0.1
echo "Миграция завершена; backup: $backup"
