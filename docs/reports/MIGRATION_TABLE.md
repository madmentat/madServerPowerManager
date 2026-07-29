# Таблица переноса NUT

| Параметр Proxmox | Фактическое значение | На NUC | Новое значение | Причина |
|---|---|---|---|---|
| NUT | 2.8.1-5 | Да | 2.8.1-4ubuntu1 | Версия репозитория Ubuntu |
| mode | standalone | Да | netserver | NUC — будущий основной NUT server |
| driver | nutdrv_qx | Да | nutdrv_qx | Проверенный драйвер |
| protocol | hunnox | Да | hunnox | Исторически успешный Hunnox 0.02 |
| vendorid/productid | 0001/0000 | Да | 0001/0000 | Совпадает с фактическим USB |
| port | auto | Да | auto | Проверенная конфигурация |
| bus | 001 | Да | 001 | Фактическая шина NUC |
| langid_fix | 0x0409 | Да | 0x0409 | Проверенный quirk |
| novendor/noscanlangid | enabled | Да | enabled | Проверенные quirks |
| UPS name | ups | Да | ups | Совместимость |
| upsd listen | localhost/default | Да | 127.0.0.1 и 192.168.88.148 | Локальный менеджер и будущий netclient |
| upsd user/password | madmentat/старый секрет | Нет | localmon/случайный секрет | Не переносить пароль |
| upsmon | primary | Позже | отключён на этапе телеметрии | Исключить преждевременный shutdown |
| POWEROFF_WAIT | 780 | Нет | логика FSM | NUC не должен отключать Proxmox локальным флагом |
| nut-ob-timer.sh | sleep 600 + poweroff | Нет | C++ FSM, grace 480 | Персистентность и управляемый удалённый цикл |

Proxmox не переведён в netclient: это отдельный этап после устойчивой работы
NUC и подготовки отката.
