#!/usr/bin/env bash
set -euo pipefail

host="${1:-}"
admin="${2:-root}"
port="${3:-22}"
remote_user="mad-power-manager"
config_dir="/etc/mad-server-power-manager"
key_file="$config_dir/id_ed25519"
known_hosts="$config_dir/known_hosts"

if [[ ${EUID} -ne 0 ]]; then
    echo "Запусти от root: sudo $0 SERVER_HOST [ADMIN_USER] [SSH_PORT]" >&2
    exit 1
fi
if [[ -z "$host" || "$host" == YOUR_* ]]; then
    echo "Использование: sudo $0 SERVER_HOST [ADMIN_USER] [SSH_PORT]" >&2
    exit 1
fi
if ! [[ "$port" =~ ^[0-9]+$ ]] || ((port < 1 || port > 65535)); then
    echo "Некорректный SSH-порт: $port" >&2
    exit 1
fi

install -d -o root -g madspm -m 0750 "$config_dir"
if [[ ! -f "$key_file" ]]; then
    ssh-keygen -q -t ed25519 -N '' -C 'madServerPowerManager' -f "$key_file"
fi
chown madspm:madspm "$key_file" "$key_file.pub"
chmod 0600 "$key_file"
chmod 0644 "$key_file.pub"

scan_file="$(mktemp)"
trap 'rm -f "$scan_file"' EXIT
ssh-keyscan -p "$port" -T 5 "$host" >"$scan_file" 2>/dev/null
if [[ ! -s "$scan_file" ]]; then
    echo "Не удалось получить SSH host key с $host:$port" >&2
    exit 1
fi
echo "Проверь fingerprints управляемого сервера по независимому каналу:"
ssh-keygen -lf "$scan_file"
read -r -p "Fingerprints верны? Введи yes: " answer
if [[ "$answer" != "yes" ]]; then
    echo "Отменено; known_hosts не изменён." >&2
    exit 1
fi
install -o madspm -g madspm -m 0644 "$scan_file" "$known_hosts"

public_key="$(<"$key_file.pub")"
remote_script='
set -eu
remote_user="mad-power-manager"
id "$remote_user" >/dev/null 2>&1 ||
    useradd --system --create-home --shell /bin/sh "$remote_user"
cat >/usr/local/sbin/madspm-server-command <<'"'"'REMOTE_HELPER'"'"'
#!/bin/sh
set -eu
case "${1:-}" in
    status) exit 0 ;;
    poweroff) exec /usr/sbin/shutdown --poweroff now ;;
    *) echo "Command denied" >&2; exit 126 ;;
esac
REMOTE_HELPER
chown root:root /usr/local/sbin/madspm-server-command
chmod 0755 /usr/local/sbin/madspm-server-command
cat >/usr/local/bin/madspm-ssh-dispatch <<'"'"'REMOTE_DISPATCH'"'"'
#!/bin/sh
set -eu
case "${SSH_ORIGINAL_COMMAND:-}" in
    status) exec /usr/bin/sudo /usr/local/sbin/madspm-server-command status ;;
    poweroff) exec /usr/bin/sudo /usr/local/sbin/madspm-server-command poweroff ;;
    *) echo "Command denied" >&2; exit 126 ;;
esac
REMOTE_DISPATCH
chown root:root /usr/local/bin/madspm-ssh-dispatch
chmod 0755 /usr/local/bin/madspm-ssh-dispatch
printf "%s\n" \
    "mad-power-manager ALL=(root) NOPASSWD: /usr/local/sbin/madspm-server-command status, /usr/local/sbin/madspm-server-command poweroff" \
    >/etc/sudoers.d/mad-power-manager
chmod 0440 /etc/sudoers.d/mad-power-manager
visudo -cf /etc/sudoers.d/mad-power-manager >/dev/null
home="$(getent passwd "$remote_user" | cut -d: -f6)"
install -d -o "$remote_user" -g "$remote_user" -m 0700 "$home/.ssh"
authorized="$home/.ssh/authorized_keys"
touch "$authorized"
chown "$remote_user:$remote_user" "$authorized"
chmod 0600 "$authorized"
key_line="restrict,command=\"/usr/local/bin/madspm-ssh-dispatch\" '"$public_key"'"
grep -Fqx "$key_line" "$authorized" || printf "%s\n" "$key_line" >>"$authorized"
'

echo "Настраиваю ограниченного пользователя на ${admin}@${host}."
echo "SSH может запросить пароль администратора управляемого сервера."
ssh -p "$port" -o StrictHostKeyChecking=yes \
    -o "UserKnownHostsFile=$known_hosts" "${admin}@${host}" "$remote_script"

echo "Проверяю forced command без выключения сервера..."
sudo -u madspm ssh -p "$port" -i "$key_file" \
    -o BatchMode=yes -o StrictHostKeyChecking=yes \
    -o "UserKnownHostsFile=$known_hosts" \
    "${remote_user}@${host}" status
echo "SSH provisioning завершён. Команда poweroff не выполнялась."
