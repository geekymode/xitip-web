#!/bin/sh
# Build the prover for the browser. Produces docs/xitip.{js,wasm}, after
# which docs/ is a complete static site -- no server, no install, and nothing
# the user types ever leaves their machine. GitHub Pages serves that folder.
#
#   EMSDK=/path/to/emsdk GLPK=/path/to/glpk-5.0 sh web/build-wasm.sh
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
EMSDK=${EMSDK:?set EMSDK to the emsdk checkout}
GLPK=${GLPK:?set GLPK to the extracted glpk source}

# shellcheck disable=SC1091
. "$EMSDK/emsdk_env.sh" >/dev/null 2>&1

# 1. the parser and scanner, which are ordinary C++ once generated
make -C "$ROOT" build/parser.cxx build/scanner.cxx

# the scanner includes FlexLexer.h, which belongs to flex rather than to the
# compiler. Emscripten's sysroot has no copy, and putting the host's whole
# include directory on the path would drag in the host's libc headers, so
# take just that one file.
if [ ! -f "$ROOT/build/FlexLexer.h" ]; then
    for dir in /usr/local/include /opt/homebrew/opt/flex/include \
               /Library/Developer/CommandLineTools/usr/include /usr/include; do
        if [ -f "$dir/FlexLexer.h" ]; then
            cp "$dir/FlexLexer.h" "$ROOT/build/FlexLexer.h"
            break
        fi
    done
fi
[ -f "$ROOT/build/FlexLexer.h" ] || { echo "FlexLexer.h not found" >&2; exit 1; }

# 2. GLPK, compiled to wasm once and cached
if [ ! -f "$GLPK/src/.libs/libglpk.a" ]; then
    echo "--- building GLPK for wasm (slow, once) ---"
    ( cd "$GLPK" && emconfigure ./configure --disable-shared --enable-static \
        --host=i686-pc-linux-gnu >/dev/null && emmake make -j4 >/dev/null )
fi

# 3. the prover itself. main.cpp is left out: the browser entry point is
#    wasm_api.cpp, which takes text and returns text.
echo "--- linking xitip.js ---"
em++ -O2 -std=c++11 \
    -I"$ROOT" -I"$ROOT/build" -I"$GLPK/src" \
    "$ROOT/citip.cpp" "$ROOT/wasm_api.cpp" \
    "$ROOT/build/parser.cxx" "$ROOT/build/scanner.cxx" \
    "$GLPK/src/.libs/libglpk.a" \
    -o "$ROOT/docs/xitip.js" \
    --no-entry \
    -sMODULARIZE=1 -sEXPORT_NAME=createXitip \
    -sEXPORTED_FUNCTIONS='["_xitip_solve","_xitip_variables","_malloc","_free"]' \
    -sEXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString"]' \
    -sALLOW_MEMORY_GROWTH=1 \
    -sENVIRONMENT=web,worker,node \
    -sDISABLE_EXCEPTION_CATCHING=0 \
    -sEXIT_RUNTIME=0

ls -lh "$ROOT/docs/xitip.js" "$ROOT/docs/xitip.wasm"
echo "done"
