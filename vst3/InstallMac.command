#!/bin/bash

set -euo pipefail

PLUGIN_NAME="Ensoniq EPS-16 Plus.vst3"
LEGACY_PLUGIN_NAME="EPS-16 Plus Prototype.vst3"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd -P)"
SOURCE_PLUGIN="$SCRIPT_DIR/$PLUGIN_NAME"
SOURCE_FILES="$SCRIPT_DIR/EPS_files"

fail() {
  printf '\nInstallation failed: %s\n' "$1" >&2
  exit 1
}

[ -d "$SOURCE_PLUGIN" ] || fail "Keep this installer beside $PLUGIN_NAME."
[ -d "$SOURCE_FILES" ] || fail "The EPS_files folder is missing."

if [ -n "${EPS16_INSTALL_ROOT:-}" ]; then
  INSTALL_ROOT="$EPS16_INSTALL_ROOT"
  USE_SUDO=0
elif [ -d "/Library/Audio/Plug-Ins/VST3/$PLUGIN_NAME" ] ||
     [ -d "/Library/Audio/Plug-Ins/VST3/$LEGACY_PLUGIN_NAME" ]; then
  INSTALL_ROOT="/Library/Audio/Plug-Ins/VST3"
  USE_SUDO=1
else
  INSTALL_ROOT="$HOME/Library/Audio/Plug-Ins/VST3"
  USE_SUDO=0
fi

STAGE_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/eps16-install.XXXXXX")"
STAGE_PLUGIN="$STAGE_ROOT/$PLUGIN_NAME"
trap 'rm -rf "$STAGE_ROOT"' EXIT

printf 'Preparing %s...\n' "$PLUGIN_NAME"
/usr/bin/ditto --norsrc --noextattr --noqtn --noacl \
  "$SOURCE_PLUGIN" "$STAGE_PLUGIN"
/usr/bin/xattr -cr "$STAGE_PLUGIN"

# macOS 11 and newer understand both slices of a Universal 2 binary and can
# safely replace the transport signature with a local ad-hoc signature. Older
# Intel systems keep the already verified release signature.
DARWIN_MAJOR="$(/usr/bin/uname -r | /usr/bin/cut -d. -f1)"
if [ "$DARWIN_MAJOR" -ge 20 ]; then
  /usr/bin/codesign --force --deep --sign - "$STAGE_PLUGIN"
fi

BINARY="$STAGE_PLUGIN/Contents/MacOS/Ensoniq EPS-16 Plus"
ARCHS="$(/usr/bin/lipo -archs "$BINARY")"
case " $ARCHS " in
  *" arm64 "*) ;;
  *) fail "The Apple Silicon slice is missing." ;;
esac
case " $ARCHS " in
  *" x86_64 "*) ;;
  *) fail "The Intel slice is missing." ;;
esac

VERIFY_ARCH="$(/usr/bin/uname -m)"
/usr/bin/codesign --verify --deep --strict --verbose=2 \
  --arch "$VERIFY_ARCH" "$STAGE_PLUGIN"

DEST_PLUGIN="$INSTALL_ROOT/$PLUGIN_NAME"
DEST_FILES="$INSTALL_ROOT/EPS_files"
INCOMING="$INSTALL_ROOT/.Ensoniq-EPS-16-Plus.installing.$$"
BACKUP="$INSTALL_ROOT/Ensoniq EPS-16 Plus.vst3.backup-$(date +%Y%m%d-%H%M%S)"
LEGACY_PLUGIN="$INSTALL_ROOT/$LEGACY_PLUGIN_NAME"
LEGACY_BACKUP="$INSTALL_ROOT/EPS-16 Plus Prototype.vst3.backup-$(date +%Y%m%d-%H%M%S)"

install_user() {
  /bin/mkdir -p "$INSTALL_ROOT"
  /bin/rm -rf "$INCOMING"
  /usr/bin/ditto --norsrc --noextattr --noqtn --noacl \
    "$STAGE_PLUGIN" "$INCOMING"
  /usr/bin/xattr -cr "$INCOMING"
  /usr/bin/codesign --verify --deep --strict --verbose=2 \
    --arch "$VERIFY_ARCH" "$INCOMING"
  if [ -d "$DEST_PLUGIN" ]; then
    /bin/mv "$DEST_PLUGIN" "$BACKUP"
  elif [ -d "$LEGACY_PLUGIN" ]; then
    /bin/mv "$LEGACY_PLUGIN" "$LEGACY_BACKUP"
  fi
  /bin/mv "$INCOMING" "$DEST_PLUGIN"
  /bin/mkdir -p "$DEST_FILES"
  /usr/bin/ditto --norsrc --noextattr --noqtn --noacl \
    "$SOURCE_FILES" "$DEST_FILES"
}

install_system() {
  printf 'The existing system-wide installation will be updated.\n'
  printf 'macOS may ask for your administrator password.\n'
  /usr/bin/sudo /bin/mkdir -p "$INSTALL_ROOT"
  /usr/bin/sudo /bin/rm -rf "$INCOMING"
  /usr/bin/sudo /usr/bin/ditto --norsrc --noextattr --noqtn --noacl \
    "$STAGE_PLUGIN" "$INCOMING"
  /usr/bin/sudo /usr/bin/xattr -cr "$INCOMING"
  /usr/bin/codesign --verify --deep --strict --verbose=2 \
    --arch "$VERIFY_ARCH" "$INCOMING"
  if [ -d "$DEST_PLUGIN" ]; then
    /usr/bin/sudo /bin/mv "$DEST_PLUGIN" "$BACKUP"
  elif [ -d "$LEGACY_PLUGIN" ]; then
    /usr/bin/sudo /bin/mv "$LEGACY_PLUGIN" "$LEGACY_BACKUP"
  fi
  /usr/bin/sudo /bin/mv "$INCOMING" "$DEST_PLUGIN"
  /usr/bin/sudo /bin/mkdir -p "$DEST_FILES"
  /usr/bin/sudo /usr/bin/ditto --norsrc --noextattr --noqtn --noacl \
    "$SOURCE_FILES" "$DEST_FILES"
}

if [ "$USE_SUDO" -eq 1 ]; then
  install_system
else
  install_user
fi

/usr/bin/codesign --verify --deep --strict --verbose=2 \
  --arch "$VERIFY_ARCH" "$DEST_PLUGIN"

printf '\nInstalled successfully:\n  %s\n' "$DEST_PLUGIN"
if [ -d "$BACKUP" ]; then
  printf 'Previous version preserved as:\n  %s\n' "$BACKUP"
elif [ -d "$LEGACY_BACKUP" ]; then
  printf 'Previous version preserved as:\n  %s\n' "$LEGACY_BACKUP"
fi
printf '\nAdd your legally obtained ROM, KPC and OS files to:\n  %s\n' "$DEST_FILES"
printf '\nYou can now start the DAW and scan VST3 plug-ins.\n'
