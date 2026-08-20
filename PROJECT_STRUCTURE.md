# PCHUB — структура проекта

## Назначение

PCHUB — система мониторинга окружения на **ESP32** с TFT-дисплеем и веб-панелью. Прошивка получает данные BME680/DS3231, ведёт журнал на SD-карту, загружает прогноз погоды, поддерживает Telegram и MQTT. Приложение `PCAgent` для Windows передаёт на устройство метрики ПК.

## Состав репозитория

```text
PCHUB/
├── src/                         # Исходный код прошивки ESP32
│   ├── main.cpp                 # Точка входа: загрузка и главный цикл
│   ├── API.cpp                  # HTTP API, OTA, история и фоновая загрузка CSV
│   ├── Sensors.cpp              # BME680/BSEC, DS3231, NTP
│   ├── Weather.cpp              # Геокодирование и прогноз Open-Meteo
│   ├── UI.cpp                   # Отрисовка TFT, вкладки, touch-интерфейс
│   ├── WebUI.html               # Исходник одностраничной веб-панели
│   ├── WebUI.h                  # Сгенерированная C++-версия WebUI.html
│   ├── SDCard.cpp               # Подключение и обслуживание SD-карты
│   ├── Logger.cpp               # CSV-журналы датчиков и погоды
│   ├── RuntimeSettings.cpp      # Хранение настроек в NVS
│   ├── PCAgent.cpp              # Приём метрик ПК по USB Serial
│   ├── MQTT.cpp                 # MQTT-клиент и команды
│   ├── Telegram.cpp             # Telegram-бот и пороговые уведомления
│   ├── Backlight.cpp            # PWM-подсветка и расписание яркости
│   └── Led.cpp                  # Управление RGB-светодиодом
├── include/                     # Заголовки модулей и общие структуры
│   ├── Config.h                 # GPIO, значения по умолчанию, интервалы
│   ├── Version.h                # Версия прошивки
│   ├── PCData.h                 # Структура метрик ПК и батареи
│   └── *.h                      # Публичные интерфейсы модулей src/
├── PCAgent/                     # Windows companion app (.NET 10, WinForms)
│   ├── Program.cs               # Точка входа приложения
│   ├── MainForm.cs              # Главное окно, настройки и tray-режим
│   ├── HardwareMonitor.cs       # Сбор метрик через LibreHardwareMonitor
│   ├── PcMetrics.cs             # Модель метрик ПК
│   ├── PcHubClient.cs           # Отправка метрик по Wi-Fi или Serial
│   ├── AppSettings.cs           # Локальные настройки агента
│   ├── PCAgent.csproj           # Проект .NET 10 Windows (x64)
│   ├── app.manifest             # Запрос прав администратора
│   ├── pchub.ico                # Иконка приложения
│   └── build.bat                # Сборка/публикация агента
├── tools/                       # Скрипты сборки
│   ├── pre_build.py             # Преобразует WebUI.html в WebUI.h
│   ├── build_web_ui.py          # Ручная сборка заголовка веб-интерфейса
│   ├── build_web_ui.bat         # Обёртка для Windows
│   └── build_firmware.bat       # Обёртка сборки прошивки
├── platformio.ini               # Конфигурация PlatformIO и зависимости
├── PCHUB.sln                    # Solution для IDE
├── README.md                    # Документация, настройка и API
├── .vscode/                     # Настройки VS Code
├── .pio/                        # Артефакты PlatformIO (генерируются)
└── .claude/                     # Локальные настройки ассистента
```

## Архитектура прошивки

`src/main.cpp` координирует систему:

1. Загружает сохранённые настройки, инициализирует дисплей, светодиод, датчики и SD-карту.
2. Подключается к Wi-Fi; при неудаче запускает точку доступа и captive portal.
3. В обычном режиме запускает HTTP API, веб-интерфейс, MQTT, Telegram, обмен с PC Agent и фоновую загрузку погоды.
4. Запускает загрузку истории SD в фоне; в цикле обновляет датчики и экран, синхронизирует NTP, пишет CSV-логи и контролирует Wi-Fi. Свежесть PC Agent определяется настраиваемым тайм-аутом (5–300 с, по умолчанию 30 с).

Основные потоки данных:

```text
BME680 + DS3231 → Sensors → UI / API / Logger / MQTT / Telegram
Open-Meteo       → Weather → UI / API / Logger
Windows PCAgent  → HTTP POST или Serial → PCData → UI / API
NVS              ↔ RuntimeSettings ← API / WebUI (включая Access, тайм-аут offline PC и экран при offline PC)
SD-card          ↔ Logger + API history
```

## Сборка

- Прошивка: PlatformIO, окружение определено в `platformio.ini`.
- Перед сборкой `tools/pre_build.py` упаковывает `src/WebUI.html` в `src/WebUI.h`; вручную править сгенерированный заголовок не следует.
- Агент: `PCAgent/PCAgent.csproj`, `net10.0-windows`, x64; зависимость — `LibreHardwareMonitorLib` 0.9.3.

## Важные точки изменения

| Задача | Файл(ы) |
|---|---|
| Добавить REST-метод или изменить JSON | `src/API.cpp`, соответствующий заголовок |
| Изменить экран или touch-логику | `src/UI.cpp`, `include/UI.h` |
| Изменить веб-панель | `src/WebUI.html`, затем сгенерировать `WebUI.h` |
| Изменить GPIO или дефолтные интервалы | `include/Config.h` |
| Добавить постоянную настройку | `src/RuntimeSettings.cpp`, `include/RuntimeSettings.h`, API/WebUI |
| Добавить метрику ПК | `PCAgent/*.cs`, `include/PCData.h`, `src/PCAgent.cpp`, `src/API.cpp`, `src/UI.cpp` |

## Генерируемые и локальные файлы

- `.pio/` содержит результаты PlatformIO и не является исходным кодом.
- `src/WebUI.h` является производным от `src/WebUI.html`.
- Настройки ESP32 сохраняются в NVS на устройстве; настройки Windows-агента — в `%LOCALAPPDATA%\\PCHub\\settings.json`.
