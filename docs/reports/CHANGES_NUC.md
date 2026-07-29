# Изменения на NUC

2026-07-28:

1. Установлены пакеты `nut`, `nut-client`, `nut-server` 2.8.1-4ubuntu1.
2. Установлен CMake 3.31.6.
3. Резервная копия исходных NUT-файлов:
   `/root/madspm-backups/nut-initial-20260728T132843Z`.
4. Настроен `nutdrv_qx` для USB `0001:0000`, `protocol=hunnox`.
5. `upsd` слушает 127.0.0.1 и 192.168.88.148:3493.
6. Создан отдельный случайный локальный секрет NUT; старый пароль не переносился.
7. `nut-monitor` отключён до следующего безопасного этапа.
8. Проект собран в `/home/madmentat/madServerPowerManager`.
9. Установлен и включён `mad-server-power-manager.service` под пользователем
   `madspm`, режим `armed=false`, API `127.0.0.1:9187`.
10. Проверено восстановление после restart NUT и restart менеджера.

Розетка и Proxmox в ходе этих изменений не выключались.
