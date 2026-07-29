# FinePower IEC LCD 3000VA: доступная телеметрия

Фактический вывод NUT на NUC после переноса:

| Переменная | Значение при обследовании |
|---|---:|
| `ups.status` | `OL` |
| `battery.charge` | 100 |
| `battery.voltage` | 54.0 V |
| `battery.voltage.high` | 52.00 V, оценка драйвера |
| `battery.voltage.low` | 41.60 V, оценка драйвера |
| `battery.voltage.nominal` | 48.0 V |
| `input.frequency` | 49.9–50.0 Hz |
| `input.voltage` | 230.0 V |
| `input.voltage.fault` | 0.0 V |
| `input.voltage.nominal` | 220 V |
| `output.voltage` | 230.0 V |
| `ups.load` | 4% |
| `ups.temperature` | 28.0 °C |
| `ups.beeper.status` | enabled |
| `ups.delay.shutdown` | 60 s |
| `ups.delay.start` | 0 s |
| `ups.type` | offline / line interactive |

Драйвер: `nutdrv_qx 0.36`, data protocol `Hunnox 0.02`, NUT 2.8.1,
libusb 1.0.27.

Недоступны `battery.runtime`, модель, серийный номер и производитель.
`battery.voltage.high/low` вычислены драйвером, а не сообщены ИБП; использовать
их как жёсткую гарантию нельзя.
