#!/bin/sh
# vi: sw=2 ts=4

set -e

TOOLBOX_IMAGE=registry.gitlab.gnome.org/gnome/gnome-shell/toolbox
DEFAULT_NAME=gnome-shell-devel

usage() {
  cat <<-EOF
	Usage: $(basename $0) [OPTION…]

	Create a toolbox for gnome-shell development

	Options:
	  -n, --name=NAME         Create container as NAME instead of the
	                          default "$DEFAULT_NAME"
	  -v, --version=VERSION   Create container for stable version VERSION
	                          (like 44) instead of the main branch
	  -r, --replace           Replace an existing container
	  --skip-mutter           Do not build mutter
	  -h, --help              Display this help

	EOF
}

die() {
  echo "$@" >&2
  exit 1
}

toolbox_run() {
  toolbox run --container $NAME "$@"
}

TEMP=$(getopt \
  --name $(basename $0) \
  --options 'n:v:rh' \
  --longoptions 'name:' \
  --longoptions 'version:' \
  --longoptions 'replace' \
  --longoptions 'skip-mutter' \
  --longoptions 'help' \
  -- "$@")

eval set -- "$TEMP"
unset TEMP

NAME=$DEFAULT_NAME

while true; do
  case "$1" in
    -n|--name)
      NAME="$2"
      shift 2
    ;;

    -v|--version)
      VERSION="$2"
      shift 2
    ;;

    -r|--replace)
      REPLACE=1
      shift
    ;;

    --skip-mutter)
      SKIP_MUTTER=1
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

TAG=${VERSION:-main}

if podman container exists $NAME; then
  [[ $REPLACE ]] ||
    die "Container $NAME already exists and --replace was not specified"
  toolbox rm --force $NAME
fi

podman pull $TOOLBOX_IMAGE:$TAG

toolbox create --image $TOOLBOX_IMAGE:$TAG $NAME
[[ $SKIP_MUTTER ]] || toolbox_run update-mutter
