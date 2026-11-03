#!/bin/bash
# nes-pi build: ./build.sh [4|3|all]
# Circle in circle-rpi4mc / circle-rpi3mc (the same copies neo-pi uses).
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
NST="$ROOT/third_party/nestopia"
TARGETS="${1:-all}"
[ "$TARGETS" = "all" ] && TARGETS="4 3"

for P in $TARGETS; do
	B="$ROOT/build/rpi$P"
	# keep the Nestopia objects between builds (they take a while): only the
	# frontend is rebuilt from scratch
	mkdir -p "$B"
	find "$B" -maxdepth 1 -type f ! -name '*.nst.o' ! -name 'libnst.a' -delete
	cp "$ROOT"/src/*.c "$ROOT"/src/*.cpp "$B"/
	cp "$ROOT"/third_party/miniz/miniz.c "$B"/
	# Nestopia core, with the line hook (patches/nestopia-linehook.py)
	mkdir -p "$B/nst"
	cp -a "$NST/core" "$NST/nes_ntsc" "$B/nst/"
	python3 "$ROOT/patches/nestopia-linehook.py" "$B/nst/core/NstPpu.cpp"
	# the copy is fresh every time: keep its timestamp only if the patched
	# file is the same as last build (otherwise NstPpu is recompiled)
	if cmp -s "$B/nst/core/NstPpu.cpp" "$B/nst/NstPpu.patched"; then
		touch -r "$B/nst/NstPpu.patched" "$B/nst/core/NstPpu.cpp"
	else
		cp -p "$B/nst/core/NstPpu.cpp" "$B/nst/NstPpu.patched"
	fi
	sed "s|@CIRCLEHOME@|$ROOT/circle-rpi${P}mc|; s|@SRC@|$ROOT/src|g; s|@MINIZ@|$ROOT/third_party/miniz|" \
		"$ROOT/src/Makefile.in" > "$B/Makefile"
	make -C "$B" -j16 libnst.a
	make -C "$B" -j16
done

# sanity check: the whole kernel (code + data + bss) must fit in KERNEL_MAX_SIZE
for P in $TARGETS; do
	ELF=$(ls "$ROOT"/build/rpi$P/*.elf)
	END=$("$ROOT"/toolchain/xpack-aarch64-none-elf-gcc-14.2.1-1.1/bin/aarch64-none-elf-nm "$ELF" | awk '$3=="__bss_end"||$3=="_end"{print $1}' | sort | tail -1)
	LIMIT=$((0x80000 + 0x1000000))
	if [ $((16#$END)) -ge $LIMIT ]; then echo "ERROR: kernel rpi$P too large (ends at 0x$END)"; exit 1; fi
	echo "rpi$P: kernel ends at 0x$END (limit $(printf 0x%x $LIMIT)) OK"
done
