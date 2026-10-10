// Runs the C test groups that are in tests.wasm and puts the outcome in the
// page as JSON: { groups: [{ name, checks, output }], failures }. The module
// prints the FAIL lines through console_write. tools/test_web.py reads it.

import { loadModule } from "./meth.js";

const status = document.getElementById("status");
const results = document.getElementById("results");

try {
	let output = "";
	const api = await loadModule("tests.wasm", (text) => {
		output += text;
	});
	const decoder = new TextDecoder();
	const nameOf = (n) => {
		const bytes = new Uint8Array(api.memory.buffer);
		const start = api.meth_test_name(n);
		let end = start;

		while (bytes[end] !== 0) {
			end++;
		}

		return decoder.decode(bytes.subarray(start, end));
	};
	const groups = [];

	for (let n = 0; n < api.meth_test_count(); n++) {
		output = "";
		const checks = api.meth_test_run(n);

		groups.push({ name: nameOf(n), checks, output });
	}

	output = "";

	const failures = api.meth_test_finish();

	results.textContent = JSON.stringify({ groups, failures, output });
	status.textContent = `${groups.length} groups, ${failures} failed`;
} catch (error) {
	results.textContent = JSON.stringify({ error: String(error) });
	status.textContent = String(error);
}
