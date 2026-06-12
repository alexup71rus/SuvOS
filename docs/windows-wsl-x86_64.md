# Windows/WSL x86_64 host flow

Этот документ фиксирует текущий целевой host-flow для SuvOS x86_64. Он нужен
именно для того, чтобы после compaction не восстанавливать процесс по памяти.

## Карта текущего хоста

- Windows host: `MyPC@192.168.1.62`.
- WSL distro: `Ubuntu`.
- WSL user: `mypc`.
- Основной рабочий checkout для сборки и QEMU: `/home/mypc/Projects/SuvOS`.
- Windows-visible зона логов/артефактов: `C:\Projects\SuvOS`.
- Chromium full source checkout: `/home/mypc/Projects/SuvOS/third_party/SuvOS_Chromium`.
- Lockfile path для Chromium: `third_party/chromium`.
- На Windows/WSL host `third_party/chromium` и `third_party/src` должны быть
  симлинками на `third_party/SuvOS_Chromium`, а не вторыми независимыми
  checkout'ами.

Проверка:

```sh
cd /home/mypc/Projects/SuvOS
ls -la third_party
git status --short
git -C third_party/SuvOS_Chromium status --short
```

## Главное правило Windows

Не запускайте долгие Chromium/QEMU/`make run` jobs напрямую из SSH-сессии или
интерактивного Windows shell. Windows может завершить дочерние процессы при
закрытии сеанса/разлогине, даже если WSL-команда ещё работала.

Долгие jobs запускаются через Windows Task Scheduler. Прогресс должен писаться
в Windows-visible log path, чтобы его можно было смотреть независимо от SSH:

```text
C:\Projects\SuvOS\build\ninja-chrome-shell-controls.log
C:\Projects\SuvOS\build\windows-qemu-serial.log
C:\Projects\SuvOS\build\windows-qemu-stderr.log
```

Текущая scheduled Chromium task:

```text
SuvOS Chromium Build
```

Tracked helper для повторного запуска Chromium source build:

```sh
cd /home/mypc/Projects/SuvOS
scripts/schedule-windows-wsl-chromium-build.sh
```

Этот helper рассчитан на WSL-сессию, где доступен `powershell.exe` interop. Если
WSL запущен из удалённого Windows SSH-сеанса и `powershell.exe` внутри WSL не
доступен, создавайте/запускайте scheduled task с Windows side через `schtasks`;
сам build-driver всё равно должен вызывать `scripts/build-chromium-source.sh`.

Текущая QEMU task из `scripts/run-suvos-windows-qemu.sh`:

```text
SuvOSWindowsQemuRun
```

## Ресурсы WSL

Текущий host имеет 20 logical CPUs и 32 GB RAM. WSL настроен на 20 CPUs, 28 GB
RAM и 32 GB swap:

```ini
[wsl2]
processors=20
memory=28GB
swap=32GB
localhostForwarding=true
guiApplications=true
nestedVirtualization=true
```

Проверка внутри WSL:

```sh
nproc
free -h
```

На Windows host сон от сети должен быть отключён на время длинных сборок и QEMU
прогонов. Иначе scheduled task может пропасть после idle timeout:

```bat
powercfg /change standby-timeout-ac 0
```

## Chromium source build

Правильный путь сборки source Chromium из SuvOS repo:

```sh
cd /home/mypc/Projects/SuvOS
SUVOS_ARCH=x86_64 \
SUVOS_CHROMIUM_SOURCE_DIR=/home/mypc/Projects/SuvOS/third_party/chromium \
SUVOS_CHROMIUM_OUT_DIR=out/Linux_x64 \
SUVOS_CHROMIUM_TARGET=chrome \
SUVOS_CHROMIUM_JOBS=20 \
scripts/build-chromium-source.sh
```

После компиляции нужно перепаковать rootfs artifact, иначе QEMU увидит старый
Chromium:

```sh
cd /home/mypc/Projects/SuvOS
SUVOS_ARCH=x86_64 \
SUVOS_CHROMIUM_SOURCE_DIR=/home/mypc/Projects/SuvOS/third_party/chromium \
SUVOS_CHROMIUM_OUT_DIR=out/Linux_x64 \
SUVOS_CHROMIUM_JOBS=20 \
scripts/package-chromium-source-artifact.sh
```

`scripts/build-chromium-source.sh` обязан использовать Chromium tooling
(`siso`/`autoninja`, если доступно) и временно прятать корневой
`/home/mypc/Projects/SuvOS/node_modules`. Прямой запуск:

```sh
ninja -C out/Linux_x64 -j20 chrome
```

не является штатным SuvOS workflow. Он уже приводил к большому dirty graph и к
TypeScript-конфликту, где Chromium webui tools подхватывали
`/home/mypc/Projects/SuvOS/node_modules/@types/estree` вместо своих типов.

Сигнатура ошибки:

```text
../../ui/webui/resources/tools/eslint/query_utils.ts
Type ... third_party/SuvOS_Chromium/third_party/node/node_modules ...
is not assignable to type ... /home/mypc/Projects/SuvOS/node_modules ...
```

Решение: запускать через `scripts/build-chromium-source.sh` или wrapper, который
его вызывает. Не чинить это правками TypeScript в Chromium.

## Browser shell window controls

Chromium в SuvOS GUI является оболочкой ОС, а не обычным приложением. Поэтому
штатный GUI launch должен передавать:

```text
--suvos-shell-hide-window-controls
```

Ожидаемое поведение vendor Chromium fork при этом флаге:

- не показывать стандартные minimize/maximize/close buttons;
- отключить resize/minimize/maximize/fullscreen controls;
- не возвращать `HTCAPTION` для browser top area, чтобы drag/double-click по
  заголовку/табам не менял размер окна.

Main repo отвечает за передачу флага в
`os/rootfs/system/suvos/bin/suvos-start-gui`. Chromium fork отвечает за
реализацию поведения в `chrome/browser/ui/views/frame/*`.

## QEMU запуск

В WSL `make run` выбирает backend `windows-qemu` для x86_64 и вызывает
`scripts/run-suvos-windows-qemu.sh`. Скрипт копирует kernel/initramfs в
`C:\Projects\SuvOS\run`, генерирует PowerShell launcher в
`C:\Projects\SuvOS\artifacts`, затем запускает Windows QEMU через PowerShell
interop или Windows SSH scheduled task fallback.

Основные логи:

```text
C:\Projects\SuvOS\build\windows-qemu-serial.log
C:\Projects\SuvOS\build\windows-qemu-stderr.log
```

После Chromium source build и packaging нужно пересобрать initramfs, затем
запустить QEMU:

```sh
cd /home/mypc/Projects/SuvOS
make run
```

Для browser-shell правок проверяйте serial log: там должен быть Chromium argv с
`--suvos-shell-hide-window-controls`.

## Что не удалять

Не удалять `third_party/SuvOS_Chromium/out/Linux_x64`, `.siso_*`,
`.ninja_deps`, `.ninja_log` и `dist/*.tar.gz`, если цель - сохранить
инкрементальную x86_64 сборку.

Удалять можно только явно временные логи/статусы и мусор переноса вроде
`._*` AppleDouble files. `build/` в main repo не коммитится.
