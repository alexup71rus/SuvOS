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
- localhost-only HTTP gateway `suvos-gateway` на `http://suv.os/`;
- структурированные JSON endpoints для статуса, ролей и app registry;
- первая web UI-страница, отдаваемая через `suvos-gateway`;
- пустой app registry в `/system/suvos/apps/manifest.d` для будущих системных приложений;
- read-only системная зона `/system/suvos` после boot;
- writable-зона `/data/suvos` для будущих данных и расширений;
- базовая локализация `ru` и `en`.

## Сборка

```sh
make
```

Сборка получает kernel, минимальные graphics modules и static BusyBox из Alpine `v3.22`, а также собирает `suvosd`, `suvosctl`, `suvos-splash` и `suvos-gateway` через Docker/OrbStack. Python/Node runtime-пакеты ставятся только в явном full-профиле.

Alpine assets кэшируются в `build/cache`, `build/kernel` и `build/assets`. Если outputs уже есть, сборка не ходит в сеть. Принудительно обновить upstream assets можно так:

```sh
SUVOS_REFRESH_ASSETS=1 make assets
```

Alpine runtime/GUI/AEC rootfs-слои тоже кэшируются. Первый `make test-full` или GUI/AEC запуск собирает tar-слой в `build/cache/rootfs-layers` и APK download cache в `build/cache/apk`; следующие сборки с тем же Alpine image и тем же списком пакетов просто распаковывают готовый слой без длинного списка `Installing ...`.

Отдельные большие форки не хранятся внутри этого репозитория. Вместо submodules SuvOS использует `third_party/vendors.lock.json` и `make bootstrap-vendors`. Сейчас обязательных vendor checkout два: `SuvOS_AEC` в `third_party/aec` и `SuvOS_Chromium` в `third_party/chromium`. AEC отдает в SuvOS editor artifact, а `SuvOS_Chromium` - browser rootfs overlay artifact по pinned ref.

Управление layer cache:

```sh
SUVOS_REFRESH_LAYER_CACHE=1 make run
SUVOS_DISABLE_LAYER_CACHE=1 make run
make clean-layer-cache
make bootstrap-vendors
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

По умолчанию это быстрый core-тест. Он собирает initramfs без Python/Node runtime-пакетов, загружает SuvOS в QEMU с `suvos.autotest=1`, проверяет базовые команды, роли, пустой app registry, read-only защиту `/system/suvos`, затем выключает VM.

Полный тест:

```sh
make test-full
```

`make test-full` устанавливает Python/Node runtime-зависимости в initramfs и проверяет наличие этих runtime-пакетов без временных demo-приложений.

Явный developer/root-профиль:

```sh
make test-dev
```

`make test-dev` собирает GUI+AEC-профиль с `apk-tools`, проверяет positive-path для `suvos auth root <bootstrap-secret>` и подтверждает, что из root shell/AEC terminal доступна Alpine package manager-обвязка. Этот профиль не меняет обычный `make run`.

Быстрый ручной serial-запуск без Python/Node:

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

На macOS текущий Homebrew QEMU открывает окно через `-display cocoa`; `make run-graphics` пока использует `std` VGA как самый совместимый ранний режим. В этом режиме init загружает минимальные DRM/framebuffer modules, затем запускает `suvos-splash`, который рисует framebuffer boot-loader с надписью `SuvOS` и текущим статусом. Если framebuffer недоступен, boot продолжается через serial console и пишет диагностику. Зеленый framebuffer-экран теперь зарезервирован для crash/fallback состояния, например если GUI shell вышел и система вернулась в serial console.

Следующий GUI-этап описан в [SuvOS_CONCEPT.md](SuvOS_CONCEPT.md): Wayland runtime + Cage + обычный Chromium. Для MVP Chromium не должен запускаться в `--kiosk` или `--app`, потому что SuvOS shell должен сохранить вкладки, адресную строку и extensions UI. Cage нужен как минимальный compositor для одного maximized browser window без GNOME/KDE/window manager.

Обычный ручной запуск SuvOS:

```sh
make bootstrap-vendors
make run
# или явно:
make runos
# текущий backend:
make run-qemu
```

`make run` и `make runos` собирают GUI-профиль с AEC, устанавливают Alpine-пакеты `cage`, `xwayland`, `dbus`, `seatd`, `eudev`, `su-exec`, fonts, cursor theme, ALSA diagnostics и Mesa/Wayland-зависимости, а сам Chromium берут из vendor checkout `third_party/chromium` по pinned ref из `third_party/vendors.lock.json`. На Apple Silicon `make run` по умолчанию использует быстрый `aarch64` QEMU-HVF профиль (`suvos.render=qemu-hvf`) с arm64 AEC artifact. Старый x86_64 QEMU/TCG runner оставлен явно как `make run-qemu-x86`. Это тяжелый профиль: Chromium и AEC попадают в initramfs, поэтому он не используется в `make test`. После первого успешного build GUI-пакеты берутся из rootfs layer cache, пока не изменится список пакетов или Alpine image. QEMU GUI-профиль по умолчанию использует USB HID keyboard/tablet/mouse через `qemu-xhci`, CoreAudio + virtio-sound для звука и запускает Cage с `-d`, чтобы по возможности убрать client-side window controls. `eudev` нужен не как desktop environment, а как device discovery слой для libinput/wlroots; без него kernel может видеть `/dev/input/event*`, но Cage не получает мышь.

Admin Explorer Code входит в обычный manual run:

```sh
make run
```

`make run` запускает root-capable Admin Explorer Code внутри guest VM на `127.0.0.1:3030` и открывает Chromium с вкладками `http://suv.os/` и `http://suv.os/aec/`. AEC artifact по умолчанию собирается из checkout `third_party/aec`, который подтягивается через `make bootstrap-vendors` по pinned ref из `third_party/vendors.lock.json`. Для локальной разработки путь можно переопределить через `SUVOS_AEC_REPO`, а готовый artifact - через `SUVOS_AEC_DIST`. В этот профиль добавляется небольшой glibc payload для AEC server. `make run-gui` и `make run-gui-aec` сохранены как совместимые aliases для старых ручных команд.

Альтернативный runner для Parallels зарезервирован отдельной командой:

```sh
make run-parallels
```

На Apple Silicon этот target пока intentionally останавливается с диагностикой: быстрый `aarch64` kernel/initramfs/AEC artifact уже есть, но Parallels CLI не дает прямой QEMU-style `-kernel/-initrd` boot для текущего initramfs-only прототипа. Следующий шаг для Parallels/GPU - bootable arm64 disk/ISO image и отдельная проверка 3D acceleration/WebGL. Для текущей ручной работы используется `make run` через QEMU-HVF.

Cursor theme сейчас является заменяемой GUI-зависимостью, а не частью core-логики SuvOS. По умолчанию используется Alpine-пакет `adwaita-icon-theme`; заменить его можно так:

```sh
SUVOS_CURSOR_THEME_PACKAGE=breeze-icons make run
```

Chromium в этом режиме запускается как Wayland-клиент через `--ozone-platform=wayland`. `xwayland` присутствует только потому, что текущий Alpine-пакет Cage 0.2.0 пытается поднять XWayland server при старте; это не означает, что SuvOS переходит на X11. Текущий `SuvOS_Chromium` vendor repo пока собирает отдельный browser artifact на базе Alpine `chromium` package, но сам browser уже приезжает в SuvOS не из main repo, а из отдельного pinned vendor checkout. Cage/Chromium запускаются не от root, а от системного пользователя `suvos-browser` с профилем в `/data/suvos/chromium`. `--no-sandbox` запрещен в default GUI boot; включить его можно только явным dev escape через `suvos.allow_no_sandbox=1` или `SUVOS_CHROMIUM_ALLOW_NO_SANDBOX=1`.

Текущая вкладка `http://suv.os/` остается временной диагностической страницей control plane, а не финальной системой настроек. Закрытие Chromium через browser `X` пока не считается безопасным shutdown-flow. Settings UI ставит web-level `beforeunload` warning, но Chromium не гарантирует показ такого предупреждения при закрытии browser chrome. Поэтому default-поведение теперь recovery-first: если browser shell вышел или упал, `/init` перезапускает Cage/Chromium до 3 раз за 60 секунд. Если лимит исчерпан, SuvOS показывает зеленый crash/fallback screen и возвращается в serial console. Финальные OS-facing настройки, системная панель, shutdown/power UX и browser-chrome integration должны переехать в более глубокий Chromium patch/source fork или privileged internal page, а не наращиваться внутри текущей TypeScript-страницы.

Render profile задается через `suvos.render=<profile>`. Если параметр не указан, используется `hardware`, чтобы реальная ОС не осталась навсегда в software-only режиме. QEMU на Mac M через Cocoa/TCG запускается с `suvos.render=qemu-tcg`, а arm64/HVF - с `suvos.render=qemu-hvf`: Chromium в этих профилях использует ANGLE `gl-egl` поверх Mesa llvmpipe, а Vulkan/VAAPI отключены. Это быстрый и стабильный VM dev path, но не настоящий host GPU/WebGL. Текущее состояние и следующий Parallels/3D acceleration трек описаны в `docs/gpu-webgl-roadmap.md`.

`make run` на macOS автоматически выбирает стартовое разрешение примерно в 90% от logical-размера основного экрана. На текущем MacBook Pro это дает около `1552x1000` вместо старого `1280x800`. Авторазмер ограничен сверху значениями `SUVOS_GUI_MAX_WIDTH=1880` и `SUVOS_GUI_MAX_HEIGHT=1120`, чтобы внешний 4K/5K-monitor не создавал слишком тяжелый QEMU framebuffer.

Стартовое разрешение GUI-профиля можно менять без правки скриптов:

```sh
SUVOS_GUI_SCALE_PERCENT=95 make run
make run SUVOS_GUI_WIDTH=1440 SUVOS_GUI_HEIGHT=900
SUVOS_GUI_MAX_WIDTH=0 SUVOS_GUI_MAX_HEIGHT=0 make run
make test-gui-smoke SUVOS_GUI_WIDTH=1024 SUVOS_GUI_HEIGHT=768
make test-gui-resolutions
```

Это задает QEMU video device `virtio-vga,xres=...,yres=...,edid=on` и kernel mode-setting параметр `video=Virtual-1:...-32`, чтобы guest не только видел режим в списке DRM modes, но и реально стартовал в этом разрешении. `make test-gui-resolutions` автоматически проверяет стартовые режимы 1024x768 и 1440x900, включая размер QEMU screendump. Live resize окна QEMU зависит от связки QEMU Cocoa -> virtio-gpu -> Linux DRM -> wlroots/Cage и пока считается отдельной ручной/hardware-проверкой, а не гарантированной возможностью.

Input/audio devices тоже можно переопределить:

```sh
make run SUVOS_GUI_INPUT_DEVICES="-device virtio-keyboard-pci -device virtio-tablet-pci"
make run SUVOS_GUI_AUDIO_DEVICES="-audiodev coreaudio,id=suvos-audio,out.mixing-engine=on -device virtio-sound-pci,audiodev=suvos-audio,streams=1"
```

GUI smoke-test:

```sh
make test-gui-smoke
```

Он собирает тот же GUI+AEC-путь, что и обычный `make run`, и открывает окно QEMU на ограниченное время. Тест проверяет serial-лог на запуск Cage/Chromium, пользователя `suvos-browser`, render profile, input/audio devices, отсутствие default `--no-sandbox`, успешный `/health`, старт AEC-вкладки, ранний выход browser shell и fatal GL/GPU ошибки. В конце тест делает QEMU `screendump`, сверяет его размер с запрошенным стартовым разрешением и падает, если framebuffer остался на boot-loader или зеленом crash/fallback-экране.

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

Это запускает QEMU GUI с Chromium и вкладками `http://suv.os/` / `http://suv.os/aec/`.

Явный developer/root GUI-профиль:

```sh
make run-dev
```

`make run-dev` оставляет обычный `make run` lean, но добавляет в guest `apk-tools`, `/etc/apk/repositories` и `/etc/apk/keys`, чтобы под `root` можно было вручную ставить Alpine-пакеты из terminal/AEC. Никаких фоновых package/cloud обращений этот профиль сам по себе не делает; сеть используется только если пользователь сам запускает `apk`.

Для старого headless serial console:

```sh
make run-console
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
suvosctl ping
suvosctl status
suvosctl list
wget -q -O - http://suv.os/api/status
wget -q -O - http://suv.os/api/roles
wget -q -O - http://suv.os/api/apps
wget -q -O - http://suv.os/
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
  |     +-- http://suv.os/api/*
  +-- /system/suvos/bin/suvos-splash
  |     +-- optional /dev/fb0 loader/crash screen in suvos.graphics=1 mode
  +-- /system/suvos/bin/suvos-start-gui
  |     +-- optional Cage + ordinary Chromium shell in suvos.gui=1 mode
  +-- /system/suvos/bin/suvos-shell
        +-- suvos CLI
              +-- /run/suvosd/request
                    +-- suvosd проверяет и выполняет команды
```

`suvosd` не блокирует основной daemon loop: каждый request обрабатывается worker-процессом. Количество workers ограничено, app execution сейчас имеет timeout 30 секунд, timed-out apps убиваются по process group, output ограничен по размеру.

`suvosctl` нужен для проверки внутреннего Unix socket API. Основной интерактивный CLI пока остается `suvos`, а будущий HTTP gateway должен обращаться к `/run/suvosd/control.sock` так же, как `suvosctl`.

`suvos-gateway` - первый HTTP boundary для будущего web UI. Он слушает loopback-only `127.0.0.1:80`, а guest `/etc/hosts` мапит `suv.os` на `127.0.0.1`. Gateway отдает собранный UI dist из `/system/suvos/ui`, возвращает JSON и проксирует команды в `suvosd` через Unix socket. `/api/status`, `/api/roles` и `/api/apps` возвращают структурированные JSON-объекты для UI; `/api/run?name=<app>` остается command endpoint для будущих allowlisted manifests. `/health` теперь должен означать базовую готовность control plane: доступность `suvosd`, loopback API socket, read-only system root и наличие UI bundle, а не просто факт старта самого gateway-процесса. Текущие endpoints: `http://suv.os/`, `/ui/app.js`, `/ui/styles.css`, `/health`, `/api/status`, `/api/roles`, `/api/apps`, `/api/run?name=<app>`. Следующий security-шаг - session token для browser UI перед добавлением state-changing browser actions.

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

Приложения описываются manifest-файлами. Сейчас системный образ держит директорию registry пустой, без временных demo-приложений:

```text
/system/suvos/apps/manifest.d/<name>.app
```

Формат сейчас простой `key=value`:

```text
name=example
version=0.1.0
runtime=shell
path=/system/suvos/apps/example.sh
capability=app.example
ui_entry=
description.en=allowlisted system app
description.ru=разрешенное системное приложение
```

`suvosd` запускает только приложения из manifest registry, проверяет capability и канонизирует путь. TSV registry больше не является основным форматом; в коде оставлен только legacy fallback.

## Роли и Bootstrap-Secret

Текущий boot запускает SuvOS в runtime-роли `setup`. Эта роль может читать статус, смотреть список app manifests и делать попытку `auth root`. Полная runtime-роль `root` разблокируется командой:

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

SuvOS сейчас использует Alpine `v3.22` как upstream-источник kernel, BusyBox, musl и runtime-библиотек. Python/Node подтягиваются только в явном full-профиле. Собственный слой проекта: `/init`, `/system/suvos`, `/data/suvos`, `suvosd`, app manifests, локализация и будущая UI/service-модель.

Для прототипа такая зависимость нормальна. Позже ее можно заменить на Buildroot или другой контролируемый build pipeline.
