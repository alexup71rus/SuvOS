#!/bin/sh

SUVOS_LANG="${SUVOS_LANG:-ru}"
SUVOS_LANG="${SUVOS_LANG%%_*}"

suvos_t() {
  key="$1"
  case "$SUVOS_LANG:$key" in
    ru:motd.title) echo "SuvOS developer preview" ;;
    ru:motd.kernel) echo "Ядро: Linux x86_64" ;;
    ru:motd.userspace) echo "Userspace: initramfs + BusyBox + SuvOS shell" ;;
    ru:motd.help) echo "Введите \"help\", чтобы увидеть доступные команды." ;;
    ru:shell.ready) echo "Консоль SuvOS готова." ;;
    ru:shell.exit) echo "SuvOS shell работает как консоль PID 1. Используйте poweroff для остановки VM." ;;
    ru:shell.not_found) printf '%s: команда не найдена\n' "$2" ;;
    ru:shell.help) cat <<'EOF'
Встроенные команды:
  help                 Показать эту справку
  cd [dir]             Перейти в директорию
  roles                Показать текущую роль и права
  whoami               Показать runtime-роль SuvOS
  auth                 Управление bootstrap-auth
  suvos <command>      Выполнить управляющую команду SuvOS
  run <name> [args]    Запустить разрешенное приложение через suvosd
  exit                 Вернуться в shell loop
  poweroff             Выключить VM

Доступные BusyBox-команды:
  ls mkdir rmdir pwd cat echo touch rm cp mv chmod grep sed awk head tail find

Опциональные runtime в full build:
  python3 node
EOF
      ;;
    ru:cli.usage) cat <<'EOF'
Использование:
  suvos help
  suvos status
  suvos roles
  suvos whoami
  suvos auth status
  suvos auth root <bootstrap-secret>
  suvos list
  suvos run <name> [args...]

Примеры:
  suvos run hello
  suvos run cpp-hello
  suvos run py-hello
  suvos run node-hello
EOF
      ;;
    ru:cli.daemon_down) echo "suvos: suvosd не запущен" ;;
    ru:cli.bad_chars) echo "suvos: аргументы запроса не могут содержать управляющие символы" ;;
    ru:cli.malformed) echo "suvos: некорректный ответ от suvosd" ;;
    ru:cli.unknown) printf 'suvos: неизвестная команда: %s\n' "$2" ;;
    en:motd.title|*:motd.title) echo "SuvOS developer preview" ;;
    en:motd.kernel|*:motd.kernel) echo "Kernel: Linux x86_64" ;;
    en:motd.userspace|*:motd.userspace) echo "Userspace: initramfs + BusyBox + SuvOS shell" ;;
    en:motd.help|*:motd.help) echo "Type \"help\" for available SuvOS commands." ;;
    en:shell.ready|*:shell.ready) echo "SuvOS console is ready." ;;
    en:shell.exit|*:shell.exit) echo "SuvOS shell is PID 1's console. Use poweroff to stop the VM." ;;
    en:shell.not_found|*:shell.not_found) printf '%s: command not found\n' "$2" ;;
    en:shell.help|*:shell.help) cat <<'EOF'
Built-ins:
  help                 Show this help
  cd [dir]             Change directory
  roles                Show current role and permissions
  whoami               Show the SuvOS runtime role
  auth                 Manage bootstrap auth
  suvos <command>      Run SuvOS control command
  run <name> [args]    Run an allowed SuvOS app through suvosd
  exit                 Return to the shell loop
  poweroff             Shut down the VM

Common BusyBox commands available now:
  ls mkdir rmdir pwd cat echo touch rm cp mv chmod grep sed awk head tail find

Optional runtimes in full build:
  python3 node
EOF
      ;;
    en:cli.usage|*:cli.usage) cat <<'EOF'
Usage:
  suvos help
  suvos status
  suvos roles
  suvos whoami
  suvos auth status
  suvos auth root <bootstrap-secret>
  suvos list
  suvos run <name> [args...]

Examples:
  suvos run hello
  suvos run cpp-hello
  suvos run py-hello
  suvos run node-hello
EOF
      ;;
    en:cli.daemon_down|*:cli.daemon_down) echo "suvos: suvosd is not running" ;;
    en:cli.bad_chars|*:cli.bad_chars) echo "suvos: request arguments cannot contain control characters" ;;
    en:cli.malformed|*:cli.malformed) echo "suvos: malformed response from suvosd" ;;
    en:cli.unknown|*:cli.unknown) printf 'suvos: unknown command: %s\n' "$2" ;;
    *) echo "$key" ;;
  esac
}

suvos_load_locale() {
  if [ -r /system/suvos/config/locale.conf ]; then
    . /system/suvos/config/locale.conf
    export SUVOS_LANG="${SUVOS_LANG:-ru}"
  fi
}
