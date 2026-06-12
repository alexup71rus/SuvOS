# Chromium: сборка и запуск

Практическая инструкция, как собрать и запустить `SuvOS_Chromium` под основные
цели:

- **Linux artifact** (arm64 и x86_64) — упаковка готового Alpine-пакета
  `chromium` в rootfs-артефакт через Docker.
- **Linux x86_64 source build** — полноценная сборка Chromium fork на
  Windows/WSL host для целевой SuvOS x86_64 платформы.
- **macOS arm64** — полноценная сборка из исходников (host-first dev loop).

Windows/WSL x86_64 детали отдельно зафиксированы в
`docs/windows-wsl-x86_64.md`.

---

## 1. Расположение

- Репозиторий форка: `https://github.com/alexup71rus/SuvOS_Chromium.git`.
- Pinned-ревизия: `third_party/vendors.lock.json` -> `vendors.chromium.ref`.
- Lockfile path: `third_party/chromium`.
- Полный source checkout может лежать как `third_party/SuvOS_Chromium`, а
  `third_party/chromium` быть симлинком на него. Это нормальная схема для
  source-build host; не создавайте второй независимый Chromium checkout.
- Linux-артефакты складываются в `dist/` внутри Chromium checkout.
- depot_tools для mac-сборки: `build/depot_tools` (gitignored).

Важно: `scripts/bootstrap-vendors.sh chromium` использует shallow sparse
checkout для текущего artifact entrypoint (`suvos/`). Этого недостаточно для
полной source-сборки Chromium. Для source build нужен полный checkout с `DEPS`,
`chrome/`, `third_party/`, `out/...` и результатами `gclient sync`.

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

## 3. Windows/WSL Linux x86_64 — сборка из исходников

Это текущий целевой путь для SuvOS x86_64. Рабочий checkout находится в WSL:

```text
/home/mypc/Projects/SuvOS
```

Windows-visible логи и QEMU artifacts лежат в:

```text
C:\Projects\SuvOS
```

Долгие сборки запускайте через Windows Task Scheduler, не из SSH-сессии.
Текущий лог Chromium source build:

```text
C:\Projects\SuvOS\build\ninja-chrome-shell-controls.log
```

Ручная команда внутри WSL, если уже есть persistent wrapper/scheduler:

```sh
cd /home/mypc/Projects/SuvOS
SUVOS_ARCH=x86_64 \
SUVOS_CHROMIUM_SOURCE_DIR=/home/mypc/Projects/SuvOS/third_party/chromium \
SUVOS_CHROMIUM_OUT_DIR=out/Linux_x64 \
SUVOS_CHROMIUM_TARGET=chrome \
SUVOS_CHROMIUM_JOBS=20 \
scripts/build-chromium-source.sh
```

После этого обязательно перепаковать artifact:

```sh
cd /home/mypc/Projects/SuvOS
SUVOS_ARCH=x86_64 \
SUVOS_CHROMIUM_SOURCE_DIR=/home/mypc/Projects/SuvOS/third_party/chromium \
SUVOS_CHROMIUM_OUT_DIR=out/Linux_x64 \
SUVOS_CHROMIUM_JOBS=20 \
scripts/package-chromium-source-artifact.sh
```

Не запускайте для этого обычный `ninja -C out/Linux_x64 -j20 chrome`: такой
запуск уже приводил к широкому dirty graph и обходил защиту от root
`node_modules`.

## 4. macOS arm64 — сборка из исходников

> ⚠️ Кросс-сборка Chromium под Linux на macOS-хосте не поддерживается. На маке
> собираем только mac-таргет. Для Linux используем артефакт из раздела 2.

### 4.1. Предпосылки (разово)

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

### 4.2. Конфиг gclient (разово)

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

### 4.3. Подтянуть зависимости (gclient sync)

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

### 4.4. ⚠️ Конфликт с корневым `node_modules`

У SuvOS свой `node_modules` в корне репозитория (UI-тулинг). TypeScript-сборка
webui Chromium поднимается по дереву каталогов и подхватывает его → ошибки
`TS2352` / `Undeclared dependencies to definition files`.

Обычная source-сборка должна запускаться через
`scripts/build-chromium-source.sh` или совместимый wrapper. Старый mac wrapper
`scripts/build-chromium-macos.sh` теперь просто вызывает общий source wrapper.
Скрипт сам временно прячет корневой `node_modules` и возвращает его после
завершения или ошибки.

Если сборка запускается напрямую через `siso`/`autoninja`, перед сборкой нужно
из корня SuvOS спрятать `node_modules`, после — вернуть:

```sh
cd /Volumes/T7/Projects/SuvOS

# перед сборкой
mv node_modules node_modules.hidden-during-chromium-build

# ... сборка ...

# после сборки (ОБЯЗАТЕЛЬНО вернуть)
mv node_modules.hidden-during-chromium-build node_modules
```

### 4.5. Сгенерировать конфигурацию (gn gen)

```sh
cd third_party/src
export PATH="/Volumes/T7/Projects/SuvOS/build/depot_tools:$PATH"
export DEPOT_TOOLS_UPDATE=0

gn gen out/Release --args='is_debug=false target_cpu="arm64" is_component_build=false symbol_level=0 blink_symbol_level=0 use_remoteexec=false'
```

### 4.6. Скомпилировать

```sh
cd /Volumes/T7/Projects/SuvOS
scripts/build-chromium-macos.sh
```

Скрипт резолвит Chromium checkout через `SUVOS_CHROMIUM_SOURCE_DIR`,
`SUVOS_CHROMIUM_REPO`, lockfile path `third_party/chromium`, затем fallback
`third_party/SuvOS_Chromium`. Он пишет лог в `build/ninja-chrome.log`,
временно прячет корневой `node_modules` SuvOS и возвращает его после завершения.
По умолчанию сборка ограничена 10 потоками:

```sh
cd /Volumes/T7/Projects/SuvOS
SUVOS_CHROMIUM_JOBS=10 scripts/build-chromium-macos.sh
```

Другие полезные переменные:

```sh
SUVOS_CHROMIUM_OUT_DIR=out/Release
SUVOS_CHROMIUM_TARGET=chrome
SUVOS_CHROMIUM_BUILD_LOG=/Volumes/T7/Projects/SuvOS/build/ninja-chrome.log
SUVOS_CHROMIUM_SOURCE_DIR=/Volumes/T7/Projects/SuvOS/third_party/SuvOS_Chromium
```

**Память (важно на 16–18 ГБ RAM).** По умолчанию используйте скрипт выше:
он вызывает `siso ninja ... -local_jobs 10`. Если система всё равно упирается в
память, перезапустите с меньшим значением `SUVOS_CHROMIUM_JOBS`.

Сборка **инкрементальная**: можно прервать (`Ctrl-C` или
`pkill -INT -f siso`) и перезапустить — продолжит с места, объектные файлы
сохраняются. Так же безопасно менять `-j` между перезапусками.

Для scheduled/non-interactive WSL jobs общий wrapper по умолчанию передает siso
`-fast_local`, потому что siso иначе отключает fast local в batch mode. Если это
нужно временно отключить:

```sh
SUVOS_CHROMIUM_SISO_FAST_LOCAL=0 scripts/build-chromium-source.sh
```

Полная сборка с нуля занимает несколько часов; результат — `Build Succeeded`.

---

## 5. Запуск собранного mac arm Chromium

```sh
cd /Volumes/T7/Projects/SuvOS/third_party/src

# версия
out/Release/SuvOS.app/Contents/MacOS/SuvOS --version

# GUI с отдельным профилем
open out/Release/SuvOS.app --args \
  --user-data-dir=/tmp/suvos-chromium-profile \
  --no-first-run \
  --no-default-browser-check
```

Против локального SuvOS-гейтвея (см. `docs/old_chromium-workflow.md`):

```sh
make chromium-dev-gateway   # поднимает http://127.0.0.1:8080/

open out/Release/SuvOS.app --args \
  --user-data-dir=/tmp/suvos-chromium-profile \
  --no-first-run \
  http://127.0.0.1:8080/
```

Старые build output каталоги могут ещё содержать `Chromium.app`; для текущего
брендированного форка используйте `SuvOS.app`.

Не добавляйте `--no-sandbox` в обычный запуск; используйте только как явный
одноразовый обход под конкретную проблему.

---

## 6. Где лежат SuvOS-правки в Chromium

Все пути ниже относятся к форку `third_party/SuvOS_Chromium`.

### 6.1. Строка состояния в области вкладок

SuvOS-строка состояния живёт не в обычном toolbar, а в правой части области
вкладок (`HorizontalTabStripRegionView`) рядом со стандартной кнопкой поиска по
вкладкам и системными кнопками окна.

Основные файлы:

- `chrome/browser/ui/views/frame/horizontal_tab_strip_region_view.cc`
- `chrome/browser/ui/views/frame/horizontal_tab_strip_region_view.h`
- `chrome/browser/ui/BUILD.gn`

В `HorizontalTabStripRegionView` добавлены три SuvOS-контрола:

- `SuvosNotificationsButton`
- `SuvosClockView`
- `SuvosPowerButton`

Их порядок в строке состояния сейчас такой:

```text
... tab strip / grab handle | notifications | clock/date | power
```

Важные места в `horizontal_tab_strip_region_view.cc`:

- создание контролов: рядом с комментарием `SuvOS: status controls shown at the trailing end of the tab strip`;
- удаление контролов в деструкторе, чтобы не оставались `raw_ptr`;
- `GetChildrenInZOrder()` / layout child list: там порядок, в котором FlexLayout
  раскладывает кнопки;
- блок с `UpdateBorderInsetsIfNeeded(...)`: там эти кнопки учитываются для
  border insets; если добавить новую кнопку, проверьте также
  `IsPositionInWindowCaption()`, иначе клики могут начать считаться
  перетаскиванием окна.

Все SuvOS-кнопки наследуются от Chromium `TabStripControlButton`. Это важно:
так они выглядят и ведут себя как стандартные кнопки этой же панели, например
`TabSearchButton`. Не заменяйте их на обычные `LabelButton`/самодельные views,
если цель - сохранить нативную верстку и состояния Chromium.

### 6.2. Часы, календарь и дата/время

Файлы:

- `chrome/browser/ui/views/frame/suvos_clock_view.cc`
- `chrome/browser/ui/views/frame/suvos_clock_view.h`

Поведение:

- кнопка показывает дату и время в формате вроде `Пн, 08 июн 23:20`;
- при клике открывает bubble с простым календарём и кнопкой `Настроить дату и время`;
- время берётся из SuvOS gateway `http://127.0.0.1:8080/api/time`;
- URL можно переопределить через `SUVOS_GATEWAY_TIME_URL`;
- если gateway недоступен, кнопка использует локальные часы host-системы;
- gateway не опрашивается каждую секунду: код синхронизирует offset и дальше
  тикает локально, периодически пересинхронизируясь.

Текущие ru/en подписи для этой статусной области сделаны локальным helper
`SuvosString()` внутри `.cc`. Если понадобится полноценная локализация шире
ru/en или строки должны попасть в Chromium translation pipeline, переносите их
в `.grd`/`.xtb` отдельно.

### 6.3. Уведомления

Файлы:

- `chrome/browser/ui/views/frame/suvos_notifications_button.cc`
- `chrome/browser/ui/views/frame/suvos_notifications_button.h`

Поведение:

- кнопка использует стандартный Chromium menu (`ui::SimpleMenuModel` +
  `views::MenuRunner`);
- пока notification store не подключён, меню показывает disabled placeholder
  `Тут будут уведомления` / `Notifications will appear here`;
- будущую интеграцию уведомлений лучше добавлять сюда, не в
  `horizontal_tab_strip_region_view.cc`, чтобы layout-код оставался только
  layout-кодом.

### 6.4. Питание

Файлы:

- `chrome/browser/ui/views/frame/suvos_power_button.cc`
- `chrome/browser/ui/views/frame/suvos_power_button.h`

Поведение:

- первый клик открывает Chromium menu с действиями `Выключить` и
  `Перезагрузить`;
- после выбора действия показывается стандартный confirm dialog;
- только после подтверждения кнопка отправляет `POST` в SuvOS gateway;
- browser сам не выключает host/guest напрямую;
- default endpoint: `http://127.0.0.1:8080/api/system/power/`;
- endpoint можно переопределить через `SUVOS_GATEWAY_POWER_URL`;
- поддерживаются действия `shutdown` и `reboot`; `logout` специально не
  показывается, потому что в SuvOS он пока не поддержан.

Если меняется контракт gateway/suvosd, сначала согласуйте это отдельно:
кнопка должна оставаться UI-слоем и не должна запускать системные команды
напрямую.

### 6.5. `suvos://` и бренд SuvOS

Схема `suvos://` добавлена как browser-internal WebUI scheme и работает как
алиас к существующим Chromium WebUI host'ам. `chrome://` оставлен совместимым,
чтобы не ломать внутренние страницы Chromium и devtools.

Основные файлы схемы:

- `content/public/common/url_constants.h`: `content::kSuvosUIScheme`;
- `chrome/common/url_constants.h`: `chrome::kSuvosUIScheme`;
- `chrome/common/chrome_content_client.cc`: регистрация standard/secure scheme;
- `chrome/browser/chrome_content_browser_client.cc`: additional WebUI scheme,
  internal scheme и address space;
- `content/public/common/url_utils.cc`: WebUI scheme;
- `content/renderer/render_thread_impl.cc`: renderer WebUI/display isolated
  registration;
- `content/public/browser/url_data_source.cc`: разрешение обслуживать
  `suvos://` data source requests;
- `content/public/browser/webui_config_map.cc`: canonical lookup
  `suvos://host` -> `chrome://host`;
- `content/browser/webui/web_ui_impl.cc`: разрешение локальных ресурсов для
  `suvos://` WebUI.

Важно: canonical lookup намеренно ограничен только `suvos://`. Не расширяйте
его на все `GetAdditionalWebUISchemes()`, иначе можно случайно сделать алиасы
вроде `devtools://settings`.

Бренд:

- `chrome/app/chromium_strings.grd`: `IDS_PRODUCT_NAME` и
  `IDS_SHORT_PRODUCT_NAME`;
- `chrome/app/theme/chromium/BRANDING`: macOS app/framework/helper bundle names.

Page Info для внутренних страниц:

- `components/components_chromium_strings.grd`: `IDS_PAGE_INFO_INTERNAL_PAGE`;
- `components/strings/components_chromium_strings_ru.xtb`: ru-перевод;
- `components/page_info/page_info.cc`: `suvos://` считается internal page;
- `chrome/browser/ui/views/page_info/page_info_bubble_view.cc`: bubble
  принимает `suvos://` как internal scheme.

### 6.6. Страница `SuvOS` в настройках

Страница доступна как:

```text
suvos://settings/suvos
```

Файлы:

- `chrome/browser/resources/settings/suvos_page/suvos_page.ts`
- `chrome/browser/resources/settings/suvos_page/suvos_page.html`
- `chrome/browser/resources/settings/BUILD.gn`
- `chrome/browser/resources/settings/router.ts`
- `chrome/browser/resources/settings/route.ts`
- `chrome/browser/resources/settings/page_visibility.ts`
- `chrome/browser/resources/settings/settings_menu/settings_menu.html`
- `chrome/browser/resources/settings/settings_menu/settings_menu.ts`
- `chrome/browser/resources/settings/settings_main/settings_main.html`
- `chrome/browser/resources/settings/settings_main/settings_main.ts`
- `chrome/browser/ui/webui/settings/settings_localized_strings_provider.cc`

Сейчас страница пустая и только регистрирует отдельный settings route/menu item.
Заголовок берётся из `IDS_PRODUCT_NAME` через localized string
`suvosPageTitle`, чтобы не хардкодить `SuvOS` в Polymer-шаблоне.

При добавлении новых секций в страницу:

- добавляйте UI в `suvos_page.html` / `suvos_page.ts`;
- держите настройки SuvOS внутри этой страницы, не форкайте всю
  `chrome://settings`;
- если нужны browser/OS данные, проводите их через Chromium WebUI handler или
  через SuvOS gateway API, а не прямыми системными вызовами из UI;
- после изменений settings WebUI почти всегда нужен mac source build через
  `scripts/build-chromium-source.sh`.

---

## 7. Частые ошибки

| Симптом | Причина | Решение |
| --- | --- | --- |
| `cannot execute tool 'metal' due to missing Metal Toolchain` | Xcode 26 не ставит Metal Toolchain по умолчанию | `xcodebuild -downloadComponent MetalToolchain` (при сбое сперва `sudo xcodebuild -runFirstLaunch`) |
| `python3_bin_reldir.txt not found` | depot_tools не забутстрапил Python | `DEPOT_TOOLS_UPDATE=1 ./update_depot_tools` в `build/depot_tools` |
| `TS2352` / `Undeclared dependencies to definition files //../../node_modules/...` | webui TS подхватывает корневой `node_modules` SuvOS | запускать через `scripts/build-chromium-source.sh` или спрятать корневой `node_modules` на время сборки (раздел 4.4) |
| Мак виснет, процесс ест 30+ ГБ | siso запускает слишком много параллельных компиляторов | ограничить `-j` (раздел 4.6) |
| gclient клонирует второй чекаут в `third_party/src` (без `.git`) | solution назван не `src` или нет симлинка | имя solution `src` + симлинк `third_party/src -> SuvOS_Chromium` |
| Linux-артефакт не собирается | нет/не запущен Docker | запустить Docker Desktop; для x86_64 нужна эмуляция linux/amd64 |
| Windows/WSL build умирает после закрытия SSH/сеанса | long job запущен не через persistent Windows mechanism | запускать через Task Scheduler, лог писать в `C:\Projects\SuvOS\build\...` |
| После маленького patch сборка снова показывает десятки тысяч targets | запускали plain `ninja` или поменяли build tooling/output state | вернуться к `scripts/build-chromium-source.sh`; не удалять `out/Linux_x64`, `.siso_*`, `.ninja_*` |
