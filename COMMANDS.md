# ESP-HUB Serial Console — Все команды

## Как подключиться

**Требуется:**
- USB-кабель подключен к ESP32
- Терминал (Arduino IDE Monitor, PuTTY, VS Code, Putty и т.д.)
- **Скорость**: 115200 бод
- **Формат**: 8N1 (8 bit, No parity, 1 stop bit)

**Примеры подключения:**
```bash
# VS Code + PlatformIO:
> Terminal → New Terminal → pio device monitor -b 115200

# Arduino IDE:
> Tools → Serial Monitor → Select COM → 115200 baud

# Putty:
> Connection Type: Serial
> Serial Line: COM3 (или /dev/ttyUSB0)
> Speed: 115200

# Python + pyserial:
import serial
conn = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)
while True: 
    line = conn.readline().decode('utf-8', errors='ignore')
    if line: print(line.strip())
```

**Первое подключение:**
1. Откройте мониотр
2. Нажмите `Enter` для инициализации
3. Введите `help` для просмотра всех команд

## Советы

- **Регистронезависимость** — `help`, `HELP`, `Help` работают одинаково
- **Краткие команды** — `s` вместо `status`, `d` вместо `data`, `r` вместо `read`
- **ANSI-цвета** — включены по умолчанию; отключить в коде: `#define SC_ANSI 0`
- **Скорость вывода** — выводы групируются (`delay(10)`) для корректной работы медленных терминалов
- **Монитор** — команда `monitor` выводит данные каждые 2 секунды (нажмите Enter для выхода)

---

## Информация о системе
```
status / s      Полное состояние системы (WiFi, BLE, датчики)
sensors / d     Последние показания датчиков (кэшировано)
read / r        Прочитать датчики ЧИМ + показать значения
config / cfg    Полный дамп конфигурации (JSON)
url             Показать URL веб-портала
```

## Управление WiFi
```
wifi on         Включить радио WiFi (если выключена)
wifi off        Выключить WiFi (ADC2 станет доступна)
wifi status     Показать статус WiFi (IP, RSSI, SSID)
wifi scan       Сканировать доступные сети
scan            Аналогично wifi scan
```

## Bluetooth (BLE)
```
ble on          Запустить BLE сервер (если выключен)
ble off         Остановить BLE
ble status      Показать статус BLE (включен, количество клиентов)
```

## Mesh Network
```
mesh status     Показать статус Mesh (config/runtime, nodeId, peers)
mesh on         Включить Mesh в конфиге и сохранить (нужен reboot)
mesh off        Выключить Mesh в конфиге и сохранить (нужен reboot)
mesh nodes      Показать список узлов Mesh в JSON
mesh log        Показать журнал mesh-событий и выполненных команд
mesh chat <t>   Отправить chat всем узлам
mesh chat node:<id> <t>  Отправить chat на конкретный узел
mesh data <k> <v>        Отправить data пакет
mesh cmd <cmd>           Выполнить локально + отправить всем
mesh cmd node:<id> <cmd> Отправить команду конкретному узлу

set mesh on     Альтернативно через set-команду (save + reboot)
set mesh off    Альтернативно через set-команду (save + reboot)
```

## Управление светом (LED контроллер)
```
Основные команды:
  light off       Выключить все каналы
  light on        Включить с последними значениями
  light red       Red 100%
  light farred    Far Red 100%
  light blue      Blue 100%
  light white     White 100%
  light full      Все каналы 100%
  light grow      Гроу-режим (R70% FR50% B50% W30%)
  light set R<0-200> FR<0-200> B<0-200> W<0-200>   Произвольные значения

Быстрые команды (сокращения):
  R<n>      Red <n>% (0-100)       Пример: R75      → Red 75%
  FR<n>     Far Red <n>% (0-100)   Пример: FR50     → Far Red 50%
  B<n>      Blue <n>% (0-100)      Пример: B100     → Blue 100%
  W<n>      White <n>% (0-100)     Пример: W25      → White 25%
  
  Комбинированные: R75 B100  → Red 75% + Blue 100%

Диммирование:
  dim increase [step]    Увеличить яркость на step% (default: 10)
  dim decrease [step]    Уменьшить на step%
  light status           Показать текущую яркость всех каналов
```

## ⏰ Расписания и таймеры GPIO
```
Сценарии по времени:
  scenario list           Все сценарии
  scenario enable <N>     Включить сценарий #N
  scenario disable <N>    Отключить сценарий #N
  scenario status         Статус текущего сценария

Интервальные таймеры:
  timer list              Все таймеры
  timer enable <N>        Включить таймер #N
  timer disable <N>       Отключить таймер #N
  timer status            Статус таймеров
```

## ⏱️ Система времени и синхронизация
```
time / clock            Показать текущее время (NTP синхронизация / резервный таймер)
time HH:MM:SS          Установить время вручную
rtc status              Статус RTC (Real Time Clock)
```

## Конфигурация (изменение параметров)
```
Установка параметров:
  set wifi <SSID> [пароль]    Установить SSID и пароль WiFi
  set name <имя>              Название устройства
  set mqtt <хост> [порт]      MQTT брокер и порт (default: 1883)
  set ble <on|off> [имя]      Включить/выключить BLE, опционально имя
  set cpu <80|160|240>        Частота CPU (МГц)

Сохранение и перезагрузка:
  save                    Сохранить конфигурацию во FLASH
  reboot                  Перезагрузить ESP32
```

## GPIO и АЦП (Analog-to-Digital)
```
Цифровые входы/выходы:
  gpio <pin> read         Прочитать логический уровень (0 или 1)
  gpio <pin> 0|1          Установить значение (0=LOW, 1=HIGH)
  gpio <pin> pwm <0-255>  Установить ШИМ (0=0%, 255=100%)

Аналоговые входы (АЦП):
  gpio <pin> adc          Прочитать ADC1 (pins 32-39, всегда доступна)
  gpio <pin> adc2         Прочитать ADC2 (pins 0,2,4,12-15,25-27 с повторными попытками)
                          Важно: ADC2 недоступна при активном WiFi

Примеры:
  gpio 5 read             → Показать уровень на GPIO 5
  gpio 23 1               → Поднять GPIO 23 высоко (5V)
  gpio 32 adc             → Прочитать напряжение на GPIO 32
```

## Мониторинг и автоматизация
```
auto <сек>              Автоматический вывод датчиков каждые N секунд
auto off                Отключить автовывод

monitor                 Непрерывный мониторинг (каждые 2 сек)
                        Нажмите Enter для выхода

mqtt host|status        Статус MQTT подключения
mqtt subscribe <topic>  Подписаться на топик (требует переписки вручную)
```

## Конфигурация датчиков
```
sensors / data          Показать кэшированно последние значения
read / r                Прочитать все датчики ЧИМ + вывести значения
config                  Полный дамп конфигурации в JSON формате
url                     Показать URL веб-портала

Типы датчиков:
  - DHT (температура, влажность)
  - DS18B20 (температура)
  - BMP280 (давление, температура)
  - BH1750 (освещенность люкс)
  - MH-Z19 (CO2)
  - SDS011 (PM2.5, PM10)
  - MH-Sensor (многоцелевой)
  - CAN-шина (датчики через CAN)
  - UART (внешние датчики по последовательному порту)
  - Аналоговые (через АЦП)
```

## ❓ Справка
```
help            Показать это меню
```

## ⚙️ Конфигурация (set … → save → reboot)
```
set wifi <ssid> [pass]      Set WiFi network
set name <device name>      Set device name
set mqtt <host> [port]      Set MQTT broker
set ble on|off [name]       Enable/disable BLE
set cpu 80|160|240          Set CPU frequency
save                        Write config to flash
```

## GPIO (Пины)
```
gpio <pin> read             Read digital value (INPUT)
gpio <pin> 0|1              Set digital output (LOW/HIGH)
gpio <pin> adc              Read ADC1 (pin 32-39) — always works
gpio <pin> adc2             Read ADC2 (pin 0,2,4,12-15,25-27) with retry
gpio <pin> pwm <0-255>      Set PWM output
```

## Автоматизация
```
auto <sec>      Print sensor data every N seconds
auto off        Stop auto-print
monitor         Continuous read every 2s (press Enter to stop)
```

## Система
```
reboot / restart / rst      Restart ESP32
mqtt <host|port|interval|status>  MQTT configuration
help / ?                    Show this help
```

---

## REST API — Управление через Веб

### Что такое REST API?

REST API — это интерфейс для управления устройством через HTTP-запросы. Используется для:
- Интеграции с умным домом (Home Assistant, Node-RED)
- Мобильных приложений
- Скриптов автоматизации
- Внешних сервисов

**Базовый URL**: `http://<esp-ip>/api`

### Основные команды (примеры)

**Получить показания датчиков**:
```bash
curl http://192.168.1.100/api/data
```

**Получить статус системы**:
```bash
curl http://192.168.1.100/api/system
```

**Включить светильник красным + синим**:
```bash
curl -X POST http://192.168.1.100/api/fixture/set \
  -d "red=150&blue=100&white=0&far_red=0"
```

**Выключить светильник**:
```bash
curl -X POST http://192.168.1.100/api/fixture/off
```

**Установить GPIO пин (HIGH)**:
```bash
curl -X POST http://192.168.1.100/api/gpio/set \
  -d "pin=12&value=1"
```

**Сканировать WiFi сети**:
```bash
curl http://192.168.1.100/api/scan
```

**Получить статус WiFi**:
```bash
curl http://192.168.1.100/api/wifi
```

**Переключить таймер (toggle ON/OFF)**:
```bash
curl -X POST http://192.168.1.100/api/fixture/timer/enable \
  -d "index=0&enable=1"
```

### Тип данных для светильника

Все цветовые значения: **0-200** (где 200 = 100%, каждый шаг = 0.5%)
- `red` — красный канал
- `far_red` / `fr` — дальний красный
- `blue` — синий канал
- `white` — белый канал

### JavaScript (Fetch API)

```javascript
// Установить цвет
fetch('http://192.168.1.100/api/fixture/set', {
  method: 'POST',
  headers: {'Content-Type': 'application/x-www-form-urlencoded'},
  body: 'red=150&blue=100&white=50&far_red=0'
})
.then(r => r.json())
.then(d => console.log(d));

// Получить текущие показания
fetch('http://192.168.1.100/api/data')
  .then(r => r.json())
  .then(d => console.log(d));

// Включить GPIO пин
fetch('http://192.168.1.100/api/gpio/set', {
  method: 'POST',
  body: new URLSearchParams({pin: 12, value: 1})
})
.then(r => r.json());
```

### Python (requests)

```python
import requests

# Установить каналы светильника
requests.post('http://192.168.1.100/api/fixture/set', 
              data={'red': 100, 'blue': 150, 'white': 0})

# Получить статус
status = requests.get('http://192.168.1.100/api/fixture').json()
print(f"Light: R={status['red']}, B={status['blue']}")

# Включить GPIO
requests.post('http://192.168.1.100/api/gpio/set', 
              data={'pin': 12, 'value': 1})
```

### Полная справка по API

См. файл **[API.md](./API.md)** для:
- Полного списка всех эндпоинтов
- Детальные примеры параметров и ответов
- Примеры для cURL, JavaScript, Python
- Сценарии и таймеры светильника
- GPIO таймеры и автоматизация

---

## Примеры использования

### Прочитать датчик
```
> read
```

### Отключить WiFi и прочитать ADC2-датчик
```
> wifi off
> read
> wifi on
```

### Включить автоматический вывод каждые 5 секунд
```
> auto 5
```

### Отключить автопечать
```
> auto off
```

### Прочитать GPIO4 напрямую (ADC2 с retry)
```
> gpio 4 adc2
```

### Установить WiFi и сохранить
```
> set wifi AGROENGINEER MyPassword123
> set mqtt mqtt.example.com 1883
> save
> reboot
```

### Включить BLE и перезагрузиться
```
> set ble on MyDevice
> save
> reboot
```

---

## Шпаргалка — типичные операции

### Первый запуск (без сохранённых настроек)
```bash
> wifi scan                    # найти сеть
> set wifi SSID Password       # установить WiFi
> set mqtt mqtt.local 1883     # установить MQTT
> save                         # сохранить в flash
> reboot                       # перезагрузиться
```

### Управление светильником вручную
```bash
> light on              # включить (белый полной яркости)
> light grow            # режим роста (красный + дальний красный + синий)
> R100 B50              # красный 100%, синий 50%
> dim increase 20       # увеличить яркость на 20 шагов
> light off             # выключить все
```

### Проверка расписаний
```bash
> scenario list         # показать расписания
> scenario enable 0     # включить сценарий #0
> scenario disable 1    # отключить сценарий #1
> timer list            # показать таймеры
```

### Контроль времени
```bash
> clock                 # статус часов (NTP или Backup Timer)
# При наличии интернета:
#   NTP Status: SYNCED → 14:30:45
# При отсутствии:
#   NTP Status: NOT SYNCED - BACKUP TIMER ACTIVE → 0d 14:30:45
```

### Диагностика
```bash
> status               # все параметры (WiFi, BLE, heap, uptime)
> sensors              # показания датчиков (кешированные)
> read                 # считать датчики заново (принудительно)
> config               # полный конфиг в JSON
> wifi status          # статус подключения
> ble status           # статус BLE
> mqtt                 # статус MQTT
```

### GPIO операции
```bash
> gpio 4 read          # прочитать GPIO4 (0 или 1)
> gpio 12 1            # установить GPIO12 = HIGH
> gpio 13 0            # установить GPIO13 = LOW
> gpio 26 adc          # прочитать ADC GPIO26 (0-4095)
> gpio 4 adc2          # прочитать ADC2 GPIO4 с retry
> gpio 5 pwm 128       # PWM на GPIO5, значение 128 (0-255)
```

### Мониторинг в реальном времени
```bash
> monitor              # непрерывное чтение каждые 2 сек (Enter для выхода)
> auto 10              # печать датчиков каждые 10 сек
> auto off             # отключить автопечать
```

### Отладка сетевых параметров
```bash
> wifi scan            # сканировать доступные сети
> set wifi MyNetwork   # установить только SSID (пароль пусто)
> set wifi SSID Pass   # установить SSID и пароль
> wifi off             # отключить WiFi (высвобиться ADC2)
> wifi on              # включить WiFi
```

---

## ⚠️ Частые ошибки

| Ошибка | Причина | Решение |
|--------|---------|--------|
| `Unknown command` | Неверная команда или опечатка | Попробовать `help` для справки |
| `gpio <pin> adc2` не работает | WiFi занимает ADC2 | `wifi off` перед чтением ADC2 |
| Время не синхронизируется | Нет интернета | Система использует Backup Timer (нормально!) |
| MQTT не отправляет | Брокер неправильный | Проверить в `set mqtt <host>` |
| Расписание не срабатывает | NTP не синхронизирован или Backup Timer отстал | Проверить `clock` |
| Интервальные таймеры не работают | Таймер выключен | `timer list` проверить, `timer enable N` включить |

---

## Дополнительно

- **API Справочник** → [API.md](./API.md)
- **Резервный Таймер** → [BACKUP_TIMER.md](./BACKUP_TIMER.md)
- **Архитектура** → [README.md](./README.md)


## Что делать если ADC2-датчик не работает

**Проблема:** `gpio 4 adc2` возвращает -1 (timeout)

**Решение 1:** Временно отключить WiFi
```
> wifi off
> read
> wifi on
```

**Решение 2:** Перепаять датчик на ADC1 пин (GPIO 32-39)
- GPIO 32, 33, 34, 35, 36, 37, 38, 39 работают всегда (ADC1)
- GPIO 0, 2, 4, 12, 13, 14, 15, 25, 26, 27 конфликтуют с WiFi (ADC2)

---

## Скорость Serial (важно!)
- Baud rate: **115200**
- Data bits: 8
- Parity: none
- Stop bits: 1
- Flow control: none
