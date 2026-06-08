# Chromium: сборка и запуск

Практическая инструкция, как собрать и запустить `SuvOS_Chromium` под две цели:

- **Linux** (arm64 и x86_64) — упаковка готового Alpine-пакета `chromium` в rootfs‑артефакт через Docker.
- **macOS arm64** — полноценная сборка из исходников (host-first dev loop).

Все команды и значения ниже проверены на Apple Silicon, macOS 26, Xcode 26.

---

## 1. Расположение

- Исходный форк: `third_party/SuvOS_Chromium` (полный чекаут Chromium, ~60 ГБ).
- Pinned-ревизия: `third_party/vendors.lock.json` → `vendors.chromium.ref`.
- Linux-артефакты складываются в `third_party/SuvOS_Chromium/dist/`.
- depot_tools для mac-сборки: `build/depot_tools` (gitignored).

---

## 2. Linux-артефакт (arm64 + x86_64)

Это **не сборка из исходников**, а упаковка апстрим‑пакета `chromium` из Alpine
в rootfs-overlay (`.tar.gz`), который SuvOS затем кладёт в образ. Требуется Docker.

### Через скрипт форка напрямую

```sh
cd third_party/SuvOS_Chromium

# arm64 (нативно на Apple Silicon)
CHROMIUM_TARGET_ARCH=aarch64 bash suvos/build-chromium-artifact.sh

# x86_64 (через эмуляцию linux/amd64, медленнее)
CHROMIUM_TARGET_ARCH=x86_64 bash suvos/build-chromium-artifact.sh
```

Результат:

- `dist/chromium-rootfs-aarch64.tar.gz`
- `dist/chromium-rootfs.tar.gz` и алиас `dist/chromium-rootfs-x86_64.tar.gz`

### Через основной репозиторий (под выбранную архитектуру SuvOS)

```sh
# арх берётся из scripts/suvos-arch.sh (по умолчанию arm64 на Apple Silicon)
make chromium

# явно:
SUVOS_ARCH=x86_64 make chromium
SUVOS_ARCH=aarch64 make chromium
```

`make chromium` (через `scripts/build-chromium.sh`) проверяет lockfile, при
необходимости вызывает `suvos/build-chromium-artifact.sh` и кладёт артефакт туда,
куда ждёт сборка образа.

### Полезные переменные

```sh
SUVOS_REFRESH_CHROMIUM=1            # пересобрать, даже если артефакт уже есть
SUVOS_REFRESH_CHROMIUM_CACHE=1      # игнорировать docker-кэш слоя
SUVOS_ALPINE_VERSION=3.22           # версия Alpine (источник пакета chromium)
SUVOS_CHROMIUM_PACKAGES=chromium    # список apk-пакетов
SUVOS_CHROMIUM_DIST=/path/art.tgz   # взять готовый артефакт, не собирать
```

---

## 3. macOS arm64 — сборка из исходников

> ⚠️ Кросс-сборка Chromium под Linux на macOS-хосте не поддерживается. На маке
> собираем только mac-таргет. Для Linux используем артефакт из раздела 2.

### 3.1. Предпосылки (разово)

- Xcode + Command Line Tools + macOS SDK.
- **Metal Toolchain** (в Xcode 26 это отдельный компонент, иначе падает сборка
  Metal-шейдеров ANGLE):

  ```sh
  sudo xcodebuild -runFirstLaunch
  xcodebuild -downloadComponent MetalToolchain
  xcrun metal --version   # должно отработать без ошибки
  ```

- depot_tools:

  ```sh
  git clone --depth 1 \
    https://chromium.googlesource.com/chromium/tools/depot_tools.git \
    build/depot_tools
  ```

### 3.2. Конфиг gclient (разово)

Chromium ожидает, что чекаут лежит в каталоге с именем `src`. Файл
`third_party/.gclient` уже настроен так:

```python
solutions = [
  {
    "name": "src",
    "url": "https://github.com/alexup71rus/SuvOS_Chromium.git",
    "managed": False,
    "custom_deps": {},
    "custom_vars": {},
  },
]
target_os = ["mac"]
```

и рядом стоит симлинк `third_party/src -> SuvOS_Chromium`, чтобы зависимости
gclient писались внутрь существующего чекаута. Если симлинка нет:

```sh
cd third_party
ln -s SuvOS_Chromium src
```

### 3.3. Подтянуть зависимости (gclient sync)

Тянет clang/rust/node toolchains и DEPS (десятки ГБ, долго):

```sh
cd third_party
export PATH="$PWD/../build/depot_tools:$PATH"
export DEPOT_TOOLS_UPDATE=0
gclient sync --no-history --shallow --jobs 8
```

Если `gn`/`gclient` ругается на `python3_bin_reldir.txt not found` — нужно один
раз дать depot_tools забутстрапить свой Python:

```sh
cd build/depot_tools
DEPOT_TOOLS_UPDATE=1 ./update_depot_tools
```

### 3.4. ⚠️ Обойти конфликт с корневым `node_modules`

У SuvOS свой `node_modules` в корне репозитория (UI-тулинг). TypeScript-сборка
webui Chromium поднимается по дереву каталогов и подхватывает его → ошибки
`TS2352` / `Undeclared dependencies to definition files`. Перед сборкой спрятать,
после — вернуть:

```sh
# перед сборкой
mv node_modules node_modules.hidden-during-chromium-build

# ... сборка ...

# после сборки (ОБЯЗАТЕЛЬНО вернуть)
mv node_modules.hidden-during-chromium-build node_modules
```

### 3.5. Сгенерировать конфигурацию (gn gen)

```sh
cd third_party/src
export PATH="/Volumes/T7/Projects/SuvOS/build/depot_tools:$PATH"
export DEPOT_TOOLS_UPDATE=0

gn gen out/Release --args='is_debug=false target_cpu="arm64" is_component_build=false symbol_level=0 blink_symbol_level=0 use_remoteexec=false'
```

### 3.6. Скомпилировать

```sh
cd third_party/src
export PATH="/Volumes/T7/Projects/SuvOS/build/depot_tools:$PATH"
export DEPOT_TOOLS_UPDATE=0

autoninja -C out/Release chrome
```

**Память (важно на 16–18 ГБ RAM).** По умолчанию siso запускает по числу ядер
параллельных компиляторов; на тяжёлых фазах (V8, Blink) пик памяти уходит за
30 ГБ и система виснет. Ограничивайте параллелизм флагом `-j`:

```sh
autoninja -C out/Release chrome -j 4    # тяжёлые фазы V8 / Blink core
autoninja -C out/Release chrome -j 8    # после них
autoninja -C out/Release chrome -j 12   # финал (мелкие объекты + линковка)
```

Сборка **инкрементальная**: можно прервать (`Ctrl-C` или
`pkill -INT -f siso`) и перезапустить — продолжит с места, объектные файлы
сохраняются. Так же безопасно менять `-j` между перезапусками.

Полная сборка с нуля занимает несколько часов; результат — `Build Succeeded`.

---

## 4. Запуск собранного mac arm Chromium

```sh
cd /Volumes/T7/Projects/SuvOS/third_party/src

# версия
out/Release/Chromium.app/Contents/MacOS/Chromium --version

# GUI с отдельным профилем
open out/Release/Chromium.app --args \
  --user-data-dir=/tmp/suvos-chromium-profile \
  --no-first-run \
  --no-default-browser-check
```

Против локального SuvOS-гейтвея (см. `docs/old_chromium-workflow.md`):

```sh
make chromium-dev-gateway   # поднимает http://127.0.0.1:8080/

open out/Release/Chromium.app --args \
  --user-data-dir=/tmp/suvos-chromium-profile \
  --no-first-run \
  http://127.0.0.1:8080/
```

Не добавляйте `--no-sandbox` в обычный запуск; используйте только как явный
одноразовый обход под конкретную проблему.

---

## 5. Частые ошибки

| Симптом | Причина | Решение |
| --- | --- | --- |
| `cannot execute tool 'metal' due to missing Metal Toolchain` | Xcode 26 не ставит Metal Toolchain по умолчанию | `xcodebuild -downloadComponent MetalToolchain` (при сбое сперва `sudo xcodebuild -runFirstLaunch`) |
| `python3_bin_reldir.txt not found` | depot_tools не забутстрапил Python | `DEPOT_TOOLS_UPDATE=1 ./update_depot_tools` в `build/depot_tools` |
| `TS2352` / `Undeclared dependencies to definition files //../../node_modules/...` | webui TS подхватывает корневой `node_modules` SuvOS | спрятать корневой `node_modules` на время сборки (раздел 3.4) |
| Мак виснет, процесс ест 30+ ГБ | siso запускает слишком много параллельных компиляторов | ограничить `-j` (раздел 3.6) |
| gclient клонирует второй чекаут в `third_party/src` (без `.git`) | solution назван не `src` или нет симлинка | имя solution `src` + симлинк `third_party/src -> SuvOS_Chromium` |
| Linux-артефакт не собирается | нет/не запущен Docker | запустить Docker Desktop; для x86_64 нужна эмуляция linux/amd64 |
