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
		enableSymbols: () => {
			notebook.applyAnswer(meth.setSymbols(true));
			notebook.render();
		},
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
	const NL_ = String.fromCharCode(10);
	const cells = () => root.querySelectorAll("section.cell").length;

	notebook.load("m:algebra a = 3\nm:algebra a * 2\nm:algebra det ([[1, 2], [3, 4]])\n");

	check("load: three lines", root.querySelectorAll(".line").length === 3);

	// by default only the last expression of a cell shows its result
	check("results: definition hidden", out(0).textContent === "", out(0).textContent);
	check("results: not the last hidden", out(1).textContent === "", out(1).textContent);
	check("results: last shown", out(2).textContent.includes("2"), out(2).textContent);

	// the button of one line
	const showResult = (i) => [...root.querySelectorAll(`.line[data-i="${i}"] .line-tools .tool-btn`)].find((b) => /result/.test(b.textContent));

	showResult(1).click();
	check("results: show one line", out(1).textContent.includes("6"), out(1).textContent);
	check("results: button says hide", /hide/.test(showResult(1).textContent));
	showResult(1).click();
	check("results: hide it again", out(1).textContent === "");

	// the button of the cell: every result
	const allButton = () => [...root.querySelectorAll("section.cell.code > .cell-head button")].find((b) => /results?$/.test(b.textContent));

	check("results: cell button", allButton() !== null && /last result/.test(allButton().textContent));
	allButton().click();
	check("results: all shown", out(0).textContent.includes("3") && out(1).textContent.includes("6") && out(2).textContent.includes("2"), [0, 1, 2].map((i) => out(i).textContent));
	check("results: pressed", allButton().getAttribute("aria-pressed") === "true");

	// folding a cell
	const fold = () => root.querySelector("section.cell.code > .fold");

	fold().click();
	check("fold: summary", root.querySelector("section.cell.code.folded .summary .more")?.textContent === "3 lines" && root.querySelectorAll(".line").length === 0, root.querySelectorAll(".line").length);
	fold().click();
	check("fold: open again", root.querySelectorAll(".line").length === 3 && out(0).textContent.includes("3"));
	check("fold: state kept", notebook.viewState().allResults.length === 1 && notebook.viewState().collapsed.length === 0);
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
	const toggle = [...root.querySelectorAll('.line[data-i="2"] .line-tools .tool-btn')].find((b) => b.textContent === "steps");

	check("steps: button", toggle !== null);
	toggle.click();
	check("steps: rows", out(2).querySelector(".steps-list") !== null);
	check("steps: matrices are MathML", out(2).querySelectorAll(".steps-list .step-matrix math mtable").length >= 2 && out(2).querySelector("pre.steps") === null, out(2).querySelectorAll(".step-matrix").length);
	check("steps: text rows", out(2).querySelector(".steps-list .step-text")?.textContent.startsWith("Eliminate"));

	// the notes of rules stand in one column
	notebook.load("m:algebra diff (x^3 + sin (x), x)" + String.fromCharCode(10));
	notebook.stepsOpen.add(0);
	notebook.render(null);
	const formulas = [...root.querySelectorAll(".steps-list .step-formula")];
	const notes = [...root.querySelectorAll(".steps-list .step-note")];

	check("steps: rules with notes", formulas.length === 3 && notes.length === 3 && notes[0].textContent === "(power rule)", [formulas.length, notes.length]);
	check("steps: notes in one column", new Set(notes.map((n) => Math.round(n.getBoundingClientRect().left))).size === 1, notes.map((n) => n.getBoundingClientRect().left));
	notebook.stepsOpen.clear();
	notebook.load("m:algebra a = 3" + String.fromCharCode(10) + "m:algebra a * 2" + String.fromCharCode(10) + "m:algebra det ([[1, 2], [3, 4]])" + String.fromCharCode(10));
	notebook.toggleAllResults(0);

	// deleting a cell is one step to undo
	notebook.load("m:algebra a = 1" + String.fromCharCode(10) + "m:algebra a + 1" + String.fromCharCode(10) + "m:algebra a + 2" + String.fromCharCode(10));
	notebook.deleteCell(0, 2);
	check("undo cell: deleted", notebook.count === 0 && cells() === 0, notebook.count);
	notebook.undo();
	check("undo cell: the whole cell is back", notebook.count === 3 && root.querySelectorAll(".line").length === 3 && notebook.lines[2].text === "a + 2", notebook.count);
	notebook.redo();
	check("redo cell: gone again", notebook.count === 0);
	notebook.undo();

	// the same for the other actions that change several lines
	notebook.insertCell(3, "math");
	check("undo insert: a cell is one step", notebook.count === 5 && notebook.lines[3].kind === "blank", notebook.lines.map((l) => l.kind));
	notebook.undo();
	check("undo insert: back", notebook.count === 3, notebook.count);
	area(1).focus();
	area(1).setSelectionRange(1, 1);
	press(1, "Enter");
	check("split: two lines", notebook.count === 4);
	notebook.undo();
	check("undo split: one step", notebook.count === 3 && notebook.lines[1].text === "a + 1", [notebook.count, notebook.lines[1].text]);
	const select2 = root.querySelector("section.cell.code select");

	select2.value = "lambda";
	select2.dispatchEvent(new Event("change"));
	notebook.undo();
	check("undo domain: every line of the cell", notebook.lines.every((l) => l.domain === "algebra"), notebook.lines.map((l) => l.domain));

	// plots
	notebook.load("m:algebra hist ([1, 2, 2, 3, 3, 3], 3)\nm:algebra plot (x^2, 0, 3)\n");
	notebook.toggleAllResults(0);
	check("plot: svg", out(0).querySelector("svg.plot") !== null);

	// results typeset as MathML
	document.activeElement.blur();
	notebook.load("m:algebra 1/3 + 1/6" + String.fromCharCode(10) + "m:algebra sqrt (2) * 3" + String.fromCharCode(10) + "m:algebra [[1, 2], [3, 4]]^-1" + String.fromCharCode(10) + "m:algebra foo (1)" + String.fromCharCode(10) + "m:algebra a = 3");
	notebook.toggleAllResults(0);
	check("mathml: fraction", out(0).querySelector("math mfrac") !== null, out(0).innerHTML);
	check("mathml: approx mark", out(1).querySelector(".eq")?.textContent === "≈" && out(1).querySelector("math") !== null);
	check("mathml: matrix", out(2).querySelector("math mtable") !== null);
	check("mathml: error stays text", out(3).querySelector("math") === null && out(3).classList.contains("error"));
	check("mathml: plain value", out(4).querySelector("math mn") !== null);
	check("mathml: no ascii art block", out(2).querySelector("pre.block") === null);

	// a name without a value: the error offers symbols
	notebook.load("m:algebra x + x" + NL_);
	check("symbols: undefined name offers them", out(0).classList.contains("error") && out(0).querySelector(".symbol-hint") !== null, out(0).textContent);
	out(0).querySelector(".symbol-hint").click();
	check("symbols: then it is a value", out(0).classList.contains("error") === false && out(0).textContent.includes("2"), out(0).textContent);
	meth.setSymbols(false);
	notebook.load("m:algebra 1 + 1" + NL_);

	// the input of a line is typeset while it is not being edited
	notebook.load("m:algebra x = 1/2 + sqrt (3)" + NL_ + "m:algebra foo (" + NL_ + "m:algebra a && b");
	document.activeElement.blur();
	const lineRow = (i) => root.querySelector(`.line[data-i="${i}"]`);
	const shownBox = (e) => getComputedStyle(e).display !== "none";

	notebook.render(null);
	check("input: typeset", lineRow(0).classList.contains("typeset") && lineRow(0).querySelector(".tview math mfrac") !== null && lineRow(0).querySelector(".tview math msqrt") !== null);
	check("input: typeset shown, source hidden", shownBox(lineRow(0).querySelector(".tview")) && !shownBox(lineRow(0).querySelector("textarea")) && !shownBox(lineRow(0).querySelector(".input > pre")));
	check("input: not a formula keeps its source", !lineRow(1).classList.contains("typeset") && shownBox(lineRow(1).querySelector("textarea")) && !shownBox(lineRow(1).querySelector(".tview")));
	check("input: other domains are typeset too", notebook.lines[2].domain === "algebra" || lineRow(2).classList.contains("typeset"));
	lineRow(0).querySelector(".tview").click();
	check("input: click edits the source", lineRow(0).classList.contains("editing") && shownBox(lineRow(0).querySelector("textarea")) && !shownBox(lineRow(0).querySelector(".tview")));
	check("input: the source is the text", lineRow(0).querySelector("textarea").value === "x = 1/2 + sqrt (3)");
	lineRow(0).querySelector("textarea").dispatchEvent(new Event("blur"));
	check("input: leaving shows it typeset again", lineRow(0).classList.contains("typeset") && shownBox(lineRow(0).querySelector(".tview")) && !lineRow(0).classList.contains("editing"));

	// a text cell: lines shown rendered, the line being edited shows its source
	document.activeElement.blur();
	notebook.load("# # Title\n# some *text*\n\nm:algebra 1 + 1\n");

	const noteView = (i) => root.querySelector(`.nline[data-i="${i}"] .view`);
	const noteRow = (i) => root.querySelector(`.nline[data-i="${i}"]`);

	check("note: rendered", noteView(0).querySelector("h1") !== null && noteView(1).querySelector("em") !== null);
	check("note: none is editing", root.querySelector(".nline.editing") === null);
	noteView(1).click();
	check("note: line editing", noteRow(1).classList.contains("editing") && !noteRow(0).classList.contains("editing"));
	check("note: others stay rendered", getComputedStyle(noteView(0)).display !== "none" && getComputedStyle(noteView(1)).display === "none");
	type(1, "some *text* and **more**");
	check("note: library has it", text(1) === "some *text* and **more**", text(1));
	press(1, "Enter");
	check("note: enter splits", notebook.count === 5 && notebook.lines[2].kind === "note", notebook.count);
	type(2, "- a list item");
	area(2).blur();
	check("note: list rendered", noteView(2).querySelector("li") !== null);
	check("note: edited line rendered again", noteView(1).querySelector("strong") !== null);
	check("note: stays one cell", root.querySelectorAll("section.cell.text").length === 1);

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
