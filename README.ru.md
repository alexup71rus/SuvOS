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
- app registry в `/system/suvos/apps/registry.tsv`;
- read-only системная зона `/system/suvos` после boot;
- базовая локализация `ru` и `en`;
- статически собранное x86_64 C++ demo-приложение;
- runtime Python 3;
- runtime Node.js.

## Сборка

```sh
make
```

Сборка скачивает x86_64 kernel и static BusyBox из Alpine `v3.22`, устанавливает Python/Node runtime-зависимости в initramfs rootfs, а также собирает C++ demo и `suvosd` через Docker/OrbStack.

Результаты:

```text
build/kernel/vmlinuz-x86_64
build/initramfs/suvos-initramfs.cpio.gz
```

## Проверка

```sh
make test
```

Команда загружает SuvOS в QEMU с `suvos.autotest=1`, проверяет базовые команды, роли, read-only защиту `/system/suvos`, запускает shell/C++/Python/Node apps и выключает VM.

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
  +-- /system/suvos/bin/suvos-shell
        +-- suvos CLI
              +-- /run/suvosd/request
                    +-- suvosd проверяет и выполняет команды
```

`suvosd` не блокирует основной daemon loop: каждый request обрабатывается worker-процессом. Количество workers ограничено, app execution сейчас имеет timeout 30 секунд, timed-out apps убиваются по process group, output ограничен по размеру.

Файлы SuvOS лежат здесь:

```text
/system/suvos/
  bin/
  apps/
  config/
  lib/
  security/
  src/
```

`/opt/suvos` является compatibility symlink на `/system/suvos`.

## Роли и Bootstrap-Secret

Текущий boot запускает SuvOS в runtime-роли `setup`. Эта роль может читать статус, смотреть список app registry, выполнять demo-приложения и делать попытку `auth root`. Полная runtime-роль `root` разблокируется командой:

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

SuvOS сейчас использует Alpine `v3.22` как upstream-источник kernel, BusyBox, Python, Node.js, musl и runtime-библиотек. Собственный слой проекта: `/init`, `/system/suvos`, `suvosd`, app registry, локализация и будущая UI/service-модель.

Для прототипа такая зависимость нормальна. Позже ее можно заменить на Buildroot или другой контролируемый build pipeline.
