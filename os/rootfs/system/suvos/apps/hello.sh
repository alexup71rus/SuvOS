#!/bin/sh
case "${SUVOS_LANG%%_*}" in
  en) echo "Hello from a SuvOS allowlisted shell app." ;;
  *) echo "Привет из разрешенного shell-приложения SuvOS." ;;
esac
