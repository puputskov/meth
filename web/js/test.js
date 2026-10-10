// Runs every expression of tests/cases.txt in the wasm module and puts the
// outcome in the page as JSON: [{ input, code, text }]. tools/test_web.py
// starts a browser on this page, runs the same expressions with the native
// program and compares. The page itself decides nothing.

import { loadMeth } from "./meth.js";

const status = document.getElementById("status");
const results = document.getElementById("results");

try {
	const meth = await loadMeth("meth.wasm");
	const response = await fetch("tests/cases.txt");
	const cases = (await response.text())
		.split("\n")
		.map((line) => line.trim())
		.filter((line) => line !== "" && !line.startsWith("#"));
	const outcome = cases.map((input) => ({ input, ...meth.evaluate(input) }));

	results.textContent = JSON.stringify(outcome);
	status.textContent = `${outcome.length} expressions evaluated`;
	document.body.dataset.done = "1";
} catch (error) {
	results.textContent = JSON.stringify({ error: String(error) });
	status.textContent = String(error);
	document.body.dataset.done = "1";
}
