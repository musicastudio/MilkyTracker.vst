#!/usr/bin/env bash
# Builds MilkyTracker.vst's installers with the musica.studio installer engine
# (github.com/musicastudio/Installer): a copy of the engine with this folder as its product/, run through
# the engine's own build.sh, its dist/ copied to installer/dist/. $INSTALLER names the engine's checkout
# (default ../Installer beside this repo, cloned there when missing). $BIN_WINDOWS defaults to this repo's
# bin/; $BIN_MACOS, $BIN_LINUX and $VERSION pass through as the engine's build.sh takes them.
#   installer/build.sh
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
engine=${INSTALLER:-$root/../Installer}
[ -d "$engine" ] || git clone --depth 1 https://github.com/musicastudio/Installer "$engine"
engine=$(cd "$engine" && pwd)

work="$root/build/installer"
rm -rf "$work" && mkdir -p "$work"
# The engine without its own product, dist or cache; the font comes along when the checkout has it. Its tests
# fake FSVR's payload, so they run in the engine's own CI, not against this product.
(cd "$engine" && tar cf - --exclude=./dist --exclude=./.cache --exclude=./.git --exclude=./product --exclude=./main_test.go .) | (cd "$work" && tar xf -)
mkdir -p "$work/product"
cp "$here"/installer.xml "$here"/LICENSE "$here"/icon.png "$here"/map.png "$work/product/"

export BIN_WINDOWS=${BIN_WINDOWS:-$root/bin}
(cd "$work" && ./build.sh)
rm -rf "$here/dist" && cp -r "$work/dist" "$here/dist"
ls -l "$here/dist"
