# ESP-HUB API Reference

Полная документация всех REST API endpoints ESP-HUB для управления датчиками, светильником, GPIO и системными параметрами.

**Базовый URL**: `http://<esp-ip>/api`  
**Формат ответов**: JSON (если не указано иное)  
**Авторизация**: Не требуется (все endpoints открыты)

---

## 1. Система и Статус

### 1.1 Статус системы
```
GET /api/system
```

Получить информацию о ESP32: heap, uptime, перезагрузки, версия.

Также содержит поля Mesh:
- `mesh_enabled` — включена ли Mesh сеть в конфигурации
- `mesh_connected` — есть ли активные соседние узлы
- `mesh_nodes` — количество подключенных узлов

### 1.2 Статус Mesh
```
GET /api/mesh
```

Получить текущий статус Mesh-сети.

**Ответ (200 OK)**:
```json
{
  "enabled": true,
  "status": "connected",
  "nodeId": 123456789,
  "connectedCount": 2,
  "nodes": [111111, 222222]
}
```

### 1.3 Отправить Mesh Chat
```
POST /api/mesh/chat
Content-Type: application/x-www-form-urlencoded

target=all&text=Hello
```

Отправить chat-сообщение в mesh (всем или конкретному узлу).

**Параметры**:
- `target` — `all` или `node:<id>`
- `text` — текст сообщения

**Ответ (200 OK)**:
```json
{"ok":true}
```

### 1.4 Отправить Mesh Data
```
POST /api/mesh/data
Content-Type: application/x-www-form-urlencoded

topic=raw&payload=test
```

Отправить произвольный data-пакет в mesh.

**Параметры**:
- `topic` — тема пакета
- `payload` — тело пакета

**Ответ (200 OK)**:
```json
{"ok":true}
```

### 1.5 Отправить Mesh Command
```
POST /api/mesh/cmd
Content-Type: application/x-www-form-urlencoded

target=all&run_local=1&cmd=light%20red
```

Отправить serial-команду в mesh.

**Параметры**:
- `target` — `all` или `node:<id>`
- `run_local` — `1/0` (выполнять ли локально на текущем узле)
- `cmd` — команда serial-консоли

**Примеры**:
- `target=all&run_local=1&cmd=mesh%20nodes`
- `target=node:123456&run_local=0&cmd=timer%20enable%200`

**Ограничения**:
- Блокируются только потенциально зацикливающие mesh-команды:
  - `mesh cmd ...`
  - `mesh chat ...`
  - `mesh data ...`

**Ответ (200 OK)**:
```json
{"ok":true}
```

### 1.6 Получить Mesh Log
```
GET /api/mesh/log
```

Получить журнал mesh-событий и выполненных команд.

**Ответ (200 OK)**:
```json
{
  "log": [
    "CMD API role=MAIN node=123 target=all run_local=1 cmd=mesh nodes",
    "EXEC LOCAL role=MAIN node=123 cmd=mesh nodes",
    "EXEC role=NODE node=987 from=123 target=all cmd=mesh nodes"
  ]
}
```

### 1.7 Очистить Mesh Log
```
POST /api/mesh/log/clear
```

Очистить журнал mesh-событий.

**Ответ (200 OK)**:
```json
{"ok":true}
```

### 1.8 Переключить Mesh
```
POST /api/mesh/toggle
```

Переключает `mesh_enabled` в конфиге и сохраняет его. Для применения нужен ребут.

**Ответ (200 OK)**:
```json
{
  "mesh_enabled": true,
  "message": "Mesh enabled. Please reboot to apply."
}
```

**Ответ (200 OK)**:
```json
{
  "device_name": "ESP-HUB",
  "uptime_seconds": 123456,
  "free_heap": 125432,
  "total_heap": 327680,
  "heap_percent": 38.3,
  "cpu_freq_mhz": 160,
  "reset_reason": 1,
  "flash_total": 3145728,
  "flash_used": 1572225,
  "flash_percent": 50.0
}
```

---

## 2. WiFi управление

### 2.1 Статус WiFi
```
GET /api/wifi
```

Получить текущий статус WiFi подключения.

**Ответ (200 OK)**:
```json
{
  "ssid": "MyWiFi",
  "ip": "192.168.1.100",
  "rssi": -65,
  "connected": true,
  "ap_enabled": false,
  "ap_ssid": "ESP-HUB",
  "ap_ip": "192.168.4.1"
}
```

**Поля**:
- `ssid` — STA режим: имя подключённой сети (если подключена)
- `ip` — STA IP адрес
- `rssi` — сигнал WiFi в дБ (отрицательное число, чем ближе к 0, тем лучше)
- `connected` — соединена ли с STA сетью
- `ap_enabled` — запущена ли точка доступа
- `ap_ssid` — имя AP
- `ap_ip` — IP адрес AP

### 2.2 Сканирование WiFi
```
GET /api/scan
```

Отсканировать доступные WiFi сети.

**Ответ (200 OK)**:
```json
[
  {
    "ssid": "MyWiFi",
    "rssi": -45,
    "channel": 6,
    "open": false
  },
  {
    "ssid": "Guest",
    "rssi": -72,
    "channel": 11,
    "open": true
  }
]
```

---

## 3. MQTT управление

### 3.1 Статус MQTT
```
GET /api/mqtt
```

Получить статус подключения к MQTT брокеру.

**Ответ (200 OK)**:
```json
{
  "enabled": true,
  "broker": "mqtt.example.com",
  "port": 1883,
  "connected": true,
  "last_publish": 12345,
  "interval_sec": 30
}
```

---

## 4. Датчики

### 4.1 Текущие показания
```
GET /api/data
```

Получить кэшированные показания всех активных датчиков.

**Ответ (200 OK)**:
```json
{
  "slot_0": {
    "sensor": "DHT22",
    "bus": "GPIO",
    "temperature": 22.5,
    "humidity": 45.3,
    "timestamp": 1699000000
  },
  "slot_1": {
    "sensor": "BMP280",
    "bus": "I2C",
    "temperature": 23.1,
    "pressure": 1013.25,
    "altitude": 100.5,
    "timestamp": 1699000001
  }
}
```

---

## 5. Bluetooth (BLE)

### 5.1 Статус BLE
```
GET /api/ble
```

Получить статус BLE и логи.

**Ответ (200 OK)**:
```json
{
  "enabled": true,
  "connected": false,
  "device_name": "ESP-HUB",
  "log": [
    "Device connected",
    "Device disconnected"
  ]
}
```

### 5.2 Отправка BLE характеристики
```
POST /api/ble/send
Content-Type: application/x-www-form-urlencoded

char_uuid=0x1234&value=12345
```

Отправить значение в BLE GATT характеристику (uint16).

**Параметры**:
- `char_uuid` — UUID характеристики (hex, напр. 0x1234)
- `value` — значение (целое число)

**Ответ (200 OK)**:
```json
{
  "status": "sent"
}
```

### 5.3 Очистить BLE лог
```
POST /api/ble/clear-log
```

Очистить внутренний логированный сессий BLE.

**Ответ (200 OK)**:
```json
{
  "status": "cleared"
}
```

---

## 6. Светильник (Fixture) управление

### 6.1 Статус светильника
```
GET /api/fixture
```

Получить текущее состояние светильника.

**Ответ (200 OK)**:
```json
{
  "enabled": true,
  "red": 50,
  "far_red": 0,
  "blue": 100,
  "white": 80,
  "red_percent": 25.0,
  "far_red_percent": 0.0,
  "blue_percent": 50.0,
  "white_percent": 40.0
}
```

**Примечание**: значения 0-200, где каждый шаг = 0.5%

### 6.2 Установить каналы светильника
```
POST /api/fixture/set
Content-Type: application/x-www-form-urlencoded

red=100&far_red=50&blue=150&white=0
```

Установить значения всех 4 каналов RGB+W.

**Параметры**:
- `red` — красный канал (0-200), необязательный
- `far_red` — дальний красный (0-200), необязательный
- `blue` — синий (0-200), необязательный
- `white` — белый (0-200), необязательный

**Ответ (200 OK)**:
```json
{
  "status": "set",
  "red": 100,
  "far_red": 50,
  "blue": 150,
  "white": 0
}
```

### 6.3 Включить светильник
```
POST /api/fixture/on
```

Включить светильник на последние сохранённые значения.

**Ответ (200 OK)**:
```json
{
  "status": "on",
  "red": 100,
  "far_red": 50,
  "blue": 150,
  "white": 80
}
```

### 6.4 Выключить светильник
```
POST /api/fixture/off
```

Выключить светильник (все каналы на 0).

**Ответ (200 OK)**:
```json
{
  "status": "off",
  "red": 0,
  "far_red": 0,
  "blue": 0,
  "white": 0
}
```

### 6.5 Переключить состояние
```
POST /api/fixture/toggle
```

Toggle между ON (последние значения) и OFF.

**Ответ (200 OK)**:
```json
{
  "status": "on",
  "red": 100,
  "blue": 150
}
```

или

```json
{
  "status": "off"
}
```

### 6.6 Затемнить светильник
```
POST /api/fixture/dim
Content-Type: application/x-www-form-urlencoded

step=10&operation=decrease
```

Изменить яркость активных каналов (увеличить/уменьшить).

**Параметры**:
- `step` — размер шага (0-200), по умолчанию 10
- `operation` — `increase` или `decrease`

**Ответ (200 OK)**:
```json
{
  "status": "dimmed",
  "red": 90,
  "blue": 140
}
```

### 6.7 Установить цвет
```
POST /api/fixture/color
Content-Type: application/x-www-form-urlencoded

r=200&fr=100&b=50&w=0
```

Удобный алиас для `/api/fixture/set` с короткими параметрами.

**Параметры**:
- `r` — красный (0-200)
- `fr` — дальний красный (0-200)
- `b` — синий (0-200)
- `w` — белый (0-200)

**Ответ (200 OK)**:
```json
{
  "status": "color_set"
}
```

### 6.8 Включить/выключить светильник
```
POST /api/fixture/enable
Content-Type: application/x-www-form-urlencoded

enable=1
```

Включить или выключить весь модуль управления светильником (не канальный toggle, а полное выключение).

**Параметры**:
- `enable` — 1 (включить) или 0 (выключить)

**Ответ (200 OK)**:
```json
{
  "status": "enabled",
  "fixture_enabled": true
}
```

### 6.9 Демонстрация светильника
```
POST /api/fixture/demo
```

Запустить встроенную демонстрацию (RGB цикл).

**Ответ (200 OK)**:
```json
{
  "status": "demo_running"
}
```

---

## 7. Сценарии светильника (Scenarios)

### 7.1 Получить все сценарии
```
GET /api/fixture/scenarios
```

Получить список всех 8 сценариев (TIME-triggered режимы).

**Ответ (200 OK)**:
```json
{
  "scenarios": [
    {
      "index": 0,
      "enabled": true,
      "start_hour": 8,
      "start_minute": 0,
      "start_second": 0,
      "red": 100,
      "far_red": 50,
      "blue": 150,
      "white": 80
    },
    {
      "index": 1,
      "enabled": false,
      "start_hour": 0,
      "start_minute": 0,
      "start_second": 0,
      "red": 0,
      "far_red": 0,
      "blue": 0,
      "white": 0
    }
  ]
}
```

### 7.2 Установить сценарии
```
POST /api/fixture/scenarios/set
Content-Type: application/x-www-form-urlencoded

sc_en_0=on&sc_h_0=8&sc_m_0=30&sc_s_0=0&sc_r_0=100&sc_fr_0=50&sc_b_0=150&sc_w_0=80
```

Установить параметры одного или нескольких сценариев.

**Параметры** (для каждого сценария N = 0-7):
- `sc_en_N` — включить (название параметра, значение не важно; отсутствие = disabled)
- `sc_h_N` — час начала (0-23)
- `sc_m_N` — минута (0-59)
- `sc_s_N` — секунда (0-59)
- `sc_r_N` — красный (0-200)
- `sc_fr_N` — дальний красный (0-200)
- `sc_b_N` — синий (0-200)
- `sc_w_N` — белый (0-200)

**Ответ (200 OK)**:
```json
{
  "status": "scenarios_saved"
}
```

### 7.3 Включить/выключить сценарий
```
POST /api/fixture/scenario/enable
Content-Type: application/x-www-form-urlencoded

index=0&enable=1
```

Runtime enable/disable одного сценария (без сохранения).

**Параметры**:
- `index` — индекс сценария (0-7)
- `enable` — 1 или 0

**Ответ (200 OK)**:
```json
{
  "status": "scenario_enabled",
  "index": 0
}
```

---

## 8. Таймеры светильника (Timers)

### 8.1 Получить все таймеры
```
GET /api/fixture/timers
```

Получить список всех 8 интервальных таймеров.

**Ответ (200 OK)**:
```json
{
  "timers": [
    {
      "index": 0,
      "enabled": true,
      "label": "Evening ON",
      "hours": 0,
      "minutes": 15,
      "seconds": 0,
      "action": 2,
      "duration_ms": 500,
      "run_hours": 2,
      "run_minutes": 30,
      "run_seconds": 0,
      "red": 100,
      "far_red": 50,
      "blue": 0,
      "white": 80
    }
  ]
}
```

**Значения `action`**:
- `0` = OFF (выключить)
- `1` = GROW (постепенное включение)
- `2` = FULL (полное включение)
- `3` = RED (красный импульс)
- `4` = BLUE (синий импульс)
- `5` = PULSE_GROW (импульс с нарастанием)
- `6` = PULSE_FULL (полный импульс)
- `7` = CUSTOM (собственные каналы)
- `8` = PULSE_CUSTOM (импульс с собственными каналами)

### 8.2 Установить таймеры
```
POST /api/fixture/timers/set
Content-Type: application/x-www-form-urlencoded

ften_0=on&ftlbl_0=Test&fth_0=0&ftm_0=15&fts_0=30&ftdur_0=500&ftact_0=2&ftrun_h_0=1&ftrun_m_0=30&ftrun_s_0=0&ftr_0=100&ftfr_0=50&ftb_0=0&ftw_0=80
```

Установить параметры таймеров.

**Параметры** (для каждого таймера N = 0-7):
- `ften_N` — включить (флаг)
- `ftlbl_N` — метка/описание (макс. 15 символов)
- `fth_N` — часы между срабатываниями (0-23)
- `ftm_N` — минуты (0-59)
- `fts_N` — секунды (0-59)
- `ftdur_N` — длительность импульса (мс)
- `ftrun_h_N` — часы работы до автовыключения
- `ftrun_m_N` — минуты работы
- `ftrun_s_N` — секунды работы
- `ftact_N` — действие (см. выше, 0-8)
- `ftr_N` — красный для CUSTOM (0-200)
- `ftfr_N` — дальний красный для CUSTOM (0-200)
- `ftb_N` — синий для CUSTOM (0-200)
- `ftw_N` — белый для CUSTOM (0-200)

**Ответ (200 OK)**:
```json
{
  "status": "timers_saved"
}
```

### 8.3 Включить/выключить таймер
```
POST /api/fixture/timer/enable
Content-Type: application/x-www-form-urlencoded

index=0&enable=1
```

Runtime enable/disable одного таймера.

**Параметры**:
- `index` — индекс таймера (0-7)
- `enable` — 1 или 0

**Ответ (200 OK)**:
```json
{
  "status": "timer_enabled",
  "index": 0
}
```

### 8.4 Срочное срабатывание таймера
```
POST /api/fixture/timer/trigger
Content-Type: application/x-www-form-urlencoded

index=0
```

Немедленно запустить действие таймера (не ждёшь регулярного срабатывания).

**Параметры**:
- `index` — индекс таймера (0-7)

**Ответ (200 OK)**:
```json
{
  "status": "timer_triggered",
  "index": 0
}
```

---

## 9. GPIO Управление

### 9.1 Получить состояние GPIO
```
GET /api/gpio
```

Получить состояние всех GPIO-таймеров (импульсной автоматизации).

**Ответ (200 OK)**:
```json
{
  "gpio_timers": [
    {
      "index": 0,
      "enabled": true,
      "pin": 15,
      "interval_sec": 3600,
      "pulse_ms": 500,
      "next_trigger": 1234567890
    }
  ]
}
```

### 9.2 Установить GPIO вывод
```
POST /api/gpio/set
Content-Type: application/x-www-form-urlencoded

pin=12&value=1
```

Установить цифровое значение на GPIO пин (1 = HIGH, 0 = LOW).

**Параметры**:
- `pin` — номер пина (0-48)
- `value` — 0 (LOW) или 1 (HIGH)

**Ответ (200 OK)**:
```json
{
  "status": "set",
  "pin": 12,
  "value": 1
}
```

### 9.3 Получить GPIO таймеры
```
GET /api/gpio/timers
```

Получить конфигурацию всех GPIO-таймеров.

**Ответ (200 OK)**:
```json
{
  "timers": [
    {
      "index": 0,
      "enabled": true,
      "pin": 15,
      "interval_sec": 3600,
      "pulse_ms": 500,
      "next_trigger": 1699000000
    }
  ]
}
```

### 9.4 Сохранить GPIO таймеры
```
POST /save/gpio-timers
Content-Type: application/x-www-form-urlencoded

gpt_en_0=on&gpt_pin_0=15&gpt_h_0=1&gpt_m_0=0&gpt_s_0=0&gpt_pulse_0=500
```

Установить и сохранить GPIO таймеры (автоматические импульсы).

**Параметры** (для каждого таймера N = 0-7):
- `gpt_en_N` — включить (флаг)
- `gpt_pin_N` — номер GPIO пина (0-48)
- `gpt_h_N` — часы между срабатываниями (0-23)
- `gpt_m_N` — минуты (0-59)
- `gpt_s_N` — секунды (0-59)
- `gpt_pulse_N` — длительность импульса (мс)

**Ответ (302 Redirect)**: перенаправление на `/system`

---

## 10. Конфигурация (Сохранение)

### 10.1 Сохранить WiFi
```
POST /save/wifi
Content-Type: application/x-www-form-urlencoded

wifi_ssid=MyNetwork&wifi_pass=secret123&ap_pass=12345678
```

Сохранить WiFi credentials и перезагрузиться.

**Параметры**:
- `wifi_ssid` — имя сети STA
- `wifi_pass` — пароль (опционально для открытых сетей)
- `ap_pass` — пароль точки доступа

**Ответ (200 OK)**: JSON или HTML редирект

### 10.2 Сохранить MQTT
```
POST /save/mqtt
Content-Type: application/x-www-form-urlencoded

mqtt_host=mqtt.example.com&mqtt_port=1883&mqtt_interval=30
```

Сохранить MQTT конфигурацию.

**Параметры**:
- `mqtt_host` — хост брокера
- `mqtt_port` — порт (по умолчанию 1883)
- `mqtt_interval` — интервал публикации (сек)

**Ответ (302 Redirect)**: на `/mqtt`

### 10.3 Запустить демонстрацию
```
POST /api/fixture/demo
```

Запустить встроенную RGB-демонстрацию светильника (цикл)

**Ответ (200 OK)**:
```json
{
  "status": "demo_running"
}
```

---

## 11. Системные операции

### 11.1 Перезагрузка ESP32
```
POST /reboot
```

Выполнить мягкую перезагрузку.

**Ответ (200 OK)**:
```json
{
  "status": "rebooting"
}
```

### 11.2 Сброс конфигурации
```
POST /reset
```

Очистить все настройки и вернуться к значениям по умолчанию (затем перезагрузка).

**Ответ (200 OK)**:
```json
{
  "status": "factory_reset"
}
```

### 11.3 Сброс WiFi credentials
```
POST /reset/wifi
```

Очистить сохранённые WiFi данные и силовой сброс в режим AP.

**Ответ (200 OK)**:
```json
{
  "status": "wifi_reset"
}
```

---

## 12. Примеры использования

### cURL

**Включить светильник красный + синий**:
```bash
curl -X POST http://192.168.1.100/api/fixture/set \
  -d "red=100&blue=150&far_red=0&white=0"
```

**Выключить светильник**:
```bash
curl -X POST http://192.168.1.100/api/fixture/off
```

**Получить текущие значения датчиков**:
```bash
curl http://192.168.1.100/api/data
```

**Сканировать WiFi сети**:
```bash
curl http://192.168.1.100/api/scan
```

### JavaScript (Fetch API)

**Установить RGB значения**:
```javascript
fetch('http://192.168.1.100/api/fixture/set', {
  method: 'POST',
  headers: {'Content-Type': 'application/x-www-form-urlencoded'},
  body: 'red=150&blue=100&white=50&far_red=0'
})
.then(r => r.json())
.then(data => console.log(data));
```

**Включить таймер**:
```javascript
fetch('http://192.168.1.100/api/fixture/timer/enable', {
  method: 'POST',
  headers: {'Content-Type': 'application/x-www-form-urlencoded'},
  body: 'index=0&enable=1'
})
.then(r => r.json())
.then(data => console.log(data));
```

**Получить статус WiFi**:
```javascript
fetch('http://192.168.1.100/api/wifi')
  .then(r => r.json())
  .then(data => console.log(data));
```

### Python (requests)

**Установить сценарий (время включения 8:00)**:
```python
import requests

url = 'http://192.168.1.100/api/fixture/scenarios/set'
data = {
    'sc_en_0': 'on',
    'sc_h_0': '8',
    'sc_m_0': '0',
    'sc_s_0': '0',
    'sc_r_0': '100',
    'sc_fr_0': '50',
    'sc_b_0': '150',
    'sc_w_0': '80'
}
response = requests.post(url, data=data)
print(response.json())
```

**Запустить GPIO импульс**:
```python
import requests

url = 'http://192.168.1.100/api/gpio/set'
response = requests.post(url, data={'pin': 12, 'value': 1})
print(response.json())
```

## 13. Статус коды ошибок

| Код | Описание |
|-----|----------|
| 200 | OK — успешно |
| 302 | Redirect — перенаправление (сохранение формы) |
| 400 | Bad Request — ошибка в параметрах |
| 404 | Not Found — эндпоинт не найден |
| 500 | Internal Server Error — ошибка сервера |

---

## 14. Примечания

- Все значения цвета (R, FR, B, W) номализованы: **0-200**, где каждый шаг = 0.5%. Таким образом, значение 200 = 100%.
  - Пример: `red=100` → 50.0% яркости
  
- Все таймеры и сценарии срабатываются по **системному времени** (RTC). Убедитесь, что время верно установлено (WiFi автоматически синхронизирует с NTP при успешном подключении).

- GPIO таймеры работают независимо и не требуют подключения к интернету, но могут быть синхронизированы через MQTT.

- Лог BLE хранится в памяти (до перезагрузки). При использовании `/api/ble/clear-log` всё стирается.

- Светильник может работать в нескольких режимах:
  - **Manual** — через endpoints `/api/fixture/set`, `/api/fixture/on`, `/api/fixture/off`  
  - **Scheduled** — сценарии по времени (каждый день в указанное время)  
  - **Interval** — таймеры срабатывают с заданным периодом  
  - **Trigger** — через `/api/fixture/timer/trigger` или GPIO-таймеры  

- Все конфигурационные изменения сохраняются в LittleFS NVRAM и переживают перезагрузку.
