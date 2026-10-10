// Calls every method of the page API (js/meth.js) against the module and puts
// the outcome in the page as JSON: [{ name, ok, detail }]. The C side is
// tested by the webapi group; this checks the other half, the strings that
// cross the border, the numbers and the parsing. tools/test_web.py reads it.

import { EDIT, loadMeth } from "./meth.js";

const status = document.getElementById("status");
const results = document.getElementById("results");
const outcome = [];

function check(name, ok, detail = "") {
	outcome.push({ name, ok: Boolean(ok), detail: String(detail) });
}

try {
	const meth = await loadMeth("meth.wasm");
	const file =
		"# Notesä with unicode ≈\n" +
		"m:algebra a = 3\n" +
		"m:algebra a * 2 + 1/2\n" +
		"m:algebra steps (det ([[1, 2], [3, 4]]))\n" +
		"m:algebra 1 +\n";

	let answer = meth.load(file);
	check("load: count", answer.count === 5, answer.count);
	check("load: note text", answer.lines[0].text.startsWith("Notesä"));
	check("load: value", answer.lines[2].result.text === "13/2");
	check("load: error", answer.lines[4].result.kind === "error");
	check("load: block", answer.lines[1].first === 1);
	check("load: wants_steps", answer.lines[3].wants_steps === true);

	check("save: round trip", meth.save() === file, JSON.stringify(meth.save()));

	answer = meth.edit(EDIT.replace, 1, "math", "algebra", "a = 10");
	check("edit: from", answer.from === 1, answer.from);
	check("edit: flows down", answer.lines[1].result.text === "41/2");
	answer = meth.edit(EDIT.insert, 5, "note", "", "end");
	check("edit: insert", answer.count === 6);
	answer = meth.edit(EDIT.delete, 5);
	check("edit: delete", answer.count === 5);
	answer = meth.edit(EDIT.undo, 0);
	check("edit: undo", answer.count === 6);
	answer = meth.edit(EDIT.redo, 0);
	check("edit: redo", answer.count === 5);

	let failed = null;

	try {
		meth.edit(EDIT.replace, 99, "math", "algebra", "1");
	} catch (error) {
		failed = error;
	}

	check("edit: invalid index throws", failed && failed.code === 2, failed);

	const runs = meth.highlight("algebra", 3, "sin (a) + 2");
	check("highlight: function", JSON.stringify(runs[0]) === '[0,3,"function"]');
	check("highlight: covers", runs.reduce((n, run) => n + run[1], 0) === 11);

	let ghost = meth.ghost("algebra", 3, "a * 2");
	check("ghost: value", ghost.shown && ghost.text === "20");
	ghost = meth.ghost("algebra", 3, "2 +");
	check("ghost: nothing", ghost.shown === false);
	ghost = meth.ghost("algebra", 3, "[[1, 2], [3, 4]] * 2");
	check("ghost: block", ghost.block.length === 2, ghost.block);

	const completion = meth.complete("algebra", 3, "2 * sq");
	check("complete: sqrt", completion.items.includes("sqrt"), completion.items);
	check("complete: start", completion.start === 4, completion.start);

	const steps = meth.steps(3);
	check("steps: found", steps.found && steps.rows.length > 0, steps.rows.length);

	check("export: latex", meth.exportAs("latex", "body").includes("\\begin{"));
	check("export: markdown", meth.exportAs("markdown").includes("$$"));
	check("domains", meth.domains().includes("proof"));
	check("typeset: fraction", meth.typeset("algebra", "1/2").includes("<mfrac>"));
	check("typeset: logic", meth.typeset("boolean", "a && b").includes("∧"));
	let notFormula = null;

	try {
		meth.typeset("algebra", "2 +");
	} catch (error) {
		notFormula = error;
	}

	check("typeset: not a formula throws", notFormula && notFormula.code === 6);

	meth.load("m:proof theorem t: x + x = 2x\nm:proof x + x = 2x\n");
	check("hint", meth.hint(1).reason === "simplify");

	answer = meth.setSymbols(true);
	check("setSymbols", answer.count === 2);

	const docs = meth.docs();
	check("docs: list", docs.length > 20 && docs.some((p) => p.id === "index"), docs.length);
	const page = meth.doc("syntax");
	check("doc: blocks", page.blocks.length > 3 && page.title !== "", page.title);
	check("search", meth.searchDocs("matrix").length > 0);
	let missing = null;

	try {
		meth.doc("no-such-page");
	} catch (error) {
		missing = error;
	}

	check("doc: unknown id throws", missing && missing.code === 2);

	const examples = meth.examples();
	check("examples: list", examples.length > 10 && examples[0].title !== "", examples.length);
	const example = meth.load(meth.example(examples[0].name));
	check("example: loads", example.count > 2, example.count);
	const snippets = meth.snippets();
	check("snippets: list", snippets.length > 0 && snippets[0].summary !== "");
	check("snippet: text", meth.snippet(snippets[0].name).includes("m:"));
	const lessons = meth.lessons();
	check("lessons", lessons.length > 3 && lessons[0].steps.length > 0, lessons.length);

	const themes = meth.themes();
	check("themes: builtin", themes.length >= 4 && themes[0].name === "dark");
	check("themes: classes", /^#[0-9a-f]{6}$/.test(themes[0].classes.number.fg ?? ""));
	const custom = meth.themeFrom("number = #ff0000 bold\nnonsense\n");
	check("theme text", custom.classes.number.fg === "#ff0000" && custom.classes.number.bold && custom.bad === 1, JSON.stringify(custom.classes.number));

	check("evaluate still works", meth.evaluate("1/3 + 1/6").code === 0);
} catch (error) {
	check("api test ran", false, error.stack ?? error);
}

results.textContent = JSON.stringify(outcome);
status.textContent = `${outcome.filter((c) => c.ok).length} of ${outcome.length} passed`;
