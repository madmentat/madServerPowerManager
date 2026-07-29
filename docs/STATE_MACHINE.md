# Машина состояний

`STARTING` сверяет конфиг, сохранённое и реальное состояние. При `armed=false`
любое состояние сводится к `MONITOR_ONLY`, где управляющие действия запрещены.

Боевой путь после отдельного допуска:

```text
MAINS_ON
  -> ON_BATTERY_GRACE
  -> SHUTDOWN_REQUESTED
  -> WAITING_SERVER_OFF
  -> CUTTING_SERVER_POWER
  -> WAITING_FOR_MAINS
  -> MAINS_STABILIZING
  -> RESTORING_SERVER_POWER
  -> RECOVERY
  -> MAINS_ON
```

Возврат сети до shutdown переводит grace period в `MAINS_STABILIZING`. Возврат
сети после отправки shutdown не отменяет цикл: сервер должен завершить работу,
розетка — дать новый фронт питания после минимального OFF.

Недостоверный NUT, неоднозначная телеметрия и невозможность безопасного действия
ведут в `DEGRADED`; повреждение обязательной конфигурации — в `ERROR`. В обоих
состояниях автоматическое управление питанием запрещено.
