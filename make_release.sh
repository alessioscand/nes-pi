#!/bin/bash
# nes-pi release package: ./make_release.sh  (run ./build.sh all first)
# Checks the kernels, the firmware and that no user data is included, then
# writes dist/nes-pi-<version>-sd.zip with everything that goes on the card.
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

VERSION=$(sed -n 's/^#define PROJECT_VERSION[[:space:]]*"\(.*\)".*/\1/p' src/project.h)
[ -n "$VERSION" ] || { echo "ERROR: PROJECT_VERSION not found in src/project.h"; exit 1; }
echo "nes-pi $VERSION"

fail () { echo "ERROR: $*"; exit 1; }

# kernels: present and built from this version
K4=build/rpi4/kernel8-rpi4.img
K3=build/rpi3/kernel8.img
for K in "$K4" "$K3"; do
	[ -f "$K" ] || fail "$K missing: run ./build.sh all"
	grep -qa "$VERSION" "$K" || fail "$K does not contain version $VERSION: rebuild"
	[ "$K" -nt src/project.h ] || fail "$K is older than src/project.h: rebuild"
done

# the Raspberry Pi firmware (third_party/rpi-firmware)
for F in bootcode.bin start.elf start4.elf fixup.dat fixup4.dat armstub8-rpi4.bin \
	 bcm2711-rpi-4-b.dtb bcm2711-rpi-400.dtb bcm2711-rpi-cm4.dtb LICENCE.broadcom; do
	[ -f "third_party/rpi-firmware/$F" ] || fail "third_party/rpi-firmware/$F missing"
done

# configuration files in dist/sd
for F in config.txt cmdline.txt README.txt LICENSES.txt nespi/PUT_GAMES_HERE.txt; do
	[ -f "dist/sd/$F" ] || fail "dist/sd/$F missing"
done
grep -q "^nes-pi $VERSION " dist/sd/README.txt || fail "dist/sd/README.txt does not start with 'nes-pi $VERSION'"

# assemble in a clean staging folder: only the files that belong on the card
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
cp -r dist/sd/. "$STAGE/"
find third_party/rpi-firmware -type f ! -name COMMIT -exec cp {} "$STAGE/" \;
rm -f "$STAGE"/kernel*.img
cp "$K4" "$K3" "$STAGE/"
# the nespi folder ships with the placeholder only: no games, BENCH,
# logs, saves, screenshots or settings from a test card
find "$STAGE/nespi" -mindepth 1 ! -name PUT_GAMES_HERE.txt -exec rm -rf {} +

LEFT=$(find "$STAGE" -iname '*.nes' -o -iname '*.sav' -o -iname '*.st1' -o -iname 'BENCH*' \
	-o -iname '*.cfg' -o -iname 'LOG.TXT' -o -iname 'LAST.TXT' -o -iname 'PROFILE.TXT')
[ -z "$LEFT" ] || fail "user data in the package: $LEFT"

OUT="$ROOT/dist/nes-pi-$VERSION-sd.zip"
rm -f "$OUT"
(cd "$STAGE" && zip -qrX "$OUT" .)
echo "written $OUT"
unzip -l "$OUT" | tail -n +4 | head -n -2 | awk '{print "  " $4}'
