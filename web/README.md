# oXitip on the web

A single page and one endpoint, with the C++ prover doing the work. There
is no runtime dependency beyond Python 3 (standard library only) and the
`oXitipLen` binary — no Julia, no package install, nothing for the user to
have on their machine but a browser.

## Running it

```console
$ make                       # build the prover
$ python3 web/server.py      # http://127.0.0.1:8080
```

Options: `--port`, `--host` (use `0.0.0.0` to accept outside connections),
`--binary` (where the prover is).

## The endpoint

```console
$ curl -s localhost:8080/api/prove \
    -H 'Content-Type: application/json' \
    -d '{"expression": "I(W;Z) <= I(X;Y)\nW/X/Y/Z"}'
{"ok": true, "verdict": true, "proof": "Proof of  E >= 0  ...", "variables": 4}
```

`expression` is the statement followed by any constraints, one per line.
`proof` (default true) asks for the proof text. On a bad request the reply
is `{"ok": false, "error": "..."}` with a 4xx status.

## Taking requests from strangers

The prover is run through `subprocess` with an argument list, so **no shell
is ever involved** — shell metacharacters in the input reach the parser as
ordinary text and are rejected there. The lines are passed after `--`, so a
line starting with `-` is input rather than an option.

Each request is bounded: at most 4000 bytes, 40 lines, 20 seconds. Control
characters are refused. The server binds to localhost unless told otherwise.

What is *not* handled here, and should be if this faces the open internet:

* **rate limiting** — a request costs real CPU, and the cost grows as 2^n in
  the number of random variables. Eight variables is about a tenth of a
  second; ten is impractical. Cap the variable count or put a limiter in
  front.
* **TLS** and a real server in front (nginx, Caddy); this one is the
  standard library's, which is fine for a few users and not meant to be a
  public web server.
* **process isolation** — the prover is a C++ program parsing untrusted
  text. It is small and has been fuzzed only lightly, so a container or a
  seccomp sandbox is worth having.

## The version with no server at all

`docs/` is the same tool with the prover compiled to WebAssembly, so
there is no backend: the page, `worker.js`, `xitip.js` and `xitip.wasm` are
all there is. The prover runs in a Web Worker (`worker.js`), so a long
problem leaves the page responsive, and Cancel stops it by terminating the
worker and starting a fresh one.
Nothing the user types leaves their machine, there is nothing to rate limit
or sandbox, and it can be published on GitHub Pages at no cost.

### Building it

```console
$ curl -sSLO https://ftp.gnu.org/gnu/glpk/glpk-5.0.tar.gz && tar xzf glpk-5.0.tar.gz
$ git clone https://github.com/emscripten-core/emsdk && (cd emsdk && ./emsdk install latest && ./emsdk activate latest)
$ EMSDK=$PWD/emsdk GLPK=$PWD/glpk-5.0 sh web/build-wasm.sh
```

GLPK is configured and built for wasm once and cached; the rest takes
seconds. The result is about 440 KB of wasm and 70 KB of JavaScript.

Three things the build has to get right, in case it needs changing:

* GLPK 5.0 ships a `config.sub` that predates the emscripten triple, so it
  is configured as `--host=i686-pc-linux-gnu`; `emconfigure` supplies the
  compiler regardless.
* `FlexLexer.h` belongs to flex, not to the compiler, and emscripten's
  sysroot has no copy. The script copies that one header into `build/`
  rather than putting the host's include directory on the path, which would
  drag in the host's libc headers.
* Linking uses `em++`, not `emcc`: with `emcc` the C++ standard library is
  not linked and the stream and vtable symbols come out undefined.

### Checking it

```console
$ node web/wasm-test.js
 ok   TRUE  "H(X,Y,Z) <= H(X,Y) + H(Z)"
 ok   FALSE "I(X;Y|Z) <= I(X;Y)"
 ...
all good
```

Twelve statements, covering constraints, an equality, a constant, four
variables and two malformed inputs. The verdicts agree with the native
binary and with Xitip.jl, and every true one comes back with its proof.

### Checking it in a real browser

`node web/wasm-test.js` exercises the wasm but not the page, and not the
browser's own WebAssembly runtime. For that:

```console
$ sh web/browser-test.sh
ok    H(X,Y,Z) <= H(X,Y) + H(Z)  TRUE
ok    I(X;Y|Z) <= I(X;Y)  FALSE
...
all good in the browser
```

It serves `docs/`, drives headless Chrome at it over the DevTools protocol
(`web/browser-test.mjs`, Node 22 or later), and reads the verdict back out of
the DOM. It also cancels a long run and checks that the prover answers
again afterwards. Chrome's `--dump-dom` is not enough here, because it does
not wait for the worker. Set `CHROME=` to point at another Chromium-like
browser.

By hand, which is worth doing once:

```console
$ cd docs && python3 -m http.server
```

then open <http://localhost:8000>. Type a statement, press Prove (or
Cmd/Ctrl+Enter). Opening the file directly with `file://` will *not* work —
the browser refuses to fetch the `.wasm` over that scheme.

### Permalinks

The page reads the problem from the URL fragment, so a result can be linked:

```
index.html#I(W;Z)%20%3C%3D%20I(X;Y)%0AW/X/Y/Z
```

Constraints are separate lines, encoded as `%0A`. The separator cannot be
`;`, because `;` is part of `I(X;Y)`.

### Publishing it

`docs/` is self-contained, so it can be copied to any static host.
It is served at https://www.oxitip.com from GitHub Pages; `docs/CNAME` names
that domain, and the domain's DNS points at GitHub Pages.
On GitHub Pages, note that `.wasm` must be served as `application/wasm` —
Pages does this already, as does `python3 -m http.server` for local
checking.
