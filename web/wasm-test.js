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

// Sufficient conditions for statements that are not provable: the sets
// Xitip.jl's sufficient_conditions finds, in its order, each set's
// assumptions joined by " & ". Every set must also make the statement
// provable when it is added back as constraints.
const conditionCases = [
  ["I(X;Y|Z) <= I(X;Y)", ["I(X;Y|Z) = 0", "I(X;Z|Y) = 0", "I(Y;Z|X) = 0"]],
  ["I(X;Y) <= I(X;Y|Z)", ["I(X;Y) = 0", "I(X;Z) = 0", "I(Y;Z) = 0"]],
  ["H(X) <= H(Y)",       ["H(X|Y) = 0"]],
  ["H(X,Y) = H(X) + H(Y)", ["I(X;Y) = 0"]],
  ["I(X;Z) <= I(X;Y)",   ["I(X;Z|Y) = 0", "I(X;Z) = 0"]],
  ["2 I(C;D) <= I(A;B) + I(A;C,D) + 3 I(C;D|A) + I(C;D|B)",
   ["I(C;D) = 0", "I(C;A|D,B) = 0", "I(C;A|B) = 0", "I(C;A) = 0",
    "I(C;B|D,A) = 0", "I(C;B|A) = 0"]],
  ["I(X;Y,Z) <= I(X;Y)",
   ["I(X;Z|Y) = 0", "I(X;Y|Z) = 0 & I(X;Z) = 0", "I(X;Z) = 0 & I(Y;Z|X) = 0"]],
  ["H(X,Y,Z) = H(X) + H(Y) + H(Z)", []],     // needs three assumptions
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

  const conditions = mod.cwrap("xitip_conditions", "string", ["string"]);
  console.log("\n--- sufficient conditions ---");
  for (const [text, want] of conditionCases) {
    const reply = conditions(text);
    const sets = reply.split("\n").slice(1).join("\n").split("\n\n")
      .map(block => block.split("\n").filter(Boolean).map(l => l.split("\t")[0]))
      .filter(set => set.length);
    const got = sets.map(set => set.join(" & "));
    let note = "";
    if (!reply.startsWith("CONDITIONS")) note = " " + reply.replace(/\n/g, " ");
    else if (JSON.stringify(got) !== JSON.stringify(want))
      note = ` got ${JSON.stringify(got)}`;
    else {
      const weak = sets.find(set => !solve([text, ...set].join("\n"), 0).startsWith("TRUE"));
      if (weak) note = ` does not prove it: ${weak.join(" & ")}`;
    }
    if (note) bad++;
    console.log(`${note ? "FAIL" : " ok "}  ${String(got.length).padEnd(5)} ${JSON.stringify(text).slice(0,44).padEnd(46)}${note}`);
  }
  const proven = conditions("H(X,Y,Z) <= H(X,Y) + H(Z)");
  const refused = proven.startsWith("ERROR") && proven.includes("already provable");
  if (!refused) bad++;
  console.log(`${refused ? " ok " : "FAIL"}  ERROR a provable statement is refused`);

  console.log(bad ? `\n${bad} failed` : "\nall good");
  // show one proof in full, to confirm it is the real thing
  console.log("\n--- proof from wasm ---");
  console.log(solve("H(X,Y,Z) <= H(X,Y) + H(Z)", 1).split("\n").slice(1).join("\n"));
  process.exit(bad ? 1 : 0);
});
