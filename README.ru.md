# SuvOS

SuvOS - прототип ОС на базе Linux с браузерным интерфейсом. Пользовательская
оболочка строится вокруг обычного Chromium, а системные действия проходят через
небольшой контролируемый слой SuvOS: `/init`, `/system/suvos`, `/data/suvos`,
`suvosd`, `suvos-gateway`, web UI, манифесты приложений, локализацию и
QEMU-сценарии запуска/проверки.

Alpine `v3.22` используется как источник ядра, BusyBox, musl и системных
пакетов. Это не продуктовая идентичность SuvOS.

Обычный ручной запуск сейчас поднимает GUI-профиль: Wayland/Cage, Chromium,
локальный gateway на `http://suv.os/` и Admin Explorer Code на
`http://suv.os/aec/`. На Apple Silicon `make run` по умолчанию использует
быстрый `aarch64` путь через QEMU-HVF; x86_64 QEMU/TCG оставлен как отдельный
совместимый режим запуска.

## Быстрый старт

```sh
make bootstrap-vendors
make run
```

`make bootstrap-vendors` подтягивает закрепленные vendor-репозитории из
`third_party/vendors.lock.json`:

- `SuvOS_AEC` -> `third_party/SuvOS_AEC`;
- `SuvOS_Chromium` -> `third_party/chromium` в lockfile. На source-build
  хосте полный checkout может лежать как `third_party/SuvOS_Chromium`, а
  `third_party/chromium` быть симлинком на него.

`make run` собирает GUI+AEC initramfs и запускает SuvOS в QEMU. В Chromium
открываются две вкладки: `http://suv.os/` для текущей системной web UI и
`http://suv.os/aec/` для AEC.

Правила работы с Chromium fork и локальный Docker gateway описаны в
[docs/chromium-build-run.md](docs/chromium-build-run.md). Для текущего
Windows/WSL x86_64 host-flow см. [docs/windows-wsl-x86_64.md](docs/windows-wsl-x86_64.md).
Основной цикл Chromium идет на текущем ПК; SuvOS VM используется после
host-level проверки.

## Требования

Для сборки и запуска нужны:

- Docker-совместимая среда выполнения, например Docker Desktop или OrbStack;
- QEMU; на macOS обычно используется Homebrew QEMU;
- Node.js/npm для проверки и сборки UI;
- доступ к сети при первом получении Alpine assets и vendor-репозиториев.

Повторные сборки используют локальные кэши, пока не меняются версии assets,
списки пакетов или vendor refs.

## Что уже есть

- initramfs-only загрузка с собственным `/init`;
- `/system/suvos` только для чтения после boot и writable `/data/suvos`;
- `suvosd` как привилегированный C++ control plane;
- `suvos` CLI и диагностический `suvosctl`;
- Unix socket API `/run/suvosd/control.sock`;
- `suvos-gateway`, который слушает только `127.0.0.1`;
- JSON endpoints: `/health`, `/api/status`, `/api/roles`, `/api/apps`,
  `/api/aec/status`;
- TypeScript UI из `src/ui`, собранный и установленный в `/system/suvos/ui`;
- Wayland/Cage/Chromium browser shell без GNOME/KDE/session manager;
- Chromium profile в `/data/suvos/chromium`;
- AEC как admin/debug explorer с root-доступом внутри гостевой VM;
- registry манифестов `/system/suvos/apps/manifest.d/*.app`;
- runtime-роли `setup` и `root`;
- bootstrap secret вне образа: `build/secrets/root-bootstrap.secret`;
- локализация `ru` и `en` через `SUVOS_LANG`;
- быстрый core test, full runtime test, dev/root test, AEC smoke и GUI smoke.

## Сборка

```sh
make
```

`make` собирает полный initramfs-профиль. Сборка получает Alpine `v3.22` assets,
собирает `suvosd`, `suvosctl`, `suvos-gateway`, `suvos-splash` и копирует
готовые файлы UI.

Основные выходные файлы:

```text
build/kernel/vmlinuz-<arch>
build/initramfs/suvos-initramfs.cpio.gz
build/initramfs/suvos-initramfs-<arch>.cpio.gz
```

Кэши и сгенерированные файлы:

```text
build/cache
build/kernel
build/assets
build/cache/rootfs-layers
build/cache/apk
```

Полезные команды:

```sh
SUVOS_REFRESH_ASSETS=1 make assets
SUVOS_REFRESH_LAYER_CACHE=1 make run
SUVOS_DISABLE_LAYER_CACHE=1 make run
make clean-layer-cache
make clean
make distclean
```

`make clean` не удаляет secrets и package/rootfs layer cache. `make distclean`
удаляет весь `build/`, включая сгенерированный bootstrap secret.

## UI

Исходники UI находятся в `src/ui`. В initramfs попадает только собранный
`build/ui`, который затем устанавливается в `/system/suvos/ui`.

```sh
npm run ui:check
npm run ui:fix
make ui
SUVOS_REFRESH_UI_BUNDLE=1 make ui
```

Сборка initramfs не пересобирает исходники UI напрямую: она только проверяет и
копирует `build/ui`.

## Vendor-форки

Крупные внешние проекты не клонируются в основной репозиторий SuvOS и не
коммитятся сюда исходниками. Они живут как отдельные локальные копии, закрепленные по SHA в
`third_party/vendors.lock.json`, и отдают SuvOS только готовые артефакты.

`SuvOS_AEC` - форк на базе Code - OSS для Admin Explorer Code:

```sh
make aec
SUVOS_AEC_REPO=/path/to/SuvOS_AEC make aec
SUVOS_AEC_DIST=/path/to/aec-rootfs.tar.gz make run
SUVOS_REFRESH_AEC=1 make aec
```

`SuvOS_Chromium` - vendor-репозиторий для Chromium-артефакта:

```sh
make chromium
SUVOS_CHROMIUM_REPO=/path/to/SuvOS_Chromium make chromium
SUVOS_CHROMIUM_DIST=/path/to/chromium-rootfs.tar.gz make run
SUVOS_REFRESH_CHROMIUM=1 make chromium
```

Текущий `SuvOS_Chromium` поставляет browser rootfs overlay. Более глубокие
патчи Chromium должны развиваться в этом vendor-репозитории, не в основном
репозитории SuvOS.

## Проверки

```sh
make test
```

Быстрый core test без Python/Node runtime-пакетов. Он загружает SuvOS в QEMU и
проверяет `suvosd`, `suvosctl`, HTTP gateway/UI, JSON endpoints, read-only
`/system/suvos`, writable `/data/suvos`, отказ при неверной авторизации и
пустой registry приложений.

```sh
make test-full
```

`make test-full` добавляет Python/Node runtime-пакеты и проверяет их наличие в
гостевой VM.

```sh
make test-dev
```

`make test-dev` собирает образ GUI+AEC с `apk-tools`, проверяет успешный
`suvos auth root <bootstrap-secret>` и наличие файлов Alpine package manager в
гостевой VM.

```sh
make test-aec-smoke
make test-gui-smoke
make test-gui-resolutions
```

AEC smoke проверяет артефакт, маршрут gateway `/aec/`, product metadata без
marketplace/cloud/chat defaults, нужные syntax/icon extensions и `node-pty`.
GUI smoke проверяет запуск Cage/Chromium, AEC-вкладку, профиль рендеринга,
пользователя `suvos-browser`, отсутствие штатного `--no-sandbox`,
health-проверки и screendump. Resolution test прогоняет стартовые размеры
1024x768 и 1440x900.

## Запуск

```sh
make run
```

Обычный ручной запуск собирает GUI+AEC профиль и открывает Chromium с вкладками
`http://suv.os/` и `http://suv.os/aec/`.

На Apple Silicon `make run` выбирает `run-arm64`: `aarch64` kernel/assets,
`aarch64` rootfs-пакеты, arm64-артефакт AEC и QEMU-HVF. На других хостах
дефолтный режим - x86_64 QEMU/TCG.

В WSL `make run` выбирает `run-windows-qemu`: initramfs собирается внутри WSL,
а окно QEMU запускается на Windows host через `scripts/run-suvos-windows-qemu.sh`.
Это текущий основной путь для целевой x86_64 проверки.

Явные режимы запуска:

```sh
make run-arm64
make run-qemu-x86
make run-dev
make run-core
make run-console
make run-graphics
make run-core-graphics
make run-gui
make run-gui-aec
make run-parallels
```

`make run-gui` и `make run-gui-aec` сохранены как совместимые псевдонимы для
старых ручных команд; основной GUI-путь сейчас `make run`.

`make run-dev` добавляет в гостевую систему `apk-tools`, `/etc/apk/repositories` и
`/etc/apk/keys`. Профиль не ставит пакеты сам: сеть используется только если
пользователь вручную запускает `apk`.

`make run-parallels` пока является защитной командой. Для текущего initramfs-only
прототипа Parallels CLI не дает полезного прямого `-kernel/-initrd` пути; для
реального Parallels/GPU трека нужен загрузочный arm64 disk/ISO-образ.

## GUI параметры

На macOS стартовое разрешение выбирается автоматически: примерно 90% основного
экрана, с верхним clamp около 2K. Переопределения:

```sh
SUVOS_GUI_SCALE_PERCENT=95 make run
make run SUVOS_GUI_WIDTH=1440 SUVOS_GUI_HEIGHT=900
SUVOS_GUI_MAX_WIDTH=0 SUVOS_GUI_MAX_HEIGHT=0 make run
```

Устройства ввода и звука QEMU тоже остаются переменными сборки и запуска:

```sh
make run SUVOS_GUI_INPUT_DEVICES="-device virtio-keyboard-pci -device virtio-tablet-pci"
make run SUVOS_GUI_AUDIO_DEVICES="-audiodev coreaudio,id=suvos-audio,out.mixing-engine=on -device virtio-sound-pci,audiodev=suvos-audio,streams=1"
```

Профиль рендеринга задается параметром ядра `suvos.render=<profile>`. Текущие
профили разработки используют `qemu-tcg` или `qemu-hvf`; неявное значение по
умолчанию остается `hardware`, чтобы реальный аппаратный путь не зафиксировался
в режиме software rendering.

## Runtime-схема

```text
Linux kernel
  -> /init
      -> mount /system/suvos read-only
      -> prepare /data/suvos
      -> suvosd
          -> FIFO CLI compatibility path
          -> /run/suvosd/control.sock
      -> suvos-gateway
          -> 127.0.0.1:80
          -> http://suv.os/
          -> http://suv.os/api/*
      -> suvos-start-aec      # GUI+AEC profile
          -> 127.0.0.1:3030/aec
      -> suvos-start-gui      # GUI profile
          -> Cage
              -> Chromium
```

`suvosd` остается привилегированным управляющим слоем. CLI, UI и gateway не должны
запускать произвольные системные пути напрямую.

`suvos-gateway` слушает только `127.0.0.1`. В гостевой системе `/etc/hosts`
сопоставляет `suv.os` с loopback. Endpoints для браузера возвращают
структурированный JSON, чтобы UI не парсил локализованный текст CLI.

## Файловая модель

```text
/system/suvos/
  bin/
  apps/
    manifest.d/
  ui/
  config/
  lib/
  security/
  aec/              # только в AEC profile

/data/suvos/
  aec/
  apps/
  chromium/
  extensions/
  logs/
  state/
  tmp/
```

`/system/suvos` монтируется только для чтения во время boot. Изменение этой политики
должно быть отдельным архитектурным решением, а не побочным эффектом разработки
AEC, UI или инструментов упаковки.

`/data/suvos` остается writable, но файлы и расширения из этой зоны не получают доверие
автоматически. Проверка манифестов, роли и capabilities остаются явной
границей.

`/opt/suvos` остается compatibility symlink на `/system/suvos`.

## Управляющий слой и API

Текущие важные endpoints:

```text
http://suv.os/
http://suv.os/health
http://suv.os/api/status
http://suv.os/api/roles
http://suv.os/api/apps
http://suv.os/api/aec/status
http://suv.os/aec/
```

`/health` означает готовность базового управляющего слоя: `suvosd`, socket API,
system root в режиме read-only и UI bundle. Это не просто факт запуска
gateway-процесса.

Действия из браузера, меняющие состояние системы, должны появляться только
после session token / capability layer. До этого UI остается диагностической
панелью и не должен разрастаться в root-панель.

## Манифесты приложений

Системные приложения описываются файлами:

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

`suvosd` запускает только приложения из manifest registry, проверяет capability
и канонизирует executable path. Имена с `/` или `..` блокируются. TSV registry
оставлен только как legacy fallback.

Текущий образ держит registry пустым: временные demo-приложения удалены.

## Роли и bootstrap secret

При загрузке SuvOS стартует в runtime-роли `setup`. Эта роль может читать
status, roles, app list и выполнить попытку `auth root`.

Root runtime-role разблокируется на текущую boot-сессию:

```sh
suvos auth root <bootstrap-secret>
```

Секрет хранится только снаружи образа:

```text
build/secrets/root-bootstrap.secret
```

В образ копируется только проверочный материал:

```text
/system/suvos/security/root-bootstrap.sha256
```

Для реального устройства это должно стать per-device provisioning / claim flow,
а не общим секретом для массового образа.

## Admin Explorer Code (AEC)

Admin Explorer Code, или AEC, - это отдельная admin/debug web-среда внутри
гостевой SuvOS VM. Он основан на Code - OSS fork `SuvOS_AEC`, поставляется в
SuvOS как готовый vendor-артефакт и устанавливается в `/system/suvos/aec` в
GUI+AEC профиле.

AEC дает проводник и терминал с root-доступом к файловой системе гостевой VM:

- дерево файлов гостевой VM;
- вкладки редактора/просмотра;
- просмотр текстов, логов и простых медиафайлов;
- встроенный терминал через `node-pty`;
- workspace, user data и extensions под `/data/suvos/aec` и
  `/data/suvos/extensions/aec`.

В обычном `make run` AEC доступен в Chromium на `http://suv.os/aec/`. В
гостевой системе сервер AEC слушает `127.0.0.1:3030/aec`, а `suvos-gateway`
проксирует `/aec/` и отдает `/api/aec/status`.

AEC - осознанное исключение для разработки и администрирования гостевой VM. Он
может иметь root-доступ к гостевой файловой системе, включая просмотр и
изменение runtime state. Это не пользовательский file picker и не модель
доверия для будущих обычных приложений.

Ограничения:

- AEC работает внутри гостевой VM и не дает доступ к файловой системе хоста;
- AEC-артефакт живет в отдельном `SuvOS_AEC` repo, не в основном репозитории SuvOS;
- AEC не должен ослаблять read-only boot policy для `/system/suvos`;
- запись в `/system/suvos` из AEC требует отдельного явного изменения boot
  policy;
- policy-aware пользовательский file API должен идти отдельным треком через
  `suvos-gateway -> suvosd`, а не через AEC.

По умолчанию терминал AEC держится в режиме software rendering:

```sh
SUVOS_AEC_TERMINAL_GPU=off make run
```

Эксперименты с WebGL renderer терминала должны оставаться частью отдельного
GPU/WebGL трека:

```sh
SUVOS_AEC_TERMINAL_GPU=auto make run
```

## Браузерная оболочка

GUI-профиль запускает обычный Chromium как Wayland-клиент внутри Cage. SuvOS не
использует GNOME, KDE или полноценный session manager для штатного UI-пути.

Важные правила:

- Chromium запускается не как root, а как `suvos-browser`;
- состояние браузера хранится в `/data/suvos/chromium`;
- `--no-sandbox` запрещен в штатной GUI-загрузке;
- `--kiosk` и `--app` не используются для основного browser shell;
- стандартные кнопки окна Chromium и caption resize/maximize отключаются
  через `--suvos-shell-hide-window-controls`, потому что Chromium является
  оболочкой ОС, а не обычным resizable приложением;
- `xwayland` может присутствовать только как runtime-зависимость пакета Cage;
- выход browser shell считается recoverable failure: `/init` перезапускает
  Cage/Chromium до лимита, затем показывает crash/fallback screen и возвращает
  serial console.

Системная панель, power UX, browser-chrome controls, privileged settings и
глубокий file picker должны решаться через `SuvOS_Chromium` patchset /
privileged internal pages, а не через разрастание обычной web-страницы
`http://suv.os/`.

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

Текущий boot image по умолчанию использует русский. Системные сообщения и
runtime-приложения должны читать `SUVOS_LANG`.

## GPU/WebGL

Текущий быстрый путь на Apple Silicon использует `aarch64` QEMU-HVF и render
profile `qemu-hvf`. Это стабильный путь разработки в VM, но пока не настоящий
путь с GPU/WebGL хоста: Chromium использует Mesa software fallback. Детали и критерии
готовности ускоренного пути описаны в [docs/gpu-webgl-roadmap.md](docs/gpu-webgl-roadmap.md).

## Лицензия

SuvOS распространяется под лицензией MIT. См. [LICENSE](LICENSE).

## Безопасность

- Не устанавливать SuvOS на хост-машину.
- Тестировать через QEMU.
- Не коммитить `build/` и package/rootfs caches.
- Не раскрывать содержимое `build/secrets/root-bootstrap.secret`.
- Не ослаблять read-only boot policy для `/system/suvos` без явного решения.
