#!/bin/sh
# Build Hengband (English) for the browser (Emscripten + Asyncify).
# Output goes to web/dist; deploy with web/deploy.sh.
# Objects are cached in web/obj (rebuilt when the source is newer; headers
# are not tracked: rm -rf web/obj after header changes).
set -e
cd "$(dirname "$0")/.."
OUT=web/dist OBJ=web/obj
rm -rf "$OUT" web/stage && mkdir -p "$OUT" web/stage/lib "$OBJ"

mkdir -p "$OUT/music" && cp ../quickband/web/music/new_town.ogg "$OUT/music/"

# Game files (English build: the *_j / j* files stay in, they are small)
for d in edit file help pref; do cp -R lib/$d web/stage/lib/; done
mkdir -p web/stage/lib/xtra/sound && cp lib/xtra/sound/sound.cfg web/stage/lib/xtra/sound/ 2>/dev/null || true
for d in data info save user apex bone script; do mkdir -p web/stage/lib/$d; done
find web/stage \( -name 'Makefile*' -o -name 'delete.me' -o -name '*.vim' -o -name '*.sh' \) -delete

# Sources: every .cpp of src/Makefile.am (the autotools list) except the
# other frontends, plus main-web.cpp
SRCS=$(awk '/_SOURCES *=/{f=1} /^EXTRA_hengband_SOURCES/{f=0} f' src/Makefile.am \
	| grep -o -E '[A-Za-z0-9_./-]+\.(cpp|cc)' | grep -v -E '^main-(x11|gcu|cap)\.cpp$' | sort -u)
SRCS="$SRCS main-web.cpp"

CXXFLAGS="-O2 -std=c++20 -fexceptions -DUSE_WEB -DDISABLE_NET -Isrc -isystem src/external-lib/include -w"
export CXXFLAGS OBJ
echo "$SRCS" | tr ' ' '\n' | xargs -P "$(sysctl -n hw.ncpu 2>/dev/null || nproc)" -n 1 sh -c '
	o="$OBJ/$(echo "$1" | tr / _).o"
	[ "$o" -nt "src/$1" ] || em++ $CXXFLAGS -c "src/$1" -o "$o"' _

em++ -O2 -fexceptions $OBJ/*.o -o "$OUT/hengband-core.js" \
	-sASYNCIFY -sASYNCIFY_STACK_SIZE=131072 -sSTACK_SIZE=2097152 \
	-sALLOW_MEMORY_GROWTH -sINITIAL_MEMORY=128MB \
	-sEXPORTED_FUNCTIONS=_main,_web_request_save \
	-sEXPORTED_RUNTIME_METHODS=FS,IDBFS,HEAPU8,addRunDependency,removeRunDependency \
	-sFORCE_FILESYSTEM -lidbfs.js -sENVIRONMENT=web \
	--preload-file web/stage/lib@/hengband/lib

cp web/index.html "$HOME/Games/rvip-tools/web/rvip-wm.js" web/hengband.js "$OUT/"
rm -rf web/stage
ls -la "$OUT"
