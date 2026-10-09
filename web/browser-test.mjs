// Drive headless Chrome at the page over the DevTools protocol and read the
// verdicts out of the DOM. Chrome's --dump-dom cannot be used for this: it
// does not wait for the Web Worker the prover runs in.
//
//   node web/browser-test.mjs http://localhost:8899
//
// Called by browser-test.sh, which serves docs/ first. Needs Node >= 22
// (for the built-in WebSocket) and CHROME pointing at a Chromium browser.
import { spawn } from "node:child_process";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const BASE = process.argv[2].replace(/\/$/, "");
const CHROME = process.env.CHROME;
const PORT = 9300 + Math.floor(Math.random() * 600);
const profile = mkdtempSync(join(tmpdir(), "xitip-chrome-"));
const sleep = ms => new Promise(r => setTimeout(r, ms));

const chrome = spawn(CHROME, ["--headless=new", "--no-sandbox", "--disable-gpu",
  `--remote-debugging-port=${PORT}`, `--user-data-dir=${profile}`, "about:blank"],
  { stdio: "ignore" });
const exited = new Promise(r => chrome.on("exit", r));
const cleanup = async () => {
  chrome.kill();
  await exited;                         // Chrome writes to its profile until it exits
  try { rmSync(profile, { recursive: true, force: true }); } catch {}
};

let page;
for (let i = 0; i < 100 && !page; i++) {
  await sleep(100);
  try {
    const list = await (await fetch(`http://127.0.0.1:${PORT}/json`)).json();
    page = list.find(t => t.type === "page");
  } catch {}
}
if (!page) { console.error("could not reach Chrome"); await cleanup(); process.exit(1); }

const ws = new WebSocket(page.webSocketDebuggerUrl);
await new Promise(r => ws.onopen = r);
let seq = 0;
const pending = new Map();
ws.onmessage = ({ data }) => {
  const m = JSON.parse(data);
  if (m.id && pending.has(m.id)) { pending.get(m.id)(m.result); pending.delete(m.id); }
  if (m.method === "Runtime.exceptionThrown")
    console.log("page exception:", m.params.exceptionDetails.exception?.description);
};
const send = (method, params = {}) => new Promise(r => {
  const id = ++seq; pending.set(id, r); ws.send(JSON.stringify({ id, method, params }));
});
const js = async expr =>
  (await send("Runtime.evaluate", { expression: expr, returnByValue: true })).result.value;
await send("Runtime.enable");

// wait until cond (a JS expression) is truthy in the page
async function until(cond, ms = 20000) {
  for (const t0 = Date.now(); Date.now() - t0 < ms; await sleep(100))
    if (await js(cond)) return true;
  return false;
}

async function load(hash) {
  await send("Page.navigate", { url: "about:blank" });
  await send("Page.navigate", { url: BASE + "/#" + encodeURIComponent(hash) });
  await until(`document.body && document.body.dataset.ready === "1"`);
}

const verdict = `(document.querySelector("#out .verdict")?.textContent.match(/is (TRUE|FALSE)/) || [])[1]`;
let fail = 0;
const report = (ok, label, detail) => {
  console.log((ok ? "ok    " : "FAIL  ") + label + "  " + detail);
  if (!ok) fail = 1;
};

async function check(want, wantProof, expr) {
  await load(expr);
  await until(`${verdict} || document.querySelector("#out .err")`);
  const got = (await js(verdict)) || "NONE";
  const proof = (await js(`document.querySelector("#out pre")?.textContent.includes("Proof of  E >= 0")`)) ? 1 : 0;
  report(got === want && proof === wantProof, expr.replace(/\n/g, "/"),
         got === want && proof === wantProof ? want : `(verdict ${got}, proof ${proof})`);
}

console.log("checking " + BASE);
await check("TRUE", 1, "H(X,Y,Z) <= H(X,Y) + H(Z)");
await check("FALSE", 0, "I(X;Y|Z) <= I(X;Y)");
await check("TRUE", 1, "I(W;Z) <= I(X;Y)\nW/X/Y/Z");
await check("TRUE", 1, "2 H(X,Y,Z) <= H(X,Y) + H(Y,Z) + H(X,Z)");
await check("TRUE", 1, "H(X) <= H(Y)\nX:Y");
await check("NONE", 0, "H(X) <<< H(Y)");   // a syntax error shows a message, not a verdict

// Not provable: the page suggests conditions, and proving with one of them
// comes back TRUE, with the assumption added to the constraints.
{
  await load("I(X;Y|Z) <= I(X;Y)");
  await until(`document.querySelectorAll("#conds .cond").length > 0`);
  const shown = await js(`[...document.querySelectorAll("#conds .cond code")].map(c => c.textContent)`);
  const want = ["I(X;Y|Z) = 0", "I(X;Z|Y) = 0", "I(Y;Z|X) = 0"];
  report(JSON.stringify(shown) === JSON.stringify(want), "conditions for I(X;Y|Z) <= I(X;Y)",
         JSON.stringify(shown));
  await js(`document.querySelectorAll("#conds .cond button")[1].click()`);
  const proved = await until(`${verdict} === "TRUE"`, 10000);
  const text = await js(`document.getElementById("expr").value`);
  report(proved && text === "I(X;Y|Z) <= I(X;Y)\nI(X;Z|Y) = 0", "prove with a suggested condition",
         `(verdict ${await js(verdict)}, input ${JSON.stringify(text)})`);
}

// Cancel: start a problem too big to finish quickly, cancel it, and check
// that the restarted prover still answers.
{
  // subadditivity over 11 variables: about 20 s natively, longer in wasm
  const big = "H(A,B,C,D,E,F,G,J,K,L,M) <= " + [..."ABCDEFGJKLM"].map(v => `H(${v})`).join(" + ");
  await load("");
  await js(`document.getElementById("expr").value = ${JSON.stringify(big)}; document.getElementById("go").click()`);
  await sleep(1500);
  const running = await js(`!document.getElementById("cancel").hidden`);
  await js(`document.getElementById("cancel").click()`);
  const cancelled = await js(`document.getElementById("out").textContent.includes("cancelled")`);
  const restarted = await until(`!document.getElementById("go").disabled`, 10000);
  await js(`document.getElementById("expr").value = "H(X) <= H(X,Y)"; document.getElementById("go").click()`);
  const after = await until(`${verdict} === "TRUE"`, 10000);
  report(running && cancelled && restarted && after, "cancel a long run, then prove again",
         `(running ${running}, cancelled ${cancelled}, restarted ${restarted}, proves after ${after})`);
}

// Foundations and References are on the page, and a citation opens the
// collapsed references at the right entry.
{
  await load("");
  const ok = await js(`!!document.getElementById("foundations") && !!document.getElementById("references")
                       && document.querySelectorAll("ol.refs li").length === 9`);
  await js(`document.getElementById("foundations").open = true;
            document.querySelector('#foundations a.cite[href="#ref-3"]').click()`);
  await sleep(300);
  const opened = await js(`document.getElementById("references").open && location.hash === "#ref-3"`);
  report(ok && opened, "foundations, references, and citations open them",
         `(sections ${ok}, citation opens references ${opened})`);
}

// Help and About are on the page, with the credits.
{
  const ok = await js(`!!document.getElementById("help") && !!document.getElementById("about")
                       && document.querySelector("footer").textContent.includes("Yeung")`);
  report(ok, "help, about and credits present", ok ? "" : "missing");
}

console.log(fail ? "\nsomething failed" : "\nall good in the browser");
ws.close();
await cleanup();
process.exit(fail);
