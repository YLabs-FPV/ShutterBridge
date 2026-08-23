#!/usr/bin/env bash
# Build the ShutterBridge firmware + LittleFS image and copy the flashable
# parts into the website's public/firmware/ folder (served by esp-web-tools).
#
# Usage:  ./pack-web-firmware.sh [--fs-only]
#   --fs-only   Rebuild only the LittleFS image (use when just the web UI in
#               data/ changed and src/ is untouched -- much faster).
set -euo pipefail

ENV=esp32s3-zero
FW_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$FW_DIR/.pio/build/$ENV"
DEST="$FW_DIR/../site/public/firmware"
BOOT_APP0="$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"

cd "$FW_DIR"

if [[ "${1:-}" == "--fs-only" ]]; then
  echo ">> Building LittleFS image only"
  pio run -e "$ENV" -t buildfs
else
  echo ">> Building firmware + LittleFS image"
  pio run -e "$ENV"
  pio run -e "$ENV" -t buildfs
fi

mkdir -p "$DEST"
cp "$BUILD/bootloader.bin"  "$DEST/bootloader.bin"
cp "$BUILD/partitions.bin"  "$DEST/partitions.bin"
cp "$BOOT_APP0"             "$DEST/boot_app0.bin"
cp "$BUILD/firmware.bin"    "$DEST/firmware.bin"
cp "$BUILD/littlefs.bin"    "$DEST/littlefs.bin"

echo ">> Copied to $DEST:"
ls -l "$DEST"/*.bin
echo ">> Done. manifest.dev.json offsets are unchanged; reload the flash page to serve the new binaries."
