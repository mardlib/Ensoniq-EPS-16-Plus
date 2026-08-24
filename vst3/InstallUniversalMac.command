#!/bin/bash

set -euo pipefail

VST3_NAME="Ensoniq EPS-16 Plus.vst3"
AU_NAME="Ensoniq EPS-16 Plus.component"
LEGACY_VST3_NAME="EPS-16 Plus Prototype.vst3"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd -P)"
SOURCE_VST3="$SCRIPT_DIR/$VST3_NAME"
SOURCE_AU="$SCRIPT_DIR/$AU_NAME"
SOURCE_FILES="$SCRIPT_DIR/EPS_files"

fail() {
  printf '\nInstallation failed: %s\n' "$1" >&2
  exit 1
}

[ -d "$SOURCE_VST3" ] || fail "Keep this installer beside $VST3_NAME."
[ -d "$SOURCE_AU" ] || fail "Keep this installer beside $AU_NAME."
[ -d "$SOURCE_FILES" ] || fail "The EPS_files folder is missing."

if [ -n "${EPS16_VST3_INSTALL_ROOT:-}" ] &&
   [ -n "${EPS16_AU_INSTALL_ROOT:-}" ]; then
  VST3_ROOT="$EPS16_VST3_INSTALL_ROOT"
  AU_ROOT="$EPS16_AU_INSTALL_ROOT"
  USE_SUDO=0
elif [ -d "/Library/Audio/Plug-Ins/VST3/$VST3_NAME" ] ||
   [ -d "/Library/Audio/Plug-Ins/VST3/$LEGACY_VST3_NAME" ] ||
   [ -d "/Library/Audio/Plug-Ins/Components/$AU_NAME" ]; then
  VST3_ROOT="/Library/Audio/Plug-Ins/VST3"
  AU_ROOT="/Library/Audio/Plug-Ins/Components"
  USE_SUDO=1
else
  VST3_ROOT="$HOME/Library/Audio/Plug-Ins/VST3"
  AU_ROOT="$HOME/Library/Audio/Plug-Ins/Components"
  USE_SUDO=0
fi

STAGE_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/eps16-install.XXXXXX")"
STAGE_VST3="$STAGE_ROOT/$VST3_NAME"
STAGE_AU="$STAGE_ROOT/$AU_NAME"
trap 'rm -rf "$STAGE_ROOT"' EXIT

prepare_bundle() {
  local source="$1"
  local stage="$2"
  /usr/bin/ditto --norsrc --noextattr --noqtn --noacl "$source" "$stage"
  /usr/bin/xattr -cr "$stage"
  local darwin_major
  darwin_major="$(/usr/bin/uname -r | /usr/bin/cut -d. -f1)"
  if [ "$darwin_major" -ge 20 ]; then
    /usr/bin/codesign --force --deep --sign - "$stage"
  fi
  local binary="$stage/Contents/MacOS/Ensoniq EPS-16 Plus"
  local archs
  archs="$(/usr/bin/lipo -archs "$binary")"
  case " $archs " in *" arm64 "*) ;; *) fail "$stage has no arm64 slice." ;; esac
  case " $archs " in *" x86_64 "*) ;; *) fail "$stage has no x86_64 slice." ;; esac
  /usr/bin/codesign --verify --deep --strict --verbose=2 \
    --arch "$(/usr/bin/uname -m)" "$stage"
}

run_privileged() {
  if [ "$USE_SUDO" -eq 1 ]; then
    /usr/bin/sudo "$@"
  else
    "$@"
  fi
}

install_bundle() {
  local stage="$1"
  local root="$2"
  local name="$3"
  local legacy_name="${4:-}"
  local incoming="$root/.$name.installing.$$"
  local destination="$root/$name"
  local stamp
  stamp="$(date +%Y%m%d-%H%M%S)"
  local backup="$root/$name.backup-$stamp"

  run_privileged /bin/mkdir -p "$root"
  run_privileged /bin/rm -rf "$incoming"
  run_privileged /usr/bin/ditto --norsrc --noextattr --noqtn --noacl \
    "$stage" "$incoming"
  run_privileged /usr/bin/xattr -cr "$incoming"
  /usr/bin/codesign --verify --deep --strict --verbose=2 \
    --arch "$(/usr/bin/uname -m)" "$incoming"
  if [ -d "$destination" ]; then
    run_privileged /bin/mv "$destination" "$backup"
  elif [ -n "$legacy_name" ] && [ -d "$root/$legacy_name" ]; then
    run_privileged /bin/mv "$root/$legacy_name" \
      "$root/$legacy_name.backup-$stamp"
  fi
  run_privileged /bin/mv "$incoming" "$destination"
  printf 'Installed: %s\n' "$destination"
}

printf 'Preparing universal VST3 and Audio Unit...\n'
prepare_bundle "$SOURCE_VST3" "$STAGE_VST3"
prepare_bundle "$SOURCE_AU" "$STAGE_AU"

if [ "$USE_SUDO" -eq 1 ]; then
  printf 'The existing system-wide installation will be updated.\n'
  printf 'macOS may ask for your administrator password.\n'
fi

install_bundle "$STAGE_VST3" "$VST3_ROOT" "$VST3_NAME" "$LEGACY_VST3_NAME"
install_bundle "$STAGE_AU" "$AU_ROOT" "$AU_NAME"

for files_root in "$VST3_ROOT/EPS_files" "$AU_ROOT/EPS_files"; do
  run_privileged /bin/mkdir -p "$files_root"
  run_privileged /usr/bin/ditto --norsrc --noextattr --noqtn --noacl \
    "$SOURCE_FILES" "$files_root"
done

/usr/bin/killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
printf '\nInstallation succeeded. Restart the DAW and rescan plug-ins.\n'
printf 'Add legally obtained ROM, KPC and OS files to either EPS_files folder.\n'
