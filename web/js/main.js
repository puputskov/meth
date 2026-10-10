import { loadMeth } from "./meth.js";

const input = document.getElementById("in");
const out = document.getElementById("out");

try {
	const meth = await loadMeth("meth.wasm");

	input.disabled = false;
	input.focus();
	input.addEventListener("keydown", (event) => {
		if (event.key === "Enter" && input.value.trim() !== "") {
			const { code, text } = meth.evaluate(input.value);
			const item = document.createElement("li");
			const expr = document.createElement("div");
			const result = document.createElement("div");

			expr.className = "expr";
			expr.textContent = input.value;
			result.textContent = text.trim();
			result.className = code === 0 ? "result" : "error";
			item.append(expr, result);
			out.prepend(item);
			input.value = "";
		}
	});

	// Evaluating the expression of the address (#expression) makes the page
	// testable without typing: web/index.html#1/3+1/6
	if (location.hash.length > 1) {
		const { code, text } = meth.evaluate(
			decodeURIComponent(location.hash.slice(1)),
		);

		document.body.dataset.code = String(code);
		document.body.dataset.result = text.trim();
	}
} catch (error) {
	document.body.dataset.error = String(error);
	out.textContent = String(error);
}
