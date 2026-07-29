# Эксплуатация и аварийное управление

## Проверка

```bash
mad-server-power-manager --status
mad-server-power-manager --doctor
mad-server-power-manager --print-ups
curl http://127.0.0.1:9187/api/v1/status
```

## armed

До завершения всех этапов должно быть:

```ini
[general]
armed=false
```

Изменение на `true` выполняется только вручную после резервной копии, проверки
SSH forced command, программной симуляции и контролируемого реального теста.
Программа никогда сама не меняет этот параметр.

## Аварийная остановка автоматики

```bash
sudo systemctl stop mad-server-power-manager
sudo sed -i 's/^armed=true$/armed=false/' \
  /etc/mad-server-power-manager/madServerPowerManager.ini
sudo systemctl start mad-server-power-manager
```

Ручное управление розеткой:

```bash
sudo systemctl stop mad-server-power-manager
plugctl --config /etc/mad-server-power-manager/plug_config.json --status
```

Команды `--off/--on` выполнять только с оператором рядом и после проверки
состояния Proxmox.

## Откат NUT на NUC

Начальная конфигурация сохранена в
`/root/madspm-backups/nut-initial-20260728T132843Z`. Для полного отката
остановить новые NUT-сервисы, восстановить файлы из backup и перезапустить NUT.
Proxmox в ходе текущего этапа не менялся.

## BIOS NUC

Отдельно вручную проверить `Restore on AC Power Loss`. Автоматически BIOS не
изменялся. Рекомендуемое значение зависит от схемы питания NUC; для независимого
менеджера обычно нужно, чтобы NUC сам возвращался после восстановления питания.
