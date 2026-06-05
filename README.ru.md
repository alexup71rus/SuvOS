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

Сборка скачивает x86_64 kernel и static BusyBox из Alpine `v3.22`, устанавливает Python/Node runtime-зависимости в initramfs rootfs, а также собирает C++ demo, `suvosd`, `suvosctl` и `suvos-gateway` через Docker/OrbStack.

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

Ручной запуск в окне QEMU:

```sh
make run-graphics
make run-core-graphics
```

На macOS текущий Homebrew QEMU открывает окно через `-display cocoa`; `make run-graphics` пока использует `std` VGA как самый совместимый ранний режим. Этот режим нужен для framebuffer/графических экспериментов; полноценный браузерный kiosk UI появится позже, когда в образ будет добавлен Wayland/Chromium stack.

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
