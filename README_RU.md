# madServerPowerManager

Автономный менеджер питания сервера на C++20 для отдельного маломощного
Linux-узла — например, Intel NUC, Raspberry Pi, тонкого клиента, другого мини-ПК
или одноплатного компьютера.

[English](https://github.com/madmentat/madServerPowerManager/tree/EN) ·
**Русский** ·
[Ветка разработки](https://github.com/madmentat/madServerPowerManager/tree/develop)

> [!IMPORTANT]
> Проект управляет физическим питанием сервера. По умолчанию и после установки
> используется `armed=false`: телеметрия, диагностика и API работают, но shutdown
> и переключение реле заблокированы.

## Состояние проекта

Версия: `1.0.0`.

Текущая стадия — рабочий билд. На реальном Intel NUC проверены сборка, NUT,
локальное чтение Tuya 3.5, HTTP API, восстановление после рестарта и 27
инвариантов машины состояний. Полный аварийный цикл с shutdown сервера,
отключением и восстановлением розетки пройден успешно 28 июля 2026.

## Задача

Если внешнее питание пропало ненадолго, сервер должен продолжить работу. Если
генератор не запустился за заданный grace period, сервер необходимо штатно
выключить, подтвердить завершение работы и только затем снять с него питание.
После устойчивого восстановления сети розетка включается, а BIOS запускает
сервер по событию AC Restore.

Менеджер работает на отдельном NUC, который остаётся включённым вместе с ИБП и
сетевым оборудованием. Поэтому отключается только основной сервер, а не весь
выход ИБП.

```text
ИБП ──USB──► NUT ───────────────┐
                                │
                           Intel NUC
                                │
                    madServerPowerManager
                     ├── FSM и state.json
                     ├── SSH ───────────► управляемый сервер
                     ├── Tuya 3.5 ──────► Wi-Fi-розетка
                     └── HTTP JSON API ─► локальный мониторинг
```

## Возможности

- локальный опрос ИБП через Network UPS Tools;
- нативный клиент Tuya 3.5 без Python, TinyTuya и облачного API;
- HMAC-SHA256, SHA-256, AES-128 и AES-GCM для локального протокола Tuya;
- штатный shutdown управляемого Linux-сервера через системный OpenSSH;
- конечная машина состояний с фильтрацией кратких пропаданий сети;
- продолжение незавершённого аварийного цикла после рестарта;
- атомарная запись состояния: временный файл, `fsync`, `rename`;
- локальный read-only HTTP JSON API;
- отдельные CLI-команды диагностики и симуляции;
- systemd-unit с hardening-параметрами;
- fail-safe поведение: неоднозначность блокирует силовое действие.

## Машина состояний

```text
STARTING
  ├── armed=false ──► MONITOR_ONLY
  └── armed=true ───► MAINS_ON
                         │ OB
                         ▼
                 ON_BATTERY_GRACE
                         │
                         ▼
                SHUTDOWN_REQUESTED
                         │
                         ▼
                WAITING_SERVER_OFF
                         │
                         ▼
               CUTTING_SERVER_POWER
                         │
                         ▼
                WAITING_FOR_MAINS
                         │ OL + stable
                         ▼
                MAINS_STABILIZING
                         │
                         ▼
              RESTORING_SERVER_POWER
                         │
                         ▼
                      RECOVERY
```

Ошибочная конфигурация приводит в `ERROR`. Ненадёжная телеметрия или
невозможность принять безопасное решение — в `DEGRADED`. В обоих состояниях
автоматическое управление питанием заблокировано.

## Требования

- Linux;
- CMake 3.20 или новее;
- компилятор с поддержкой C++20;
- потоки POSIX;
- системный OpenSSH-клиент;
- работающий NUT-сервер;
- локальный сетевой доступ к управляемому серверу и Tuya-розетке.

Python, pip, TinyTuya, Tuya Cloud и доступ в интернет во время работы не
требуются.

## Сборка и тестирование

```bash
git clone --branch RU https://github.com/madmentat/madServerPowerManager.git
cd madServerPowerManager

cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Цели сборки:

- `mad-server-power-manager` — основной демон и CLI;
- `plugctl` — отдельный диагностический инструмент Tuya;
- `madspm-test-*` — тесты FSM, StateStore, конфигурации, NUT, SSH-сервера,
  HTTP API и менеджера при `BUILD_TESTING=ON`.

## Конфигурация

Основная конфигурация не должна содержать секретов:

```ini
[general]
armed=false
poll_interval_seconds=5
state_file=/var/lib/mad-server-power-manager/state.json
secrets_file=/etc/mad-server-power-manager/secrets.ini

[ups]
nut_host=YOUR_NUT_HOST
nut_port=3493
nut_name=ups
on_battery_confirm_seconds=10
grace_seconds=480
mains_stable_seconds=60
critical_battery_charge=15

[server]
host=YOUR_SERVER_HOST
port=22
user=mad-power-manager
private_key=/etc/mad-server-power-manager/id_ed25519
known_hosts=/etc/mad-server-power-manager/known_hosts
command_timeout_seconds=15
shutdown_timeout_seconds=300
server_off_confirm_seconds=30
force_cut_after_shutdown_timeout=false

[plug]
host=YOUR_TUYA_PLUG_HOST
port=6668
protocol_version=3.5
switch_dp=1
minimum_off_seconds=20

[api]
enabled=true
listen_address=127.0.0.1
port=9187
allow_control=false
```

Секция `[server]` описывает любой Linux-сервер, который принимает безопасные
команды `status` и `poweroff` через ограниченный SSH forced command. Никаких
Proxmox API программа не использует. Старая секция `[proxmox]` временно
принимается с предупреждением для совместимости.

Секреты хранятся в отдельном файле:

```ini
[tuya]
device_id=<DEVICE_ID>
local_key=<LOCAL_KEY>

[api]
token=<API_TOKEN>
```

```bash
sudo chown madspm:madspm /etc/mad-server-power-manager/secrets.ini
sudo chmod 600 /etc/mad-server-power-manager/secrets.ini
```

Безопасные шаблоны лежат в [`config/`](config/).

## Безопасная проверка

Держите `armed=false` на всех этапах начальной проверки:

```bash
./build/mad-server-power-manager --config dev.ini --validate-config
./build/mad-server-power-manager --config dev.ini --test-nut
./build/mad-server-power-manager --config dev.ini --test-plug
./build/mad-server-power-manager --config dev.ini --test-ssh
./build/mad-server-power-manager --config dev.ini --dry-run
```

`--test-plug` читает DPS и не переключает реле. `--dry-run` и `--doctor` не
отправляют команды питания при `armed=false`.

## Установка

```bash
sudo ./scripts/install.sh
sudoedit /etc/mad-server-power-manager/secrets.ini

sudo mad-server-power-manager --validate-config
sudo mad-server-power-manager --doctor
sudo systemctl enable --now mad-server-power-manager
```

Установщик оставляет сервис в безопасном режиме. SSH-ключ, проверенный
`known_hosts`, ограниченного пользователя и forced command на сервере создаёт
интерактивный provisioning:

```bash
sudo ./scripts/provision-server-ssh.sh YOUR_SERVER_HOST
```

Перед записью `known_hosts` скрипт показывает fingerprints и требует явного
`yes`. Проверочная команда — только `status`; `poweroff` при provisioning не
запускается.

Проверка сервиса:

```bash
systemctl status mad-server-power-manager
journalctl -u mad-server-power-manager -f
curl http://127.0.0.1:9187/api/v1/health
```

Удаление:

```bash
sudo ./scripts/uninstall.sh
```

Перед запуском `scripts/migrate-nut-from-server.sh` ознакомьтесь с его
содержимым и сделайте резервную копию активной конфигурации NUT.

## CLI

```
--help
--version
--init
--doctor
--status
--print-ups
--test-nut
--test-ssh
--test-plug
--dry-run
--simulate-on-battery
--simulate-online
--validate-config
--show-state
--reset-state --yes
```

Симуляции проверяют переходы FSM без реального отключения питания, но не
заменяют контролируемое физическое испытание.

## HTTP API

По умолчанию API слушает на `127.0.0.1:9187`.

| Метод | Endpoint | Назначение |
|-------|----------|------------|
| GET | `/api/v1/status` | Сводный статус |
| GET | `/api/v1/ups` | Телеметрия ИБП |
| GET | `/api/v1/server` | Доступность управляемого сервера |
| GET | `/api/v1/plug` | Состояние розетки |
| GET | `/api/v1/events` | Последние события |
| GET | `/api/v1/health` | Health check |
| GET | `/api/v1/config` | Санитизированная конфигурация |

Старый путь `/api/v1/proxmox` временно является deprecated-алиасом
`/api/v1/server`.

Любой метод кроме `GET` возвращает `405`. Версия `1.0.0` не предоставляет
управляющего API, даже если `allow_control` случайно включён.

## Модель безопасности

- `armed=false` — безопасное значение по умолчанию;
- демон никогда не включает `armed` самостоятельно;
- секреты отделены от основной конфигурации;
- SSH использует отдельный ключ, `known_hosts` и `PasswordAuthentication=no`;
- на управляемом сервере следует выделить отдельного пользователя с forced command;
- выключение подтверждается только после принятого shutdown и непрерывной
  недоступности SSH в течение `server_off_confirm_seconds`;
- `force_cut_after_shutdown_timeout=false` предотвращает слепое снятие питания;
- API доступен только на loopback и только на чтение;
- systemd-сервис работает от непривилегированного пользователя.

Никогда не коммитьте реальные Local Key, Device ID, API-токен, SSH-ключ,
рабочую конфигурацию и state-файл.

## Чек-лист развёртывания

1. Проверить телеметрию NUT (только чтение).
2. Запустить демон с `armed=false`.
3. Проверить SSH безвредной командой.
4. Протестировать розетку без подключенного сервера.
5. Симулировать OB и OL.
6. Сделать резервную копию всей активной конфигурации.
7. Провести контролируемое физическое испытание.
8. Только после этого вручную выставить `armed=true`.

## Структура репозитория

```
include/madspm/   публичные C++-интерфейсы
src/              демон, FSM, NUT, SSH, Tuya и реализация API
tests/            модульные и интеграционные тесты компонентов
config/           безопасные шаблоны конфигурации
systemd/          systemd-юнит
scripts/          скрипты установки, удаления и миграции NUT
docs/             архитектура, API, эксплуатация, отчёты
```

Ключевые документы:

- [`docs/STATE_MACHINE.md`](docs/STATE_MACHINE.md);
- [`docs/STATE_FILE.md`](docs/STATE_FILE.md);
- [`docs/API.md`](docs/API.md);
- [`docs/OPERATIONS_RU.md`](docs/OPERATIONS_RU.md);
- [`docs/KNOWN_LIMITATIONS.md`](docs/KNOWN_LIMITATIONS.md).

## Ветки

- **EN** — стабильная ветка и английский README;
- **RU** — тот же код с русским README;
- **develop** — активная разработка, русский README.

## Известные ограничения

- SSH provisioning требует доступ администратора сервера и ручную проверку
  fingerprint;
- управляемый сервер ещё не переведён в NUT netclient;
- ИБП не сообщает `battery.runtime`;
- HTTP API поддерживает только IPv4 и локальный мониторинг;
- полный физический аварийный цикл пройден 28 июля 2026;
- установленную на тестовом NUC EOL-версию Ubuntu необходимо обновить отдельным
  контролируемым этапом.

Подробности — в [`docs/KNOWN_LIMITATIONS.md`](docs/KNOWN_LIMITATIONS.md).

## Лицензия

Файл лицензии пока не добавлен. До выбора лицензии все права сохранены за
автором проекта.
