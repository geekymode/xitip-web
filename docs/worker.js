// Runs the prover off the page's main thread, so a slow problem leaves the
// page responsive and can be cancelled by terminating this worker.
//
//   page -> worker   {id, text, proof, conditions}
//   worker -> page   {type: "ready"} once, then for each request
//                    {type: "vars",       id, nvars}   before solving
//                    {type: "result",     id, reply, nvars}
//                    {type: "conditions", id, reply}   after a FALSE, if asked
//                    {type: "error",      id, message}
importScripts("xitip.js");

let solve = null, countVars = null, findConditions = null;
const ready = createXitip().then(mod => {
  solve = mod.cwrap("xitip_solve", "string", ["string", "number"]);
  countVars = mod.cwrap("xitip_variables", "number", ["string"]);
  findConditions = mod.cwrap("xitip_conditions", "string", ["string"]);
  postMessage({ type: "ready" });
}).catch(e => {
  postMessage({ type: "error", id: null, message: "could not load the prover: " + e });
});

onmessage = async ({ data: { id, text, proof, conditions } }) => {
  await ready;
  try {
    const nvars = countVars(text);
    postMessage({ type: "vars", id, nvars });
    const reply = solve(text, proof ? 1 : 0);
    postMessage({ type: "result", id, reply, nvars });
    // the verdict goes out first; the search can take longer
    if (conditions && reply.startsWith("FALSE"))
      postMessage({ type: "conditions", id, reply: findConditions(text) });
  } catch (e) {
    postMessage({ type: "error", id, message: "the prover failed: " + e });
  }
};
