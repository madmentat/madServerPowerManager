# HTTP API

По умолчанию API слушает только `127.0.0.1:9187`. Реализованы:

- `GET /api/v1/status`
- `GET /api/v1/ups`
- `GET /api/v1/proxmox`
- `GET /api/v1/plug`
- `GET /api/v1/events`
- `GET /api/v1/health`
- `GET /api/v1/config`

`/api/v1/config` не возвращает ключ Tuya, SSH-ключ и API-токен. Все методы,
кроме GET, возвращают `405`; управляющий HTTP API в текущей версии отсутствует,
даже если параметр `allow_control` будет ошибочно включён.
