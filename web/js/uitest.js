// Drives the notebook the way a user does (typing, keys, clicks) and checks
// what the page shows and what the library holds. The outcome is put in the
// page as JSON: [{ name, ok, detail }]. tools/test_web.py reads it.

import { loadMeth } from "./meth.js";
import { Notebook } from "./notebook.js";
import { Help } from "./help.js";

const status = document.getElementById("status");
const results = document.getElementById("results");
const outcome = [];

function check(name, ok, detail = "") {
	outcome.push({ name, ok: Boolean(ok), detail: String(detail) });
}

try {
	const meth = await loadMeth("meth.wasm");
	const root = document.getElementById("notebook");
	const messages = [];
	const notebook = new Notebook(meth, root, {
		domains: meth.domains(),
		say: (message) => messages.push(message),
		onChange: () => {},
	});

	const area = (i) => root.querySelector(`textarea[data-i="${i}"]`);
	const out = (i) => root.querySelector(`.line[data-i="${i}"] .out`);
	const text = (i) => meth.lines(i).lines[0].text;
	const type = (i, value, caret = value.length) => {
		const a = area(i);

		a.focus();
		a.value = value;
		a.setSelectionRange(caret, caret);
		a.dispatchEvent(new Event("input", { bubbles: true }));
	};
	const press = (i, key, init = {}) => {
		const event = new KeyboardEvent("keydown", { key, bubbles: true, cancelable: true, ...init });

		area(i).dispatchEvent(event);
		return event;
	};
	const cells = () => root.querySelectorAll("section.cell").length;

	notebook.load("m:algebra a = 3\nm:algebra a * 2\nm:algebra det ([[1, 2], [3, 4]])\n");

	check("load: three lines", root.querySelectorAll(".line").length === 3);
	check("load: result", out(1).textContent.includes("6"), out(1).textContent);
	check("load: highlight", root.querySelectorAll(".line .input pre span").length > 3);

	type(0, "a = 10");
	check("type: result below", out(1).textContent.includes("20"), out(1).textContent);
	check("type: library has it", text(0) === "a = 10");

	type(1, "foo (1)");
	check("error: shown", out(1).classList.contains("error"), out(1).className);
	check("error: underlined", root.querySelectorAll(".c-error").length > 0);
	type(1, "a * 2");

	// keys
	type(1, "12+34", 2);
	press(1, "Enter");
	check("enter: splits", notebook.count === 4 && text(1) === "12" && text(2) === "+34", [1, 2].map(text));
	press(2, "Backspace");
	check("backspace: joins", notebook.count === 3 && text(1) === "12+34", [text(1), notebook.count]);
	check("join: result", out(1).textContent.includes("46"), out(1).textContent);

	const before = cells();

	press(1, "Enter", { shiftKey: true });
	check("shift+enter: new cell", cells() === before + 1 && notebook.lines.some((l) => l.kind === "blank"), cells());

	// the new cell is empty; removing it again with the button
	const added = root.querySelectorAll("section.cell.code");
	const last = added[added.length - 1];

	last.querySelector("button.danger").click();
	check("delete cell", cells() === before, cells());

	// domain of a cell
	const select = root.querySelector("section.cell.code select");

	select.value = "lambda";
	select.dispatchEvent(new Event("change"));
	check("domain: changed", notebook.lines.filter((l) => l.kind === "math").every((l) => l.domain === "lambda"), notebook.lines.map((l) => l.domain));
	select.value = "algebra";
	select.dispatchEvent(new Event("change"));

	// undo and redo as one step for a burst of typing
	notebook.checkpoint();
	type(0, "a = 1");
	type(0, "a = 12");
	type(0, "a = 123");
	check("typing: result", out(1).textContent.length > 0);
	notebook.checkpoint();
	area(0).dispatchEvent(new KeyboardEvent("keydown", { key: "z", ctrlKey: true, bubbles: true, cancelable: true }));
	check("undo: one step", text(0) === "a = 10", text(0));
	area(0).dispatchEvent(new KeyboardEvent("keydown", { key: "y", ctrlKey: true, bubbles: true, cancelable: true }));
	check("redo", text(0) === "a = 123", text(0));

	// completion
	type(2, "s");
	check("completion: popup", root.querySelectorAll(".completion li").length >= 2, root.querySelectorAll(".completion li").length);
	type(2, "sq");
	check("completion: ghost", root.querySelector(".input .ghost") !== null);
	press(2, "Tab");
	check("completion: tab accepts", text(2) === "sqrt", text(2));
	type(2, "det ([[1, 2], [3, 4]])");

	// steps on request
	const toggle = out(2).querySelector(".steps-toggle");

	check("steps: button", toggle !== null);
	toggle.click();
	check("steps: rows", out(2).querySelector("pre.steps") !== null);

	// plots
	notebook.load("m:algebra hist ([1, 2, 2, 3, 3, 3], 3)\nm:algebra plot (x^2, 0, 3)\n");
	check("plot: svg", out(0).querySelector("svg.plot") !== null);

	// a text cell
	notebook.load("# # Title\n# some *text*\n\nm:algebra 1 + 1\n");
	const view = root.querySelector("section.cell.text .view");

	check("note: rendered", view.querySelector("h1") !== null && view.querySelector("em") !== null);
	view.click();

	const note = root.querySelector("section.cell.text textarea");

	check("note: editing", root.querySelector("section.cell.text.editing") !== null);
	note.value = "# Title\nmore\nlines";
	note.dispatchEvent(new Event("input", { bubbles: true }));
	check("note: lines follow", meth.lines(0).lines.slice(0, 3).map((l) => l.text).join("|") === "# Title|more|lines", meth.lines(0).lines.map((l) => l.text));
	note.blur();
	check("note: view again", root.querySelector("section.cell.text .view h1") !== null);

	// new cells from the bars
	const count = cells();

	root.querySelector(".insert.last button:nth-child(2)").click();
	check("insert: text cell", cells() === count + 1 && root.querySelectorAll("section.cell.text").length === 2, cells());

	// help
	const help = new Help(meth, document.getElementById("help"), {
		tryExample: (lines) => notebook.insertText(notebook.count - 1, lines.map((l) => `m:algebra ${l}`).join("\n")),
	});

	help.open("syntax");
	check("help: page", document.querySelector("#help h1") !== null && !document.getElementById("help").hidden);
	check("help: examples", document.querySelectorAll("#help pre.example").length > 0);
	help.search.value = "matrix";
	help.find("matrix");
	check("help: search", document.querySelectorAll("#help .hits li").length > 0);
	help.close();
	check("help: closed", document.getElementById("help").hidden);

	// an empty notebook
	notebook.load("");
	check("empty: only the bar", root.querySelectorAll("section.cell").length === 0 && root.querySelector(".insert.last") !== null);
	root.querySelector(".insert.last button").click();
	check("empty: first cell", cells() === 1 && notebook.count === 1);
} catch (error) {
	check("ui test ran", false, error.stack ?? error);
}

results.textContent = JSON.stringify(outcome);
status.textContent = `${outcome.filter((c) => c.ok).length} of ${outcome.length} passed`;
