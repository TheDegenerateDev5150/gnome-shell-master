#!/bin/bash
# vi: sw=2 ts=4

set -e

DEFAULT_TOOLBOX=gnome-shell-devel
CONFIG_FILE=${XDG_CONFIG_HOME:-$HOME/.config}/gnome-shell-toolbox-tools.conf

usage() {
  cat <<-EOF
	Usage: $(basename $0) [OPTION…]

	Run gnome-shell from a toolbox

	Options:
	  -t, --toolbox=TOOLBOX   Use TOOLBOX instead of the default "$DEFAULT_TOOLBOX"

	  --classic               Run in Classic mode
	  --greeter               Run (simulated) login screen

	  -2, --multi-monitor     Run with two monitors
	  -0, --headless          Run headless

	  --rtl                   Force right-to-left layout
	  --locale=LOCALE         Use specified LOCALE

	  --debug                 Run in gdb
	  -v, --verbose           Enable debug logging

	  --unsafe-mode           Do no restrict D-Bus APIs
	  --force-animations      Force animations to be enabled

	  --version               Print version
	  -h, --help              Display this help

	EOF
}

die() {
  echo "$@" >&2
  exit 1
}

find_locale() {
  locale -a | sed -e "/^$1/!d" -e '/\.utf8$/I!d' | head -n 1
}

should_run_nested() {
  [[ "$XDG_SESSION_TYPE" != "tty" ]] && [[ ! "$HEADLESS" ]]
}

has_devkit() {
  toolbox --container $TOOLBOX run gnome-shell --help | grep -q -- --devkit
}

has_nested() {
  toolbox --container $TOOLBOX run gnome-shell --help | grep -q -- --nested
}

# load defaults
. $CONFIG_FILE
TOOLBOX=$DEFAULT_TOOLBOX

SHELL_ENV=(XDG_CURRENT_DESKTOP=GNOME)
SHELL_ARGS=()

TEMP=$(getopt \
 --name $(basename $0) \
 --options '20t:vh' \
 --longoptions 'toolbox:' \
 --longoptions 'classic' \
 --longoptions 'greeter' \
 --longoptions 'multi-monitor' \
 --longoptions 'headless' \
 --longoptions 'rtl' \
 --longoptions 'locale:' \
 --longoptions 'debug' \
 --longoptions 'verbose' \
 --longoptions 'unsafe-mode' \
 --longoptions 'force-animations' \
 --longoptions 'version' \
 --longoptions 'help' \
 -- "$@") || die "Run $(basename $0) --help to see available options"

eval set -- "$TEMP"
unset TEMP

while true; do
  case $1 in
    -t|--toolbox)
      TOOLBOX=$2
      shift 2
    ;;

    --classic)
      SHELL_ENV+=(XDG_CURRENT_DESKTOP=GNOME-Classic:GNOME)
      SHELL_ARGS+=(--mode=classic)
      shift
    ;;

    --greeter)
      SHELL_ENV+=(GDM_GREETER_TEST=1)
      SHELL_ARGS+=(--mode=gdm)
      shift
    ;;

    -2|--multi-monitor)
      SHELL_ENV+=(MUTTER_DEBUG_NUM_DUMMY_MONITORS=2)
      shift
    ;;

    -0|--headless)
      HEADLESS=1
      SHELL_ARGS+=(--headless)
      shift
    ;;

    --rtl)
      SHELL_ENV+=(CLUTTER_TEXT_DIRECTION=rtl)
      shift
    ;;

    --locale)
      SHELL_ENV+=(LANG=$(find_locale $2))
      shift 2
    ;;

    --debug)
      GDB="gdb --args"
      shift
    ;;

    -v|--verbose)
      SHELL_ENV+=(
        G_MESSAGES_DEBUG="GNOME Shell"
        SHELL_DEBUG=backtrace-warnings
      )
      shift
    ;;

    --unsafe-mode|--force-animations|--version)
      SHELL_ARGS+=($1)
      shift
    ;;

    -h|--help)
      usage
      exit 0
    ;;

    --)
      shift
      break
    ;;
  esac
done

FORCE_DISPLAY=1
if should_run_nested; then
  if has_devkit; then
    SHELL_ARGS+=( --devkit )
    FORCE_DISPLAY=0
  elif has_nested; then
    SHELL_ARGS+=( --nested )
  else
    die Mutter has to be built with devkit or x11 support
  fi
else
  SHELL_ARGS+=( --wayland )
fi

if [ $FORCE_DISPLAY -gt 0 ]; then
  display=toolbox-wayland-$RANDOM
  SHELL_ENV+=(WAYLAND_DISPLAY=$display)
  SHELL_ARGS+=(--wayland-display=$display)
fi

toolbox --container $TOOLBOX run \
  env "${SHELL_ENV[@]}" dbus-run-session $GDB gnome-shell "${SHELL_ARGS[@]}"
