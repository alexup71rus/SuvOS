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
suvos run hello
suvos status
```

Важное правило: команда `suvos run ../../anything` не должна работать. CLI должен запускать только заранее зарегистрированные сценарии из allowlist.

Пример структуры:

```text
/system/suvos/
  apps/
    manifest.d/
      hello.app
    hello.sh
  scripts/
    hello.py
    network_status.py
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
- `suvos-gateway` слушает `127.0.0.1:8080` и проксирует HTTP JSON API в `/run/suvosd/control.sock`;
- первая web UI лежит в `/system/suvos/ui` и открывается через `http://127.0.0.1:8080/`;
- `/api/status`, `/api/roles` и `/api/apps` возвращают структурированный JSON, чтобы UI не парсил локализованный CLI-текст;
- `/api/run?name=<app>` остается command endpoint с `exitCode` и `output`;
- `suvos.graphics=1` режим загружает минимальные DRM/framebuffer modules, затем `suvos-splash` проверяет `/dev/fb0` и пытается залить framebuffer сплошным цветом;
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
- есть статически собранный x86_64 C++ demo: `suvos run cpp-hello`;
- есть Python runtime и demo app: `suvos run py-hello`;
- есть Node.js runtime и demo app: `suvos run node-hello`.

Команды разработки:

```sh
make test       # быстрый core boot/control-plane test без Python/Node runtime
make test-full  # полный boot/runtime test с Python/Node
make run        # full runtime image
make run-core   # core image без Python/Node
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
suvos run hello
suvos run cpp-hello
suvos run py-hello
suvos run node-hello
python3 --version
node --version
poweroff
```

Важно: исходники не являются приложениями автоматически и не должны попадать в системный образ без необходимости. Например, C++ demo запускается как собранный allowlisted бинарник `cpp-hello`, а frontend-исходники лежат вне rootfs в `src/ui` и попадают в образ только как собранный UI dist.

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
cage -- chromium --ozone-platform=wayland --user-data-dir=/data/suvos/chromium --no-first-run http://127.0.0.1:8080/
```

Важное решение: для основного SuvOS shell не использовать Chromium `--kiosk` или `--app`. Эти режимы полезны для locked-down appliance screen, но конфликтуют с требованием сохранить вкладки, адресную строку, extensions UI и обычную браузерную механику. В SuvOS "fullscreen" означает, что Cage показывает единственное окно Chromium на весь экран без desktop environment, taskbar и стандартной оконной модели. Это не browser fullscreen mode.

`/system/suvos` остается read-only системной зоной. Chromium profile, extensions, history, downloads state и пользовательские browser-настройки должны жить в `/data/suvos/chromium`. Recovery/debug путь остается через serial console или отдельный dev boot flag, а не через полноценный desktop environment.

Текущий MVP запускает Chromium как Wayland-клиент через `--ozone-platform=wayland`. `xwayland` может присутствовать в образе как runtime-зависимость Alpine-пакета Cage, потому что Cage 0.2.0 в этой сборке пытается поднять XWayland server при старте. Это не должно становиться основным UI path; позже можно собрать Cage без Xwayland или заменить пакетную стратегию.

Root остается только supervisor'ом для подготовки `udev`, `dbus`, `seatd`, `/run/user/1000` и writable browser state. Cage и Chromium запускаются через `su-exec` под системным пользователем `suvos-browser` с UID/GID 1000 и группами `video`, `input`, `audio`. `--no-sandbox` не является default-флагом; он допускается только как явный dev escape через `suvos.allow_no_sandbox=1` или `SUVOS_CHROMIUM_ALLOW_NO_SANDBOX=1`.

Разрешение GUI-профиля задается на старте через QEMU `virtio-vga,xres=...,yres=...,edid=on`. `make run-gui` на macOS выбирает размер примерно в 90% от logical-размера основного дисплея, с верхним clamp около 2K, чтобы окно было крупным, но framebuffer не становился чрезмерно тяжелым. Это остается параметризуемым через `SUVOS_GUI_WIDTH=... SUVOS_GUI_HEIGHT=...`; `make test-gui-resolutions` проверяет 1024x768 и 1440x900. Полноценный live resize окна нужно проверять отдельно: он зависит не от web UI, а от цепочки QEMU Cocoa -> virtio-gpu -> Linux DRM -> wlroots/Cage.

Render profile задается через `suvos.render=<profile>`. Default без параметра - `hardware`, чтобы реальный device path не был искусственно ограничен software rendering. QEMU Cocoa/TCG dev path использует `qemu-tcg`: Chromium запускается с ANGLE `gl-egl` и Mesa llvmpipe, а Vulkan/VAAPI отключены. Fatal `GLDisplayEGL`, SwANGLE/Vulkan и GPU-process initialization failures не считаются допустимым шумом: они означают, что browser shell может остаться за зеленым splash-экраном.

Cursor theme, QEMU input devices и audio backend относятся к заменяемому GUI runtime layer. Они не должны становиться частью core-логики SuvOS. Текущий default: Alpine cursor package, USB HID keyboard/tablet/mouse через `qemu-xhci`, CoreAudio + virtio-sound на macOS host. Для input discovery нужен `eudev`: kernel modules создают `/dev/input/*`, а udev metadata позволяет libinput/wlroots/Cage увидеть эти устройства как usable seat devices.

### Cage как MVP compositor

Cage подходит для первого GUI-этапа, потому что это Wayland kiosk compositor для запуска одного maximized приложения. Это ровно то, что нужно, чтобы убрать GNOME/KDE, taskbar и window manager из пользовательской модели.

Ограничение Cage: он не создает ChromeOS-like shell UI сам. Он не добавит системную панель в Chromium, не заменит browser toolbar, не реализует notification tray и не перехватит file picker. Эти части должны быть реализованы в web shell/backend API, через Chromium policies/profile configuration или через будущий patchset Chromium.

### OS shell внутри Chromium

MVP-модель:

- стартовая вкладка открывает `http://127.0.0.1:8080/`;
- settings dashboard обслуживается `suvos-gateway`;
- UI общается только с `suvos-gateway`;
- `suvos-gateway` общается с `suvosd` через Unix socket `/run/suvosd/control.sock`;
- все системные действия проверяются через role/capability policy;
- notification feed, network placeholder, power placeholder, logs и apps остаются web-разделами settings UI.

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
- ограничить backend API ролями и capabilities.

Без форка Chromium нельзя полноценно:

- встроить постоянную системную панель в сам browser chrome;
- заменить все native dialogs для `<input type="file">`, downloads и chooser flows;
- гарантированно контролировать все download/open/save paths на уровне browser UI;
- сделать ChromeOS-like tray/status area как часть toolbar.

С patchset Chromium нужно проектировать:

- custom top system area;
- SuvOS settings как privileged internal page;
- policy-aware file picker для `<input type="file">`;
- download target enforcement;
- native dialog replacement или XDG portal backend integration;
- deep permission UI для filesystem/device/network/system APIs.

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
cage -- chromium --ozone-platform=wayland --user-data-dir=/data/suvos/chromium --no-first-run http://127.0.0.1:8080/
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

## Что делать дальше

Текущий прототип уже загружается в QEMU, имеет SuvOS shell, `suvosd`, app manifests, read-only `/system/suvos`, writable `/data/suvos`, Unix socket control API, localhost HTTP gateway, первую TypeScript web UI, structured JSON read API, Python/Node/C++ demos, локализацию и bootstrap role auth.

Ближайший практичный план теперь такой:

1. MVP GUI boot: проверить `make run-gui` в QEMU до состояния boot -> splash -> Cage -> обычный Chromium с открытой SuvOS settings tab.
2. OS settings v1: расширить текущую TypeScript UI до dashboard с разделами system status, roles, apps, network placeholder, power placeholder и logs.
3. Backend/system API: добавить capabilities для files, network, power, notifications и ввести session token для browser UI.
4. Custom file manager/picker: сделать web-based picker для SuvOS settings и API для allowed roots, browse, metadata, select file/directory.
5. Chromium integration: зафиксировать package/build strategy, проверить XDG portal route для file chooser и только потом проектировать patchset.
6. Deep integration: patched file picker, download path enforcement, native dialogs replacement и permissions policy, связанная с SuvOS roles.

Следующий реальный коммит после текущего GUI-boot слоя должен быть маленьким: либо довести `make run-gui` до подтвержденного визуального запуска Chromium в QEMU, либо доработать settings dashboard до формы, которую затем можно открыть в Chromium shell. Control plane уже получил manifest registry, writable data boundary, Unix socket API, localhost-only HTTP gateway и первую web UI.

## UI roadmap и проверки

MVP GUI boot:

- boot -> green splash -> Cage -> Chromium fullscreen window;
- Cage/Chromium работают под `suvos-browser`, а не под root;
- default GUI boot не содержит `--no-sandbox`;
- tabs, address bar и extensions UI видимы;
- browser window controls скрыты там, где это позволяет `cage -d` и Chromium/Wayland decoration protocol;
- нет GNOME/KDE/session manager processes;
- startup resolution проверяется автоматически, live resize остается manual/hardware validation;
- GUI smoke делает QEMU screendump и не должен принимать зеленый splash как успешный browser shell;
- serial/recovery path остается доступен.

Settings/API:

- settings tab грузится с `127.0.0.1`;
- browser-facing API возвращает structured JSON;
- unauthorized file/system actions отклоняются;
- user role видит только разрешенные roots;
- admin/root видит расширенные roots только после auth/policy.

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
make run-graphics
```

`make run` оставляет headless serial console, `make run-graphics` запускает тот же initramfs в окне QEMU через `-display cocoa` и совместимый `std` VGA. Для экспериментов с virtio-графикой можно переопределить `SUVOS_VGA=virtio`.

## Использованные ориентиры

- UTM: https://mac.getutm.app/
- UTM QEMU backend: https://docs.getutm.app/settings-qemu/settings-qemu/
- UTM architecture notes: https://docs.getutm.app/settings-qemu/system/
- Buildroot: https://buildroot.org/
- Yocto Project: https://docs.yoctoproject.org/6.0/overview-manual/yp-intro.html
- Chromium Ozone/Wayland: https://chromium.googlesource.com/chromium/src/+/main/docs/ozone_overview.md
- Cage README: https://github.com/cage-kiosk/cage
- Cage configuration wiki: https://github.com/cage-kiosk/cage/wiki/Configuration
- Cage man page: https://man.archlinux.org/man/cage.1.en
- XDG Desktop Portal FileChooser: https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.FileChooser.html
- Chromium Embedded Framework: https://chromiumembedded.github.io/cef/
- VMware Fusion/Workstation FAQ: https://www.vmware.com/docs/desktop-hypervisor-faqs
