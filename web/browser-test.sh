#!/bin/sh
# Load the published page in a real browser and read the verdict out of the DOM.
# This is the check that node alone cannot make: it exercises the page, the
# Web Worker, the wasm and the browser's own WebAssembly runtime together.
# The browser is driven by browser-test.mjs (Node >= 22); this script finds
# Chrome and serves docs/.
#
#   sh web/browser-test.sh [port]
set -u

DIR=$(cd "$(dirname "$0")/../docs" && pwd)
PORT=${1:-8899}
CHROME=${CHROME:-}
# with a URL argument the published site is checked instead of a local copy:
#   sh web/browser-test.sh https://geekymode.github.io/xitip-web/
BASE=""
case "${1:-}" in http://*|https://*) BASE=${1%/}; PORT=0;; esac

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
if [ -z "$BASE" ]; then
    [ -f "$DIR/xitip.wasm" ] || { echo "no wasm build; run web/build-wasm.sh" >&2; exit 1; }
    (cd "$DIR" && python3 -m http.server "$PORT" >/dev/null 2>&1) &
    SERVER=$!
    trap 'kill $SERVER 2>/dev/null' EXIT INT TERM
    sleep 2
    BASE="http://localhost:$PORT"
fi
CHROME="$CHROME" node "$(dirname "$0")/browser-test.mjs" "$BASE"
