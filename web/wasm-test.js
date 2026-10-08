// Verify the wasm build actually proves things, outside any browser.
const createXitip = require('../docs/xitip.js');

const cases = [
  ["H(X,Y,Z) <= H(X,Y) + H(Z)",                     "TRUE",  3],
  ["I(X;Y|Z) <= I(X;Y)",                            "FALSE", 3],
  ["I(W;Z) <= I(X;Y)\nW/X/Y/Z",                     "TRUE",  4],
  ["2 H(X,Y,Z) <= H(X,Y) + H(Y,Z) + H(X,Z)",        "TRUE",  3],
  ["H(X) <= H(Y)\nX:Y",                             "TRUE",  2],
  ["H(X) <= H(Y)",                                  "FALSE", 2],
  ["I(X;Y;Z) >= 0",                                 "FALSE", 3],
  ["H(X) >= 1\nH(X) >= 2",                          "TRUE",  1],
  ["I(X;Y) = H(X) + H(Y) - H(X,Y)",                 "TRUE",  2],
  ["H(X1,X2,X3,X4) <= H(X1)+H(X2)+H(X3)+H(X4)",     "TRUE",  4],
  ["H(X) <<< H(Y)",                                 "ERROR", -1],
  ["",                                              "ERROR", -1],
];

createXitip().then(mod => {
  const solve = mod.cwrap("xitip_solve", "string", ["string", "number"]);
  const nvars = mod.cwrap("xitip_variables", "number", ["string"]);
  let bad = 0;
  for (const [text, want, wantVars] of cases) {
    const reply = solve(text, 1);
    const head = reply.split("\n")[0];
    const body = reply.slice(head.length + 1);
    const got = nvars(text);
    let note = "";
    if (head !== want) { note = ` EXPECTED ${want}`; bad++; }
    else if (want === "TRUE" && !body.includes("Proof of")) { note = " NO PROOF"; bad++; }
    else if (wantVars >= 0 && got !== wantVars) { note = ` vars ${got} != ${wantVars}`; bad++; }
    console.log(`${note ? "FAIL" : " ok "}  ${head.padEnd(5)} ${JSON.stringify(text).slice(0,44).padEnd(46)}${note}`);
  }
  console.log(bad ? `\n${bad} failed` : "\nall good");
  // show one proof in full, to confirm it is the real thing
  console.log("\n--- proof from wasm ---");
  console.log(solve("H(X,Y,Z) <= H(X,Y) + H(Z)", 1).split("\n").slice(1).join("\n"));
  process.exit(bad ? 1 : 0);
});
