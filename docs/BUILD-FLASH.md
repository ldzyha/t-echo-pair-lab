# Збірка, прошивання й відновлення

## Linux / ChromeOS Linux

Попередні персоналізовані збірки цієї серії перевірялися з Python 3.13, PlatformIO 6.2.0, Nordic nRF52 platform 10.11.0 і Meshtastic CLI 2.7.11. Версії Python-інструментів наведено в `requirements-pair.txt`; платформа й бібліотеки задані у PlatformIO-файлах цього дерева, включно з власним варіантом T-Echo Plus.

```bash
sudo apt update
sudo apt install git python3-venv python3-pip build-essential
git submodule update --init --recursive
python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -r requirements-pair.txt
```

Помилка `externally-managed-environment` означає обмеження системного Python. Використовуйте venv; `python3-xyz` у повідомленні Debian є шаблоном, не назвою пакета. `--break-system-packages` тут не потрібний.

Команди виконуйте з кореня клонованого репозиторію. Для власної черги пари спочатку створіть локальні налаштування за [PAIRING.md](PAIRING.md). Для ручного способу скопіюйте `config-examples/PairSettings.example.h` у `src/mesh/PairSettings.local.h` та замініть обидва ID. Приклади ID не є ID ваших рацій. Без локальних ID збірка також можлива: власна черга не перехоплюватиме повідомлення, а Peer може показувати favorite-контакт.

## Збірка

```bash
python tools/build_firmware.py
python tools/run_host_tests.py
```

Пряма команда без helper:

```bash
python -m platformio run -e t-echo-plus -t mtjson
```

Результат: `.pio/build/t-echo-plus/firmware-t-echo-plus-<version>.*`. Helper копіює ZIP/UF2/HEX/ELF та SHA256-маніфест у `.artifacts/`. Каталог ігнорується Git: бінарний файл може містити ваші локальні ID.

Для host-тестів без збірки firmware можна окремо завантажити бібліотеки:

```bash
python -m platformio pkg install -e t-echo-plus
python tools/run_host_tests.py
```

Не змінюйте environment на `t-echo`, якщо у вас T-Echo Plus. Фізичні досліди серії виконувалися на Plus. Для нової публічної збірки після компіляції потрібен власний цикл перевірки обох рацій; результати попередньої встановленої версії його не замінюють. Інші плати в цій серії не перевірялися.

## Прошивання по одному пристрою

Закрийте серійні монітори, Web Serial і другий CLI для цього порту. Переконайтеся, що жодна інша програма не тримає USB.

```bash
python -m serial.tools.list_ports -v
python tools/flash_radio.py --port /dev/ttyACM0 --expected-node '!11223344' --package .artifacts/firmware-t-echo-plus-VERSION.zip
```

Замініть ID й точне ім’я ZIP. Потрібен serial DFU ZIP, **не файл `-ota.zip`**. Скрипт перевіряє ID підключеної рації, зберігає її конфігурацію у `.private/flash/`, переводить у DFU, знаходить завантажувач за тим самим USB-серійним номером і запускає `adafruit-nrfutil`. Він не виконує factory reset чи повне стирання. Це оновлення пристрою, не read-only діагностика; резервний YAML та UART-журнал можуть містити секрети й особисті дані.

Дочекайтеся `Firmware programmed`, потім повторіть для іншої рації з її портом/ID. Перевірте ключі, назви, регіон, сенсорні показники й доставку після запуску. [TESTING.md](TESTING.md) описує критерії; сама успішна передача ZIP їх не замінює.

## ChromeOS: USB змінюється під час DFU

Після входу в завантажувач пристрій може зникнути з Linux і з’явитися під іншим USB-описом. У ChromeOS повторно ввімкніть доступ Linux до нового USB-пристрою. Повідомлення про від’єднання старого serial-порту під час цього переходу очікуване.

Якщо helper завершився з `Bootloader not visible`, подивіться порти після надання доступу. Для вже відомого завантажувача використовуйте точний порт:

```bash
adafruit-nrfutil dfu serial --package .artifacts/firmware-t-echo-plus-VERSION.zip --port /dev/ttyACM0 --baudrate 115200 --singlebank
```

Упевніться, що це завантажувач потрібної рації. Якщо рація зависла до DFU, подвійне швидке натискання Reset зазвичай входить у завантажувач nRF52. Наявність диска UF2 залежить від режиму та USB-передавання ChromeOS; serial DFU не потребує змонтованого диска. Якщо диск з’явився, `INFO_UF2.TXT` допомагає ідентифікувати завантажувач.

## Windows

Ті самі Python-скрипти використовують порти `COM...`. Створіть venv через `py -3 -m venv .venv`, активуйте `.venv\Scripts\Activate.ps1`, установіть requirements. Компілятор `g++` потрібен лише для host-тестів; PlatformIO завантажує embedded toolchain окремо. Серійний DFU на Windows у цій серії не пройшов повний контрольний цикл — основний перевірений шлях Linux/ChromeOS.

## Скидання й відкат

Для звичайного оновлення скидання не потрібне. Meshtastic CLI 2.7.11 розрізняє:

- `--factory-reset` / `--factory-reset-config`: скидання конфігурації зі збереженням BLE bonds і PKI-ключів.
- `--factory-reset-device`: повне скидання, яке також очищає BLE bonds і PKI-ключі.

Після повного скидання старі контакти іншої рації можуть містити попередній відкритий ключ. Повторно звірте фізичні пристрої та імпортуйте перевірені контакти. Не використовуйте скидання як спосіб виправити температуру.

Зберігайте попередній робочий ZIP локально й його SHA256. Відкат виконується тим самим serial DFU способом без factory reset. Після відкату на upstream власний протокол черги/екрана може бути відсутній; резервна копія YAML не є резервною копією всіх файлів черги у flash.
