#!/bin/sh
# Load the published page in a real browser and read the verdict out of the DOM.
# This is the check that node cannot make: it exercises the page, the wasm
# and the browser's own WebAssembly runtime together.
#
#   sh web/browser-test.sh [port]
set -u

DIR=$(cd "$(dirname "$0")/../docs" && pwd)
PORT=${1:-8899}
CHROME=${CHROME:-}

if [ -z "$CHROME" ]; then
    for c in "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" \
             "/Applications/Chromium.app/Contents/MacOS/Chromium" \
             "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge" \
             "$(command -v chromium 2>/dev/null)" \
             "$(command -v google-chrome 2>/dev/null)"; do
        [ -n "$c" ] && [ -x "$c" ] && CHROME=$c && break
    done
fi
[ -n "$CHROME" ] || { echo "no Chrome-like browser found; set CHROME=..." >&2; exit 1; }
[ -f "$DIR/xitip.wasm" ] || { echo "no wasm build; run web/build-wasm.sh" >&2; exit 1; }

(cd "$DIR" && python3 -m http.server "$PORT" >/dev/null 2>&1) &
SERVER=$!
trap 'kill $SERVER 2>/dev/null' EXIT INT TERM
sleep 2

fail=0
check() {                       # check WANT PROOF expression...
    want=$1; wantproof=$2; expr=$3
    enc=$(python3 -c "import urllib.parse,sys; print(urllib.parse.quote(sys.argv[1]))" "$expr")
    dom=$("$CHROME" --headless --disable-gpu --no-sandbox \
          --virtual-time-budget=15000 \
          --dump-dom "http://localhost:$PORT/#$enc" 2>/dev/null)
    got=$(printf '%s' "$dom" | sed -n 's/.*The information expression is \([A-Z]*\)\..*/\1/p' | head -1)
    proof=$(printf '%s' "$dom" | grep -c "Proof of  E &gt;= 0")
    label=$(printf '%s' "$expr" | tr '\n' '/')
    if [ "${got:-NONE}" != "$want" ] || [ "$proof" != "$wantproof" ]; then
        echo "FAIL  $label  (verdict ${got:-NONE}, proof $proof)"
        fail=1
    else
        echo "ok    $label  $want"
    fi
}

check TRUE  1 "H(X,Y,Z) <= H(X,Y) + H(Z)"
check FALSE 0 "I(X;Y|Z) <= I(X;Y)"
check TRUE  1 "I(W;Z) <= I(X;Y)
W/X/Y/Z"
check TRUE  1 "2 H(X,Y,Z) <= H(X,Y) + H(Y,Z) + H(X,Z)"
check TRUE  1 "H(X) <= H(Y)
X:Y"
check NONE  0 "H(X) <<< H(Y)"           # a syntax error shows a message, not a verdict

[ $fail -eq 0 ] && echo "
all good in the browser" || echo "
something failed"
exit $fail
