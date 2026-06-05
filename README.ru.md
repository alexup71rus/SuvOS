# SuvOS

SuvOS сейчас является минимальным x86_64 Linux-based прототипом ОС:

- готовое ядро Alpine `v3.22` `linux-virt`;
- собственный initramfs;
- собственный `/init`;
- консоль SuvOS;
- BusyBox для базовых Unix-совместимых команд;
- базовая ролевая система `setup`/`root`;
- root bootstrap secret генерируется вне образа, а в образ попадает только hash;
- статически собранный C++ daemon `suvosd` для привилегированных команд;
- CLI `suvos`, который общается с `suvosd` через FIFO IPC;
- внутренний Unix socket API в `/run/suvosd/control.sock` для будущего HTTP gateway;
- диагностический socket client `suvosctl`;
- framebuffer splash utility `suvos-splash` для первого графического smoke layer;
- опциональный GUI-профиль с Wayland/Cage/Chromium browser shell;
- localhost-only HTTP gateway `suvos-gateway` на `127.0.0.1:8080`;
- структурированные JSON endpoints для статуса, ролей и app registry;
- первая web UI-страница, отдаваемая через `suvos-gateway`;
- app manifests в `/system/suvos/apps/manifest.d/*.app`;
- read-only системная зона `/system/suvos` после boot;
- writable-зона `/data/suvos` для будущих данных и расширений;
- базовая локализация `ru` и `en`;
- статически собранное x86_64 C++ demo-приложение;
- runtime Python 3;
- runtime Node.js.

## Сборка

```sh
make
```

Сборка получает x86_64 kernel, минимальные graphics modules и static BusyBox из Alpine `v3.22`, устанавливает Python/Node runtime-зависимости в initramfs rootfs, а также собирает C++ demo, `suvosd`, `suvosctl`, `suvos-splash` и `suvos-gateway` через Docker/OrbStack.

Alpine assets кэшируются в `build/cache`, `build/kernel` и `build/assets`. Если outputs уже есть, сборка не ходит в сеть. Принудительно обновить upstream assets можно так:

```sh
SUVOS_REFRESH_ASSETS=1 make assets
```

Alpine runtime/GUI rootfs-слои тоже кэшируются. Первый `make`, `make test-full` или `make run-gui` собирает tar-слой в `build/cache/rootfs-layers` и APK download cache в `build/cache/apk`; следующие сборки с тем же Alpine image и тем же списком пакетов просто распаковывают готовый слой без длинного списка `Installing ...`.

Управление layer cache:

```sh
SUVOS_REFRESH_LAYER_CACHE=1 make run-gui
SUVOS_DISABLE_LAYER_CACHE=1 make run-gui
make clean-layer-cache
```

`make clean` layer cache не удаляет. `make clean-layer-cache` удаляет только Alpine package/rootfs layer cache. `make distclean` удаляет весь `build/`.

UI bundle тоже кэшируется в `build/ui` по hash исходников `src/ui/system-settings`, `tsconfig.ui.json`, `package.json`, `package-lock.json` и `tools/build-ui.mjs`. Сборка initramfs только проверяет готовый bundle и копирует его в образ; сам bundle создается отдельно через `make ui`.

```sh
make ui
SUVOS_REFRESH_UI_BUNDLE=1 make ui
```

Результаты:

```text
build/kernel/vmlinuz-x86_64
build/initramfs/suvos-initramfs.cpio.gz
```

## Проверка

```sh
make test
```

По умолчанию это быстрый core-тест. Он собирает initramfs без Python/Node runtime-пакетов, загружает SuvOS в QEMU с `suvos.autotest=1`, проверяет базовые команды, роли, read-only защиту `/system/suvos`, shell app и C++ app, затем выключает VM.

Полный тест:

```sh
make test-full
```

`make test-full` устанавливает Python/Node runtime-зависимости в initramfs и дополнительно проверяет `suvos run py-hello` и `suvos run node-hello`.

Быстрый ручной запуск без Python/Node:

```sh
make run-core
```

Проверка и сборка UI-слоя:

```sh
npm run ui:check
npm run ui:fix
make ui
```

UI-исходники лежат в `src/ui/system-settings`. В initramfs копируется только собранный dist из `build/ui`: `index.html`, `styles.css`, `app.js`.
Если исходники UI не менялись, `make ui` выводит `ui bundle cache hit: build/ui` и не запускает TypeScript build повторно.

Ручной запуск в окне QEMU:

```sh
make run-graphics
make run-core-graphics
```

На macOS текущий Homebrew QEMU открывает окно через `-display cocoa`; `make run-graphics` пока использует `std` VGA как самый совместимый ранний режим. В этом режиме init загружает минимальные DRM/framebuffer modules, затем запускает `suvos-splash`, который пытается залить `/dev/fb0` сплошным цветом. Если framebuffer недоступен, boot продолжается через serial console и пишет диагностику. Полноценный browser shell UI появится позже, когда в образ будет добавлен Wayland/Chromium stack.

Следующий GUI-этап описан в [SuvOS_CONCEPT.md](SuvOS_CONCEPT.md): Wayland runtime + Cage + обычный Chromium. Для MVP Chromium не должен запускаться в `--kiosk` или `--app`, потому что SuvOS shell должен сохранить вкладки, адресную строку и extensions UI. Cage нужен как минимальный compositor для одного maximized browser window без GNOME/KDE/window manager.

Экспериментальный запуск Chromium shell:

```sh
make run-gui
```

`make run-gui` собирает отдельный GUI-профиль с `SUVOS_WITH_GUI=1`, устанавливает Alpine-пакеты `cage`, `chromium`, `xwayland`, `dbus`, `seatd`, `eudev`, fonts, cursor theme, ALSA diagnostics и Mesa/Wayland-зависимости, затем грузит QEMU с `suvos.graphics=1 suvos.gui=1`. Это тяжелый профиль: Chromium скачивается и попадает в initramfs, поэтому он не используется в `make test`. После первого успешного build GUI-пакеты берутся из rootfs layer cache, пока не изменится список пакетов или Alpine image. QEMU GUI-профиль по умолчанию использует USB HID keyboard/tablet/mouse через `qemu-xhci`, CoreAudio + virtio-sound для звука и запускает Cage с `-d`, чтобы по возможности убрать client-side window controls. `eudev` нужен не как desktop environment, а как device discovery слой для libinput/wlroots; без него kernel может видеть `/dev/input/event*`, но Cage не получает мышь.

Cursor theme сейчас является заменяемой GUI-зависимостью, а не частью core-логики SuvOS. По умолчанию используется Alpine-пакет `adwaita-icon-theme`; заменить его можно так:

```sh
SUVOS_CURSOR_THEME_PACKAGE=breeze-icons make run-gui
```

Chromium в этом режиме запускается как Wayland-клиент через `--ozone-platform=wayland`. `xwayland` присутствует только потому, что текущий Alpine-пакет Cage 0.2.0 пытается поднять XWayland server при старте и падает без `/usr/bin/Xwayland`. Это не означает, что SuvOS переходит на X11. В MVP Chromium запускается от root с `--no-sandbox`; это только ранний dev-компромисс до появления нормального user/session слоя.

Стартовое разрешение GUI-профиля можно менять без правки скриптов:

```sh
make run-gui SUVOS_GUI_WIDTH=1440 SUVOS_GUI_HEIGHT=900
make test-gui-smoke SUVOS_GUI_WIDTH=1024 SUVOS_GUI_HEIGHT=768
```

Это задает `virtio-vga,xres=...,yres=...,edid=on`. Live resize окна QEMU зависит от связки QEMU Cocoa -> virtio-gpu -> Linux DRM -> wlroots/Cage и пока считается отдельной проверкой, а не гарантированной возможностью.

Input/audio devices тоже можно переопределить:

```sh
make run-gui SUVOS_GUI_INPUT_DEVICES="-device virtio-keyboard-pci -device virtio-tablet-pci"
make run-gui SUVOS_GUI_AUDIO_DEVICES="-audiodev coreaudio,id=suvos-audio,out.mixing-engine=on -device virtio-sound-pci,audiodev=suvos-audio,streams=1"
```

GUI smoke-test:

```sh
make test-gui-smoke
```

Он тоже собирает GUI-профиль и открывает окно QEMU на ограниченное время. Тест проверяет serial-лог на запуск Cage/Chromium и известные ошибки Cage/wlroots, но не доказывает визуально, что браузер корректно отрисовал страницу. Для визуальной проверки всё еще нужен ручной `make run-gui`.

Сборка создает внешний bootstrap-secret:

```text
build/secrets/root-bootstrap.secret
```

Этот файл не входит в initramfs и игнорируется git. В образ копируется только:

```text
/system/suvos/security/root-bootstrap.sha256
```

`make clean` не удаляет `build/secrets`, а `make distclean` удаляет весь `build/` и приведет к генерации нового bootstrap-secret при следующей сборке.

## Запуск

```sh
make run
```

Для запуска с окном вместо headless serial console:

```sh
make run-graphics
```

Внутри консоли:

```sh
help
ls
suvos status
suvos roles
suvos whoami
suvos auth status
suvos auth root <bootstrap-secret>
suvos list
suvos run hello
suvos run cpp-hello
suvosctl ping
suvosctl status
suvosctl list
suvosctl run hello
wget -q -O - http://127.0.0.1:8080/api/status
wget -q -O - http://127.0.0.1:8080/api/roles
wget -q -O - http://127.0.0.1:8080/api/apps
wget -q -O - 'http://127.0.0.1:8080/api/run?name=hello'
wget -q -O - http://127.0.0.1:8080/
suvos run py-hello
suvos run node-hello
python3 --version
node --version
poweroff
```

Файловая система первого этапа пока initramfs-only. Файлы, созданные вне read-only системной зоны, временные и пропадают после перезагрузки.

## Runtime-Схема

```text
/init
  +-- /system/suvos/bin/suvosd
  |     +-- /run/suvosd/request
  |     +-- /run/suvosd/control.sock
  +-- /system/suvos/bin/suvosctl
  +-- /system/suvos/bin/suvos-gateway
  |     +-- http://127.0.0.1:8080/api/*
  +-- /system/suvos/bin/suvos-splash
  |     +-- optional /dev/fb0 color fill in suvos.graphics=1 mode
  +-- /system/suvos/bin/suvos-start-gui
  |     +-- optional Cage + ordinary Chromium shell in suvos.gui=1 mode
  +-- /system/suvos/bin/suvos-shell
        +-- suvos CLI
              +-- /run/suvosd/request
                    +-- suvosd проверяет и выполняет команды
```

`suvosd` не блокирует основной daemon loop: каждый request обрабатывается worker-процессом. Количество workers ограничено, app execution сейчас имеет timeout 30 секунд, timed-out apps убиваются по process group, output ограничен по размеру.

`suvosctl` нужен для проверки внутреннего Unix socket API. Основной интерактивный CLI пока остается `suvos`, а будущий HTTP gateway должен обращаться к `/run/suvosd/control.sock` так же, как `suvosctl`.

`suvos-gateway` - первый HTTP boundary для будущего web UI. Он слушает только `127.0.0.1:8080`, отдает собранный UI dist из `/system/suvos/ui`, возвращает JSON и проксирует команды в `suvosd` через Unix socket. `/api/status`, `/api/roles` и `/api/apps` возвращают структурированные JSON-объекты для UI; `/api/run?name=<app>` остается command endpoint и возвращает `exitCode` плюс `output`. Текущие endpoints: `/`, `/ui/app.js`, `/ui/styles.css`, `/health`, `/api/status`, `/api/roles`, `/api/apps`, `/api/run?name=<app>`.

Файлы SuvOS лежат здесь:

```text
/system/suvos/
  bin/
  apps/
    manifest.d/
  ui/
  config/
  lib/
  security/

/data/suvos/
  apps/
  extensions/
  logs/
  state/
  tmp/
```

`/opt/suvos` является compatibility symlink на `/system/suvos`.

## App Manifests

Приложения описываются manifest-файлами:

```text
/system/suvos/apps/manifest.d/hello.app
```

Формат сейчас простой `key=value`:

```text
name=hello
version=0.1.0
runtime=shell
path=/system/suvos/apps/hello.sh
capability=app.hello
ui_entry=
description.en=allowlisted shell demo
description.ru=демо shell-приложение из manifest
```

`suvosd` запускает только приложения из manifest registry, проверяет capability и канонизирует путь. TSV registry больше не является основным форматом; в коде оставлен только legacy fallback.

## Роли и Bootstrap-Secret

Текущий boot запускает SuvOS в runtime-роли `setup`. Эта роль может читать статус, смотреть список app manifests, выполнять demo-приложения и делать попытку `auth root`. Полная runtime-роль `root` разблокируется командой:

```sh
suvos auth root <bootstrap-secret>
```

Секрет лежит только вне образа в `build/secrets/root-bootstrap.secret`. Внутри read-only системной зоны хранится только SHA-256 hash, поэтому сам секрет не раскрывается из VM-образа.

Это прототипная bootstrap-модель. В будущей браузерной UI первый запуск должен требовать создание обычного пользователя, а bootstrap-secret использовать как proof-of-ownership/claim code. Для реального устройства секрет лучше генерировать при provisioning конкретного устройства, а не переиспользовать один общий секрет для всех образов.

## Локализация

Язык по умолчанию задается в:

```text
/system/suvos/config/locale.conf
```

Поддерживаемые значения:

```text
SUVOS_LANG=ru
SUVOS_LANG=en
```

Текущий boot image по умолчанию использует русский. Системные сообщения и runtime-приложения должны читать `SUVOS_LANG`.

## Граница Alpine

SuvOS сейчас использует Alpine `v3.22` как upstream-источник kernel, BusyBox, Python, Node.js, musl и runtime-библиотек. Собственный слой проекта: `/init`, `/system/suvos`, `/data/suvos`, `suvosd`, app manifests, локализация и будущая UI/service-модель.

Для прототипа такая зависимость нормальна. Позже ее можно заменить на Buildroot или другой контролируемый build pipeline.
