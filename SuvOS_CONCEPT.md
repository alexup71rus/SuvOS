# SuvOS: browser-only OS concept

Дата: 2026-06-05

## Короткая формулировка

SuvOS - это минимальная операционная система, где единственный пользовательский интерфейс - полноэкранный браузер. Пользователь не видит классический рабочий стол, панель задач, оконный менеджер, кнопки закрытия окон и обычные системные приложения.

Внутри ОС находятся системные сервисы и скрипты на Python, C++ и Node.js. Они нужны для доступа к низкоуровневым функциям: файловая система, устройства, сеть, питание, обновления, запуск задач, системная диагностика. Эти файлы не должны быть доступны напрямую из браузерного UI. Браузер общается с ними только через ограниченный системный API.

Практичная трактовка: на первом этапе это не новая ОС с собственным ядром, а кастомная Linux-based ОС с минимальным rootfs, контролируемым init-процессом, системными демонами и браузером в browser-shell режиме. Собственное ядро можно рассматривать как отдельный исследовательский проект, но оно резко увеличит сложность и отложит браузерный UI на годы.

Главная идея SuvOS - не "Linux с браузером вместо рабочего стола", а легкий высокоуровневый системный слой, где большую часть пользовательского и административного UI можно писать на web-технологиях. HTML/CSS/JS проще для расширения, чем C++/низкоуровневый Linux userland, поэтому браузер становится оболочкой для настроек, панелей управления и пользовательских модулей. Низкоуровневая часть при этом остается маленькой, контролируемой и доступной только через системный API.

## Можно ли это сделать

Да, если начинать как Linux-based appliance OS: Linux kernel + минимальный userland + системные сервисы + Chromium в полноэкранном режиме.

Нет, если под "своей ОС" сразу понимать ядро, драйверы, файловые системы, сеть, графику, sandboxing и Chromium с нуля. Теоретически это возможно, но практически это многолетний проект даже для команды.

Самый рациональный путь:

1. Сначала собрать минимальную консольную систему, которая загружается в VM.
2. Добавить запуск доверенных скриптов через CLI.
3. Добавить системный сервис, который запускает только разрешенные операции.
4. Поднять локальный web UI.
5. Запустить Chromium как единственный UI через Wayland kiosk compositor.
6. Постепенно урезать лишнее, добавить read-only rootfs, обновления, watchdog и recovery-режим.

## Предлагаемая архитектура

```text
bootloader / firmware
        |
Linux kernel
        |
minimal root filesystem
        |
init / supervisor
        |
        +-- suvosd: privileged system daemon
        |       +-- C++ modules for low-level/hardware functions
        |       +-- Python scripts for orchestration
        |       +-- Node.js services where JS runtime is useful
        |
        +-- local API gateway
        |       +-- Unix socket control API
        |       +-- optional localhost-only HTTP gateway for the browser
        |       +-- authentication, allowlist, capability checks
        |       +-- extension manifest validation
        |
        +-- Wayland compositor (Cage for MVP)
                |
                +-- Chromium browser shell
                        |
                        +-- SuvOS web UI
                        +-- lazy-loaded settings and extension pages
```

## Web-first слой и расширяемость

SuvOS должна быть удобной для доработки людьми, которые умеют писать web UI и простые скрипты, но не хотят погружаться в C++, init-системы, Linux capabilities и сборку ядра.

Целевая модель расширения:

- разработчик пишет HTML/CSS/JS страницу для UI;
- разработчик пишет shell/Python/Node/C++ script или service для системного действия;
- расширение описывает себя manifest-файлом: имя, UI entrypoint, команды, требуемые capabilities;
- UI не вызывает скрипты напрямую, а обращается к локальному API;
- `suvosd` или API gateway проверяет manifest, роль, capability и allowlist;
- системная страница настроек может лениво подгружать модули управления: сеть, питание, файлы, обновления, диагностика, пользовательские расширения;
- системное ядро остается небольшим, а тяжелые и редкие UI-компоненты загружаются только когда нужны.

Важная граница: web-first не означает "браузеру можно все". Браузер - удобная оболочка, но не доверенная системная среда. Доверенным остается небольшой control plane: `init`, `suvosd`, policy, registry, signature/manifest validation и будущий update/recovery stack.

Для расширяемости нужна раздельная файловая модель:

- `/system/suvos` - read-only системная зона, поставляется образом или подписанным обновлением;
- `/data/suvos` - writable зона для пользователя, настроек, локальных данных и будущих установленных расширений;
- расширения из writable-зоны не получают root/capabilities автоматически;
- каждое расширение должно явно запросить capability, а пользователь или policy должны его выдать;
- HTML-страница без manifest/capability не должна уметь запускать системный код.

Эта модель оставляет возможность сделать SuvOS более открытой позже: добавить пользовательские расширения, dev mode, локальный package manager, импорт HTML-приложений. Но обратный путь сложнее. Если сначала сделать браузер root-панелью с прямым запуском скриптов, потом почти невозможно превратить это в безопасную и приватную систему без тяжелого переписывания.

Отличие от обычного Linux-дистрибутива и Chrome OS должно быть в продуктовой форме:

- нет полноценного desktop environment;
- нет панели задач, оконной модели и набора обычных desktop-приложений;
- локальная система не зависит от облачного аккаунта как базового сценария;
- минимальный read-only core;
- все системное управление идет через локальный, проверяемый API;
- приватность и отсутствие лишних фоновых сервисов считаются архитектурным требованием, а не опцией в настройках.

## Важная граница безопасности

Браузер нельзя считать доверенным. Если пользователь или сайт получает контроль над JS в браузере, он не должен получить контроль над системой.

Поэтому:

- UI не должен читать `/system/suvos`, `/usr/bin`, приватные скрипты и конфиги напрямую.
- UI не должен запускать произвольные команды.
- Все системные действия должны идти через `suvosd`.
- `suvosd` должен иметь allowlist команд и capability-модель: например, `readStatus`, `runScript:demo`, `reboot`, `installUpdate`.
- Для UI лучше использовать короткоживущий токен или сессионный ключ, выданный локальным bootstrap-сервисом.
- Системные скрипты должны лежать в read-only зоне и обновляться только подписанным обновлением.
- Низкоуровневые процессы нужно запускать от отдельных пользователей, с минимальными правами, seccomp/AppArmor/SELinux там, где это оправдано.

## Bootstrap-root и создание пользователя

Для раннего прототипа root-доступ в control plane оформляется через bootstrap-secret:

- во время сборки создается длинный случайный secret;
- secret хранится вне образа, сейчас в `build/secrets/root-bootstrap.secret`;
- в initramfs попадает только SHA-256 hash: `/system/suvos/security/root-bootstrap.sha256`;
- `suvosd` может проверить secret и разблокировать runtime-роль `root` только для текущей сессии;
- после reboot runtime-сессия снова стартует в роли `setup`;
- обычный пользователь пока не создается, потому что первый полноценный flow должен быть частью браузерного UI.

Это не финальная пользовательская модель. В будущем первый запуск должен требовать создание обычного пользователя до начала работы в UI. Пароль пользователя можно будет использовать для локальных сценариев вроде разблокировки устройства после сна, но root/bootstrap-secret должен оставаться отдельным механизмом владения устройством.

Если SuvOS будет поставляться как один общий образ для многих устройств, нельзя зашивать один общий root/claim secret в этот образ. Для реального устройства лучше генерировать уникальный claim code на этапе provisioning, привязывать его к device identity и хранить состояние в защищенном writable-разделе, TPM/secure enclave или через сервер активации. Hash в образе подходит для текущего VM-прототипа и индивидуальных dev-сборок, но не для массового одинакового образа.

## Почему браузер - не самая сложная часть

Внешне кажется, что сложнее всего "встроить браузер". На практике первые трудные места будут такими:

- надежная загрузка и восстановление после сбоя;
- безопасная связь между UI и системными функциями;
- запрет произвольного запуска кода из браузера;
- watchdog, который перезапускает браузер/UI при падении;
- read-only rootfs и отдельный writable-раздел для данных;
- обновления с откатом;
- управление сетью, питанием, временем, логами;
- драйверы и GPU/Wayland/input;
- лицензии и сопровождение Chromium, Node.js, Python и Linux-пакетов.

Убрать кнопки закрытия и панель задач относительно просто: не запускать desktop environment, не запускать обычный window manager, а стартовать один browser process как единственное maximized окно compositor.

## Рекомендация по виртуалке для Mac на M-чипе

Основная рекомендация: UTM.

Почему:

- UTM ориентирован на macOS и Apple Silicon.
- Для ARM64-гостей на Apple Silicon он использует аппаратную виртуализацию и работает близко к нативной скорости.
- Для x86_64-гостей на Apple Silicon он использует эмуляцию через QEMU. Это медленнее, но подходит для разработки VM-only ОС, если целевой образ должен быть именно x86_64.
- Внутри UTM есть QEMU, но с удобным UI.
- Можно быстро тестировать ARM64 Linux-образы, а позже перейти к прямому запуску через QEMU CLI.

Скачать: https://mac.getutm.app/

Что выбрать в UTM, если главный целевой формат - обычная PC VM:

- architecture: `x86_64`;
- machine: обычная PC/Q35 или дефолтная x86_64 machine в UTM/QEMU;
- guest OS для прототипа: минимальный Debian/Ubuntu/Alpine x86_64;
- boot logs выводить в serial console;
- ожидать, что графический этап с Chromium будет заметно тяжелее, чем консольный этап.

Что выбрать, если нужно быстрее проверить общую идею без привязки к x86_64:

- architecture: `aarch64`;
- guest OS: ARM64 Linux;
- это быстрее на Mac M, но такой образ не будет тем же самым x86_64 PC VM образом;
- включить serial console, чтобы видеть boot logs и не зависеть от графики.

Вывод: если мы хотим сделать ОС, которая должна запускаться как обычная x86_64 виртуальная машина у большинства людей, можно сразу целиться в `x86_64`. Это не концептуальная проблема. Просто на Mac M тестирование будет идти через эмуляцию, а не через быстрый hardware virtualization path.

Альтернативы:

- QEMU напрямую через Homebrew - лучше для автоматизированных тестов и будущего CI, но менее удобно для первых ручных экспериментов.
- VMware Fusion Pro - рабочий вариант, сейчас доступен бесплатно, поддерживает Apple Silicon и ARM Linux, но для низкоуровневых экспериментов QEMU/UTM обычно гибче.
- VirtualBox на Apple Silicon уже имеет ARM-host пакеты, но для этого проекта я бы не выбирал его первым: QEMU/UTM дают больше контроля над машиной, serial console, boot-параметрами и будущей автоматизацией.
- Parallels Desktop удобен для обычных ОС, но платный и не дает особого преимущества для разработки кастомной ОС.

## Что использовать для сборки ОС

На старте есть три реалистичных варианта.

### Вариант A: обычный ARM64 Linux в UTM

Самый быстрый путь к первому результату.

Ставим минимальный Debian/Ubuntu/Alpine, добавляем системную зону `/system/suvos`, пишем `suvos` CLI и системный сервис. Это еще не финальная ОС, но позволяет быстро проверить идею: загрузка, консоль, запуск скриптов, логи, API, прототип UI.

Плюсы: быстро, понятно, легко чинить.

Минусы: много лишнего, образ не выглядит как самостоятельная ОС.

### Вариант B: Buildroot

Хороший следующий шаг после прототипа. Buildroot генерирует toolchain, root filesystem, kernel image и bootloader. Подходит для маленькой, контролируемой системы.

Плюсы: компактно, понятно, хорошо для appliance OS.

Минусы: меньше похоже на обычную Linux-разработку, пакеты и обновления придется продумывать отдельно.

### Вариант C: Yocto

Мощный вариант для промышленной кастомной Linux ОС. Имеет слои, рецепты, воспроизводимые сборки, поддержку разных архитектур.

Плюсы: масштабируемо, промышленный стандарт для embedded/custom Linux.

Минусы: высокий порог входа, долгие сборки, много инфраструктуры. Для первого этапа избыточен.

Моя рекомендация: начать с варианта A, затем перенести рабочую минимальную систему в Buildroot. Yocto рассматривать позже, если проект станет большим и потребуется промышленная сборочная инфраструктура.

## Первый этап: консольная SuvOS

Цель первого этапа: получить систему, которая загружается в VM и дает консоль с возможностью запускать разрешенные скрипты.

Минимальный состав:

- x86_64 Linux VM;
- пользователь `suvos`;
- директория `/system/suvos`;
- app manifests в `/system/suvos/apps/manifest.d/*.app`;
- CLI `/system/suvos/bin/suvos`, доступный через `PATH`;
- логирование в `/var/log/suvos`;
- простой allowlist скриптов;
- запрет запуска произвольного пути.

Пример желаемого поведения:

```sh
suvos list
suvos status
```

Важное правило: команда `suvos run ../../anything` не должна работать. CLI должен запускать только заранее зарегистрированные сценарии из allowlist.

Пример структуры:

```text
/system/suvos/
  apps/
    manifest.d/
      <name>.app
  system/
    suvosd
  ui/
    index.html
```

Текущий статус первого этапа:

- есть bootable x86_64 Linux-based прототип;
- ядро берется из Alpine `linux-virt`;
- root filesystem собирается как initramfs;
- `/init` принадлежит SuvOS;
- консоль запускает SuvOS shell;
- базовые Unix-команды доступны через static BusyBox;
- есть фоновый `suvosd`, собранный как статический x86_64 C++ binary;
- `suvos` CLI общается с `suvosd` через FIFO IPC в `/run/suvosd`;
- внутренний control API доступен как Unix socket `/run/suvosd/control.sock`;
- `suvosctl` проверяет Unix socket API и служит диагностическим клиентом для будущего HTTP gateway;
- `suvos-gateway` слушает loopback-only `127.0.0.1:80`, а guest `/etc/hosts` мапит `suv.os` на `127.0.0.1`;
- первая web UI лежит в `/system/suvos/ui` и открывается через `http://suv.os/`;
- `/api/status`, `/api/roles` и `/api/apps` возвращают структурированный JSON, чтобы UI не парсил локализованный CLI-текст;
- `/api/run?name=<app>` остается command endpoint с `exitCode` и `output`;
- `suvos.graphics=1` режим загружает минимальные DRM/framebuffer modules, затем `suvos-splash` проверяет `/dev/fb0` и рисует framebuffer boot-loader/status screen;
- `suvos.gui=1` режим запускает экспериментальный browser shell через `suvos-start-gui`: Cage + обычный Chromium;
- `suvosd` выполняет `status`, `roles`, `list`, `run`;
- `suvosd run` запускает только приложения из `/system/suvos/apps/manifest.d/*.app`, а имена с `/` и `..` блокируются;
- каждый request обрабатывается отдельным worker-процессом, чтобы зависший app не останавливал основной daemon loop;
- запуск app имеет timeout, сейчас 30 секунд;
- timed-out app убивается по process group, чтобы дочерние процессы не оставались жить отдельно;
- количество одновременных request workers ограничено;
- пути из manifests канонизируются и допускаются только под `/system/suvos/apps` или `/system/suvos/bin`;
- `/system/suvos` bind-mounted read-only во время boot;
- `/data/suvos` создается как writable tmpfs-зона для будущего state/user/extensions слоя;
- базовая локализация включена через `/system/suvos/config/locale.conf`, поддерживаются `ru` и `en`;
- role policy описан в `/system/suvos/security/roles.conf`;
- boot по умолчанию стартует в runtime-роли `setup`;
- root bootstrap secret создается вне образа, а в образ попадает только SHA-256 hash;
- `suvos auth root <bootstrap-secret>` разблокирует runtime-роль `root` для текущей boot-сессии;
- `/system/suvos/apps/manifest.d` сейчас пустой: временные demo-приложения удалены, registry оставлен под будущие системные manifests;
- Python/Node runtime-пакеты ставятся только в явном full-профиле и не входят в обычный AEC GUI запуск.

Команды разработки:

```sh
make test       # быстрый core boot/control-plane test без Python/Node runtime
make test-full  # полный boot/runtime test с Python/Node
make run        # обычный GUI запуск SuvOS с Chromium и AEC
make runos      # явный alias для обычного GUI запуска
make run-qemu-x86 # старый x86_64 QEMU/TCG backend
make run-parallels # guarded Parallels runner; требует bootable arm64 disk/ISO
make run-console # serial/debug запуск full runtime image
make run-core   # serial/debug core image без Python/Node
```

Внутри консоли:

```sh
help
ls
mkdir demo
cd demo
suvos status
suvos roles
suvos list
python3 --version
node --version
poweroff
```

Важно: исходники не являются приложениями автоматически и не должны попадать в системный образ без необходимости. Frontend-исходники лежат вне rootfs в `src/ui` и попадают в образ только как собранный UI dist.

## Второй этап: системный демон

После консоли нужен `suvosd` - процесс, который работает с системными правами и принимает ограниченные команды.

Первичная схема:

- `suvos` CLI обращается к `suvosd`;
- `suvosd` проверяет app manifest registry и capability-политику;
- `suvosd` запускает только разрешенные операции;
- долгие операции выполняются в worker-процессах, а не в основном daemon loop;
- результаты пишет в лог и возвращает вызывающему процессу;
- ошибки не раскрывают лишнюю внутреннюю информацию.

Для внутреннего API выбран Unix domain socket `/run/suvosd/control.sock`. HTTP на `127.0.0.1` можно использовать позже для браузерного UI, но как отдельный gateway поверх socket, а не как расширение `suvosd` до web-сервера.

## Третий этап: web UI

Когда CLI и `suvosd` работают, можно добавить локальный web UI:

- UI source лежит в `src/ui`, а в `/system/suvos/ui` попадает только собранный dist;
- локальный сервер слушает только `127.0.0.1`;
- UI вызывает только API gateway;
- API gateway прокидывает команды в `suvosd`;
- browser-facing read endpoints возвращают структурированный JSON, а не текст CLI;
- права UI ограничены capability-моделью.

На этом этапе можно открывать UI обычным браузером внутри VM, еще без kiosk-режима.

## UI-слой: Chromium как shell ОС

Цель UI-слоя - запускать единственный GUI-процесс: обычный Chromium внутри минимального Wayland compositor. На MVP-этапе compositor должен заменить desktop/window manager, но не должен превращать Chromium в урезанное kiosk-приложение без вкладок.

Базовый boot flow:

```text
Linux kernel
  -> /init или supervisor
  -> graphics modules / DRM
  -> Wayland runtime
  -> Cage
  -> Chromium
```

MVP launch model:

```sh
cage -- chromium --ozone-platform=wayland --user-data-dir=/data/suvos/chromium --no-first-run http://suv.os/
```

Важное решение: для основного SuvOS shell не использовать Chromium `--kiosk` или `--app`. Эти режимы полезны для locked-down appliance screen, но конфликтуют с требованием сохранить вкладки, адресную строку, extensions UI и обычную браузерную механику. В SuvOS "fullscreen" означает, что Cage показывает единственное окно Chromium на весь экран без desktop environment, taskbar и стандартной оконной модели. Это не browser fullscreen mode.

`/system/suvos` остается read-only системной зоной. Chromium profile, extensions, history, downloads state и пользовательские browser-настройки должны жить в `/data/suvos/chromium`. Recovery/debug путь остается через serial console или отдельный dev boot flag, а не через полноценный desktop environment.

Текущий MVP запускает Chromium как Wayland-клиент через `--ozone-platform=wayland`. `xwayland` может присутствовать в образе как runtime-зависимость Alpine-пакета Cage, потому что Cage 0.2.0 в этой сборке пытается поднять XWayland server при старте. Это не должно становиться основным UI path; позже можно собрать Cage без Xwayland или заменить пакетную стратегию.

Root остается только supervisor'ом для подготовки `udev`, `dbus`, `seatd`, `/run/user/1000` и writable browser state. Cage и Chromium запускаются через `su-exec` под системным пользователем `suvos-browser` с UID/GID 1000 и группами `video`, `input`, `audio`. `--no-sandbox` не является default-флагом; он допускается только как явный dev escape через `suvos.allow_no_sandbox=1` или `SUVOS_CHROMIUM_ALLOW_NO_SANDBOX=1`.

Разрешение GUI-профиля задается на старте через QEMU `virtio-vga,xres=...,yres=...,edid=on` и kernel mode-setting параметр `video=Virtual-1:...-32`. Первый параметр объявляет режим guest DRM, второй заставляет guest реально стартовать в этом размере, а не оставаться на маленьком fallback `720x400`. `make run` на macOS выбирает размер примерно в 90% от logical-размера основного дисплея, с верхним clamp около 2K, чтобы окно было крупным, но framebuffer не становился чрезмерно тяжелым. Это остается параметризуемым через `SUVOS_GUI_WIDTH=... SUVOS_GUI_HEIGHT=...`; `make test-gui-resolutions` проверяет 1024x768 и 1440x900. Полноценный live resize окна нужно проверять отдельно: он зависит не от web UI, а от цепочки QEMU Cocoa -> virtio-gpu -> Linux DRM -> wlroots/Cage.

Render profile задается через `suvos.render=<profile>`. Default без параметра - `hardware`, чтобы реальный device path не был искусственно ограничен software rendering. QEMU Cocoa/TCG dev path использует `qemu-tcg`: Chromium запускается с ANGLE `gl-egl` и Mesa llvmpipe, а Vulkan/VAAPI отключены. Fatal `GLDisplayEGL`, SwANGLE/Vulkan и GPU-process initialization failures не считаются допустимым шумом: они означают, что browser shell может остаться на loader или перейти в restart/fallback path.

Cursor theme, QEMU input devices и audio backend относятся к заменяемому GUI runtime layer. Они не должны становиться частью core-логики SuvOS. Текущий default: Alpine cursor package, USB HID keyboard/tablet/mouse через `qemu-xhci`, CoreAudio + virtio-sound на macOS host. Для input discovery нужен `eudev`: kernel modules создают `/dev/input/*`, а udev metadata позволяет libinput/wlroots/Cage увидеть эти устройства как usable seat devices.

### Cage как MVP compositor

Cage подходит для первого GUI-этапа, потому что это Wayland kiosk compositor для запуска одного maximized приложения. Это ровно то, что нужно, чтобы убрать GNOME/KDE, taskbar и window manager из пользовательской модели.

Ограничение Cage: он не создает ChromeOS-like shell UI сам. Он не добавит системную панель в Chromium, не заменит browser toolbar, не реализует notification tray и не перехватит file picker. Эти части должны быть реализованы в web shell/backend API, через Chromium policies/profile configuration или через будущий patchset Chromium.

### OS shell внутри Chromium

MVP-модель:

- стартовая вкладка открывает `http://suv.os/`;
- settings dashboard обслуживается `suvos-gateway`;
- UI общается только с `suvos-gateway`;
- `suvos-gateway` общается с `suvosd` через Unix socket `/run/suvosd/control.sock`;
- все системные действия проверяются через role/capability policy;
- close guard для стартовой settings tab сейчас ограничен web-level `beforeunload` warning, но на него нельзя полагаться для browser `X`;
- выход или crash browser shell считается recoverable failure: `/init` перезапускает Cage/Chromium до 3 раз за 60 секунд, затем показывает зеленый crash/fallback screen;
- подтвержденный shutdown-flow должен идти через Chromium patchset или privileged internal page;
- текущая `http://suv.os/` страница должна оставаться временной диагностической control-plane вкладкой, а не разрастаться в финальный settings product;
- notification feed, power UX, системная панель, browser-chrome controls и OS-facing settings должны переехать в Chromium fork / privileged internal page.

Более глубокая модель:

- системные настройки становятся закрепленной системной вкладкой или privileged internal page;
- системная панель с датой, уведомлениями, сетью и питанием становится частью browser chrome;
- Chromium UI знает о SuvOS roles/capabilities;
- permission UI для файлов, устройств, сети и системных действий связан с `suvosd` policy.

### Без форка Chromium vs Chromium patch

Без форка Chromium можно:

- запускать обычный Chromium под Cage;
- сохранить вкладки, адресную строку и расширения;
- открывать SuvOS settings как обычную web tab;
- настроить profile dir, default homepage, startup URL и базовые policies;
- сделать web-based file manager/picker для SuvOS settings;
- поставить best-effort `beforeunload` warning для стартовой системной вкладки;
- ограничить backend API ролями и capabilities.

Без форка Chromium нельзя полноценно:

- надежно превратить browser `X` в системную power-кнопку для всех вкладок, страниц и crash-paths;
- встроить постоянную системную панель в сам browser chrome;
- заменить все native dialogs для `<input type="file">`, downloads и chooser flows;
- гарантированно контролировать все download/open/save paths на уровне browser UI;
- сделать ChromeOS-like tray/status area как часть toolbar.

С patchset Chromium нужно проектировать:

- custom top system area;
- close button / window control patch: confirmation UI, shutdown handoff и crash-safe behavior внутри browser chrome;
- SuvOS settings как privileged internal page;
- policy-aware file picker для `<input type="file">`;
- download target enforcement;
- native dialog replacement или XDG portal backend integration;
- deep permission UI для filesystem/device/network/system APIs.

### Admin Explorer Code (AEC)

`admin-explorer-code` (AEC) - это отдельная явно включаемая admin/debug web-вкладка внутри Chromium. В текущем направлении AEC является root-capable guest filesystem explorer: файловое дерево, вкладки, просмотр текстов/логов/картинок и встроенный terminal работают по гостевой SuvOS filesystem, включая root-level доступ внутри VM. Host filesystem этот профиль не затрагивает.

Это осознанное исключение из будущей policy-aware пользовательской file API модели. Риск ограничивается opt-in профилем `SUVOS_WITH_AEC=1`, запуском только внутри guest VM и сохранением `/system/suvos` read-only boot policy. Если понадобится запись в `/system/suvos` из AEC, это должно быть отдельным явным изменением boot policy.

MVP roadmap для AEC:

1. Держать AEC в sibling repo `../admin-explorer-code`, не клонировать исходники Code - OSS в SuvOS repo.
2. Собирать AEC artifact как Code - OSS source fork без marketplace gallery, Microsoft/cloud endpoints, telemetry defaults, settings sync, account auth defaults и chat/Copilot contributions.
3. Для ручного запуска включать AEC по умолчанию через `make run` / `make runos`; низкоуровневые `SUVOS_WITH_AEC=1`, `make run-gui-aec` и `make test-aec-smoke` оставить для совместимости и автотестов.
4. Запускать AEC server внутри guest как root на `127.0.0.1:3030` с base path `/aec`.
5. Хранить runtime state в `/data/suvos/aec`, local extensions в `/data/suvos/extensions/aec`, volatile state в `/run/suvos-aec`.
6. Проверять smoke-тестом workbench, static assets, отсутствие marketplace/cloud product metadata и работоспособность `node-pty` для terminal.
7. В default QEMU/TCG GUI-профиле держать terminal rendering software-only. WebGL для xterm можно возвращать только отдельным accelerated VM треком: `aarch64` SuvOS + `aarch64` AEC artifact для Parallels/Virtualization.framework/QEMU-HVF или рабочий virgl/venus-профиль, затем отдельный smoke для Chromium WebGL и xterm WebGL.

Policy-aware `/api/files/*` поверх `suvos-gateway -> suvosd` остается отдельным будущим пользовательским file picker / file manager треком и не заменяет AEC debug profile.

### Стратегия форков Chromium и Code - OSS

В текущий репозиторий SuvOS не нужно клонировать исходники Chromium, VS Code/Code - OSS или OpenVSCode Server. Эти проекты слишком большие для initramfs/rootfs workspace и должны жить как отдельные upstream/vendor checkouts с отдельной build-инфраструктурой.

Chromium fork нужен только после того, как обычный Chromium под Cage, web UI, `suvos-gateway`, file API и XDG portal path перестанут покрывать требования. Будущий Chromium-трек должен идти так:

1. Отдельный checkout/repo для Chromium, не внутри SuvOS.
2. Сначала vanilla Chromium build без SuvOS patchset.
3. Затем один минимальный patch и отдельная patch queue.
4. Первый практичный patch target: close button/window control -> SuvOS confirmation/shutdown handoff или privileged internal page.
5. Только после успешного patch/build подключать Chromium artifact к SuvOS GUI profile.

Code - OSS fork для AEC теперь является активным треком. Он должен жить во внешнем repo `../admin-explorer-code`, быть привязан к конкретному upstream tag/sha и отдавать в SuvOS только готовый linux-x64 artifact. OpenVSCode Server можно оставить как исторический spike/fallback, но не как основной путь AEC.

### File picker

Phase A: web-based picker for SuvOS pages.

- Работает внутри settings/file-manager UI.
- Использует будущие `/api/files/*` endpoints поверх `suvosd`.
- Обычный user видит только allowlisted roots, например `/data/user/...`.
- admin/root видит расширенный набор roots только после auth/policy.
- Не перехватывает обычный `<input type="file">` на сторонних сайтах.

Phase B: XDG portal-compatible picker.

- Реализовать SuvOS file chooser backend через DBus/XDG Desktop Portal.
- Использовать стандартный FileChooser route для open/save flows там, где Chromium build это поддерживает.
- Это хороший промежуточный слой до собственного Chromium patchset.

Phase C: Chromium patch.

- Перехватить file open/save/download flows внутри Chromium.
- Все пути проверять через SuvOS policy.
- Downloads сохранять только в разрешенные директории.
- UI file picker становится частью ChromeOS-like shell.

## Четвертый этап: браузер вместо рабочего стола

После стабильного web UI запускаем графику:

- QEMU window launch через `make run-graphics` или `make run-core-graphics`;
- framebuffer smoke layer через `suvos-splash`, без Wayland/Cage/Chromium;
- минимальный Wayland stack;
- Cage как MVP compositor для одного maximized Chromium window;
- обычный Chromium с вкладками, адресной строкой и extensions UI;
- автозапуск после boot;
- watchdog, который перезапускает browser process;
- отдельный recovery TTY или serial console для отладки.

Ориентировочный запуск:

```sh
cage -- chromium --ozone-platform=wayland --user-data-dir=/data/suvos/chromium --no-first-run http://suv.os/
```

Это не финальная команда, а направление. В реальном init flow запуск идет через root-supervisor и `su-exec suvos-browser ...`; конкретные флаги Chromium зависят от дистрибутива, версии Chromium, Wayland/GPU, sandbox и profile/policy strategy. Важная граница MVP: не использовать `--kiosk`/`--app`, пока сохраняются требования к обычным вкладкам, адресной строке и extensions UI.

Локально установленный Homebrew QEMU `11.0.1` для x86_64 на Mac M сейчас поддерживает display backends `none`, `curses`, `cocoa`, `dbus` и accelerator только `tcg`. Для первого графического этапа этого достаточно: QEMU умеет открыть окно через `cocoa`. Vulkan/GPU acceleration не стоит делать условием первого UI: сначала нужен framebuffer/splash или software-rendered Wayland/Chromium, а Vulkan/virgl/venus лучше проверять отдельной итерацией и, вероятно, на Linux host или другой QEMU-сборке с GPU acceleration.

## Подводные камни

### Архитектура CPU

Mac на M-чипе - ARM64, но целевая VM может быть x86_64. Это нормальный выбор, если SuvOS должна запускаться как обычная PC-подобная виртуальная машина.

Нужно учитывать различие:

- `aarch64` guest на Apple Silicon - быстрая виртуализация;
- `x86_64` guest на Apple Silicon - эмуляция, медленнее, но пригодно для boot/console/kernel/rootfs тестов;
- если финальная цель - x86_64 VM, то x86_64 должен быть главным тестовым target, а ARM64 можно использовать только как быстрый вспомогательный прототип.

### Chromium

Chromium большой. Собирать его из исходников долго и тяжело. На первых этапах лучше использовать готовый пакет из дистрибутива. Кастомную сборку или fork Chromium стоит трогать только когда станет понятно, что обычный Chromium под Cage не покрывает требования.

Официальный Linux build path Chromium рассчитан на x86-64 machine, минимум 8GB RAM, желательно больше 16GB, и как минимум 100GB свободного места. На Mac M для целевого x86_64 Chromium fork это лучше рассматривать как отдельную Linux x86_64 build-машину или удаленный build host, а не как часть обычной SuvOS initramfs-сборки.

### VS Code / Code - OSS

VS Code как продукт и исходный `microsoft/vscode` repo - не одно и то же. AEC строится как Code - OSS source fork с собственной product identity, без marketplace gallery, Microsoft/cloud endpoints, telemetry defaults, settings sync, account auth defaults и chat/Copilot defaults. Upstream обновления нужно подтягивать через явную review/patch queue, а не как автоматическое обновление готового Microsoft/VS Code продукта.

### Графика

Wayland, GPU acceleration, input devices, DPI, touch, экранная клавиатура и power management могут занять больше времени, чем запуск самого браузера.

### Безопасность API

Главный риск - случайно сделать браузер "root-панелью". UI должен быть тонким клиентом с ограниченными командами, а не способом вызвать любой Python/Node/C++ файл.

### Обновления

Если ОС только браузер, поломанное обновление может полностью заблокировать пользователя. Нужны A/B-разделы или другой механизм отката, recovery shell и возможность безопасно восстановиться.

### Логи и диагностика

В browser-only системе пользователь не увидит терминал. Нужны boot logs, serial console, health status, crash dumps и простая диагностика до запуска UI.

### Лицензии

Linux kernel - GPL. Chromium, Node.js, Python и остальные пакеты имеют свои лицензии. Если проект будет распространяться, нужно хранить исходники/уведомления/лицензионные тексты и соблюдать условия распространения.

### "Недоступны изнутри самой ОС"

Формулировку лучше уточнить технически: системные файлы недоступны из пользовательского UI и из непривилегированных процессов. Но сама ОС, root-процессы и recovery-режим должны иметь к ним доступ, иначе систему невозможно обслуживать.

### Terminal modes

Долгосрочная модель терминала должна быть режимной, а не бинарной:

- обычный пользовательский/runtime shell работает через SuvOS-команды, роли и policy;
- admin-режим открывает расширенный набор системных действий через SuvOS API и явно разрешенные утилиты;
- developer/root mode остается explicit escape hatch в полноценный Alpine/Linux shell с package manager и полным filesystem-доступом внутри guest VM.

Это позволяет сохранить SuvOS как управляемую среду поверх Linux, а не превратить default UX в просто `Alpine + Chromium`.

## Что делать дальше

Текущий прототип уже загружается в QEMU, имеет SuvOS shell, `suvosd`, пустой app manifest registry, read-only `/system/suvos`, writable `/data/suvos`, Unix socket control API, localhost HTTP gateway, первую TypeScript web UI, structured JSON read API, AEC debug explorer, локализацию и bootstrap role auth.

Ближайший практичный план теперь такой:

1. OS settings v1: держать текущую TypeScript UI как минимальный diagnostics/control-plane dashboard без попытки превратить его в финальную систему настроек.
2. Backend/system API: ввести session token для browser UI и добавить capabilities для files, network, power, notifications.
3. AEC MVP: подключить Code - OSS based `admin-explorer-code` artifact как opt-in root-capable guest admin/debug explorer под `/aec`.
4. File API v1: `/api/files/*` через `suvos-gateway -> suvosd` для будущего policy-aware пользовательского file picker / file manager трека.
5. Chromium integration: проверить XDG portal route для file chooser и затем проектировать Chromium fork/patchset как место для OS-facing settings, power/shutdown UX и browser-chrome integration.
6. Deep integration: patched file picker, download path enforcement, native dialogs replacement и permissions policy, связанная с SuvOS roles.

Следующий реальный коммит после текущего browser-shell слоя должен оставаться маленьким и проверяемым: AEC opt-in integration и smoke, либо session token/File API. Исходники Code - OSS не должны попадать в текущий SuvOS repo.

## UI roadmap и проверки

MVP GUI boot:

- boot -> framebuffer loader -> Cage -> Chromium fullscreen window;
- Cage/Chromium работают под `suvos-browser`, а не под root;
- default GUI boot не содержит `--no-sandbox`;
- browser shell перезапускается до 3 раз за 60 секунд перед fallback;
- tabs, address bar и extensions UI видимы;
- browser window controls скрыты там, где это позволяет `cage -d` и Chromium/Wayland decoration protocol;
- нет GNOME/KDE/session manager processes;
- startup resolution проверяется автоматически по DRM modes и QEMU screendump size, live resize остается manual/hardware validation;
- GUI smoke делает QEMU screendump и не должен принимать loader или зеленый crash/fallback screen как успешный browser shell;
- serial/recovery path остается доступен.

Settings/API:

- settings tab грузится с `http://suv.os/`;
- browser-facing API возвращает structured JSON;
- unauthorized file/system actions отклоняются;
- user role видит только разрешенные roots;
- admin/root видит расширенные roots только после auth/policy.

AEC:

- AEC открывается как internal/system web app, а не как обычное расширение;
- AEC включается только opt-in профилем `SUVOS_WITH_AEC=1`;
- AEC server слушает только `127.0.0.1:3030` и доступен через gateway под `/aec/`;
- explorer и terminal работают как guest root внутри SuvOS VM;
- `/system/suvos` остается read-only;
- product metadata не содержит marketplace gallery, cloud/chat defaults или telemetry endpoints;
- terminal smoke проверяет `node-pty` и glibc dependency path.

File picker:

- web picker не может выйти за allowlisted roots;
- `..`, symlinks, hidden/system paths и download targets проходят policy check;
- native Chromium dialogs остаются известным ограничением до portal/patch phase.

## Локальные QEMU-скрипты

В репозитории есть два стартовых скрипта:

```sh
scripts/qemu-smoke-test.sh
```

Проверяет, что установленный `/opt/homebrew/bin/qemu-system-x86_64` запускается как arm64-native бинарник и может создать x86_64 VM через TCG.

```sh
scripts/run-qemu.sh
```

Запускает будущий образ `build/suvos-x86_64.qcow2`. Пока образа нет, скрипт покажет команду для создания пустого qcow2-диска. Пустой диск не загрузится сам по себе; дальше нужно будет либо установить туда минимальную Linux-систему, либо собрать собственный bootable image.

Для текущего initramfs-прототипа используются:

```sh
make run
make runos
```

`make run` и `make runos` запускают текущий GUI-профиль SuvOS в окне QEMU, открывают Chromium и поднимают AEC на `/aec`. Старый headless serial console доступен отдельно через `make run-console`; ранний framebuffer smoke path остается через `make run-graphics`.

На Apple Silicon `make run` по умолчанию использует `aarch64` SuvOS через QEMU-HVF: Alpine aarch64 kernel/assets, aarch64 rootfs packages, aarch64 SuvOS binaries и arm64 AEC artifact. Старый `x86_64` QEMU/TCG runner оставлен как `make run-qemu-x86` для совместимости и регрессий.

Это ускоряет VM за счет Hypervisor.framework, но пока не является настоящим GPU/WebGL path: текущий QEMU virtio-gpu сообщает `-virgl`, Chromium уходит в Mesa software fallback, а `virtio-gpu-pci,blob=on` требует rutabaga/udmabuf. `make run-parallels` остается guard target до появления bootable arm64 disk/ISO image; Parallels CLI не дает прямой QEMU-style `-kernel/-initrd` boot для текущего initramfs-only прототипа. Детали зафиксированы в `docs/gpu-webgl-roadmap.md`.

## Использованные ориентиры

- UTM: https://mac.getutm.app/
- UTM QEMU backend: https://docs.getutm.app/settings-qemu/settings-qemu/
- UTM architecture notes: https://docs.getutm.app/settings-qemu/system/
- Buildroot: https://buildroot.org/
- Yocto Project: https://docs.yoctoproject.org/6.0/overview-manual/yp-intro.html
- Chromium Ozone/Wayland: https://chromium.googlesource.com/chromium/src/+/main/docs/ozone_overview.md
- Chromium Linux build instructions: https://chromium.googlesource.com/chromium/src/+/main/docs/linux/build_instructions.md
- Cage README: https://github.com/cage-kiosk/cage
- Cage configuration wiki: https://github.com/cage-kiosk/cage/wiki/Configuration
- Cage man page: https://man.archlinux.org/man/cage.1.en
- XDG Desktop Portal FileChooser: https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.FileChooser.html
- Chromium Embedded Framework: https://chromiumembedded.github.io/cef/
- VS Code repository: https://github.com/microsoft/vscode
- Differences between VS Code and Code - OSS: https://github.com/microsoft/vscode/wiki/Differences-between-the-repository-and-Visual-Studio-Code
- OpenVSCode Server: https://github.com/gitpod-io/openvscode-server
- VMware Fusion/Workstation FAQ: https://www.vmware.com/docs/desktop-hypervisor-faqs
