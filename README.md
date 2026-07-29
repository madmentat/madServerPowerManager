# madServerPowerManager

An autonomous C++20 power manager for Proxmox, designed to run on a dedicated
low-power Linux host such as an Intel NUC, Raspberry Pi, thin client, or another
mini PC or single-board computer.

**English** ·
[Русский](https://github.com/madmentat/madServerPowerManager/tree/RU) ·
[Development branch](https://github.com/madmentat/madServerPowerManager/tree/develop)

> [!IMPORTANT]
> This project can control the physical power of a server. Every installation
> starts with `armed=false`: telemetry, diagnostics, and the API remain
> available, while shutdown and relay switching are blocked.

## Project status

Version: `1.0.0`.

The current release is a working build. Compilation,
NUT telemetry, local Tuya 3.5 reads, the HTTP API, restart recovery, and 27 state
machine invariants have been verified on a real Intel NUC. A complete emergency
cycle involving Proxmox shutdown, smart plug cut-off, and power restoration
was successfully completed on July 28, 2026.

## Problem statement

A short mains outage should not stop the Proxmox host. If the generator does not
start within the configured grace period, the server must shut down cleanly.
Power may be removed only after shutdown has been confirmed. When mains power is
stable again, the smart plug is enabled and the server starts through its BIOS
AC Restore setting.

The manager runs on a separate NUC that remains powered together with the UPS and
network equipment. This makes it possible to remove power from the main server
without turning off the entire UPS output.

```text
UPS ──USB──► NUT ─────────────────┐
                                  │
                              Intel NUC
                                  │
                      madServerPowerManager
                       ├── FSM and state.json
                       ├── SSH ───────────► Proxmox
                       ├── Tuya 3.5 ──────► Wi-Fi smart plug
                       └── HTTP JSON API ─► local monitoring
```

## Features

- local UPS telemetry through Network UPS Tools;
- native Tuya 3.5 client with no Python, TinyTuya, or cloud API dependency;
- HMAC-SHA256, SHA-256, AES-128, and AES-GCM for the local Tuya protocol;
- graceful Proxmox shutdown through the system OpenSSH client;
- finite-state machine with short-outage filtering;
- persistent recovery of an unfinished emergency cycle after restart;
- atomic state writes using a temporary file, `fsync`, and `rename`;
- local read-only HTTP JSON API;
- dedicated diagnostics and simulation commands;
- hardened systemd service;
- fail-safe behavior: ambiguous input blocks physical power actions.

## State machine

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

Invalid required configuration leads to `ERROR`. Unreliable telemetry or an
inability to make a safe decision leads to `DEGRADED`. Automatic power control
is disabled in both states.

## Requirements

- Linux;
- CMake 3.20 or newer;
- a compiler with C++20 support;
- POSIX threads;
- the system OpenSSH client;
- a working NUT server;
- local network access to Proxmox and the Tuya smart plug.

Python, pip, TinyTuya, Tuya Cloud, and internet access are not required at
runtime.

## Build and test

```bash
git clone --branch EN https://github.com/madmentat/madServerPowerManager.git
cd madServerPowerManager

cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Build targets:

- `mad-server-power-manager` — the main daemon and CLI;
- `plugctl` — a standalone Tuya diagnostics tool;
- `madspm-tests` — FSM tests when `BUILD_TESTING=ON`.

## Configuration

The main configuration must not contain secrets:

```ini
[general]
armed=false
poll_interval_seconds=5
state_file=/var/lib/mad-server-power-manager/state.json
secrets_file=/etc/mad-server-power-manager/secrets.ini

[ups]
nut_host=127.0.0.1
nut_port=3493
nut_name=ups
on_battery_confirm_seconds=10
grace_seconds=480
mains_stable_seconds=60
critical_battery_charge=15

[proxmox]
host=192.168.1.20
port=22
user=mad-power-manager
private_key=/etc/mad-server-power-manager/id_ed25519
known_hosts=/etc/mad-server-power-manager/known_hosts
shutdown_timeout_seconds=360
force_cut_after_shutdown_timeout=false

[plug]
host=192.168.1.30
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

Secrets are stored in a separate file:

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

Safe templates are available in [`config/`](config/).

## Safe validation

Keep `armed=false` during every initial check:

```bash
./build/mad-server-power-manager --config dev.ini --validate-config
./build/mad-server-power-manager --config dev.ini --test-nut
./build/mad-server-power-manager --config dev.ini --test-plug
./build/mad-server-power-manager --config dev.ini --test-ssh
./build/mad-server-power-manager --config dev.ini --dry-run
```

`--test-plug` reads DPS without switching the relay. `--dry-run` and `--doctor`
do not issue power commands while `armed=false`.

## Installation

```bash
sudo ./scripts/install.sh
sudoedit /etc/mad-server-power-manager/secrets.ini

sudo mad-server-power-manager --validate-config
sudo mad-server-power-manager --doctor
sudo systemctl enable --now mad-server-power-manager
```

Service checks:

```bash
systemctl status mad-server-power-manager
journalctl -u mad-server-power-manager -f
curl http://127.0.0.1:9187/api/v1/health
```

Uninstall:

```bash
sudo ./scripts/uninstall.sh
```

Review [`scripts/migrate-nut-from-proxmox.sh`](scripts/migrate-nut-from-proxmox.sh)
and back up the active NUT configuration before running the migration helper.

## CLI

```text
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

Simulations verify FSM transitions without a power outage, but they do not
replace a controlled physical test.

## HTTP API

The API listens on `127.0.0.1:9187` by default.

| Endpoint | Purpose |
|---|---|
| `GET /api/v1/status` | Aggregate status |
| `GET /api/v1/ups` | UPS telemetry |
| `GET /api/v1/proxmox` | Proxmox reachability |
| `GET /api/v1/plug` | Smart plug state |
| `GET /api/v1/events` | Recent events |
| `GET /api/v1/health` | Health check |
| `GET /api/v1/config` | Sanitized configuration |

Every method except `GET` returns `405`. Version 1.0.0 does not expose a control
API, even if `allow_control` is accidentally enabled.

## Security model

- `armed=false` is the safe default;
- the daemon never enables `armed` automatically;
- secrets are separated from the main configuration;
- SSH uses a dedicated key, `known_hosts`, and `PasswordAuthentication=no`;
- Proxmox should use a dedicated account with a forced command;
- a closed SSH port alone is not proof that the server is off;
- `force_cut_after_shutdown_timeout=false` prevents blind power removal;
- the API is loopback-only and read-only;
- the systemd service runs as an unprivileged user.

Never commit a real Local Key, Device ID, API token, private SSH key, runtime
configuration, or state file.

## Deployment checklist

1. Validate read-only NUT telemetry.
2. Start the daemon with `armed=false`.
3. Verify SSH with a harmless command.
4. Test the smart plug without the server connected.
5. Simulate `OB` and `OL`.
6. Back up all active configuration.
7. Perform a controlled on-site power test.
8. Only then set `armed=true` manually.

## Repository layout

```text
include/madspm/   public C++ interfaces
src/              daemon, FSM, NUT, SSH, Tuya, and API implementation
tests/            state-machine tests
config/           safe configuration templates
systemd/          service unit
scripts/          install, uninstall, and NUT migration helpers
docs/             architecture, API, operations, and reports
```

Key documents:

- [`docs/STATE_MACHINE.md`](docs/STATE_MACHINE.md);
- [`docs/STATE_FILE.md`](docs/STATE_FILE.md);
- [`docs/API.md`](docs/API.md);
- [`docs/OPERATIONS_RU.md`](docs/OPERATIONS_RU.md);
- [`docs/KNOWN_LIMITATIONS.md`](docs/KNOWN_LIMITATIONS.md).

## Branches

- `EN` — stable branch and English README;
- `RU` — the same code with a Russian README;
- `develop` — active development with a Russian README.

## Known limitations

- the production SSH forced command has not been commissioned yet;
- Proxmox has not been migrated to NUT netclient mode;
- the UPS does not provide `battery.runtime`;
- the HTTP API is IPv4-only and intended for local monitoring;
- the complete physical emergency cycle was successfully tested on July 28, 2026;
- the EOL Ubuntu release on the test NUC must be upgraded as a separate,
  controlled operation.

See [`docs/KNOWN_LIMITATIONS.md`](docs/KNOWN_LIMITATIONS.md) for details.

## License

No license file has been added yet. Until a license is selected, all rights are
reserved by the project author.
