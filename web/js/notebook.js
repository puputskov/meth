// The notebook: the lines of the session as cells. Code lines are a textarea
// over a highlighted copy of the text, with the result underneath; notes are
// shown as Markdown and edited as text. The library holds the truth: every
// change goes through meth.edit (), and the answer (the lines from the changed
// one on) is what the page shows.

import { EDIT } from "./meth.js";
import { renderNote } from "./markdown.js";
import { plotSvg } from "./plot.js";

const IDLE_MS = 900;

function el(tag, className, attributes = {}) {
	const e = document.createElement(tag);

	if (className) {
		e.className = className;
	}

	for (const [key, value] of Object.entries(attributes)) {
		e.setAttribute(key, value);
	}

	return e;
}

function button(label, handler, className = "", title = "") {
	const b = el("button", className, { type: "button" });

	b.textContent = label;
	b.title = title;
	b.addEventListener("click", handler);
	return b;
}

// Whether line b would join the block of line a if they were next to each
// other (the same rule as ml_block_continues).
function merges(a, b) {
	return (
		Boolean(a) &&
		Boolean(b) &&
		a.kind !== "blank" &&
		a.kind === b.kind &&
		(a.kind !== "math" || a.domain === b.domain)
	);
}

// The domain a new code cell gets after line `line`: its own, or the default.
function ml_domain(line, fallback) {
	return line.kind === "math" ? line.domain : fallback;
}

// A line that gives a name a value (a = 3, f(x) = x^2): what it says is in the
// line, so its result is not shown unless it is asked for.
function isDefinition(line) {
	return (
		line.domain !== "proof" &&
		/^\s*[\p{L}_][\p{L}\p{N}_']*\s*(\([^)]*\))?\s*=(?!=)/u.test(line.text)
	);
}

export class Notebook {
	constructor(meth, root, { domains, onChange, say }) {
		this.meth = meth;
		this.root = root;
		this.domains = domains;
		this.onChange = onChange;
		this.say = say;
		this.lines = [];
		this.domain = "algebra"; // for new cells
		this.signature = "";
		this.stepsOpen = new Set();
		this.forced = new Set(); // lines whose result is shown against the rule
		this.allResults = new Set(); // first lines of the cells that show every result
		this.collapsed = new Set(); // the first lines of the cells that are folded
		this.burst = false; // typing since the last checkpoint
		this.timer = 0;
		this.completion = null; // { index, items, selected, ghost, start }
		this.charWidth = 0;
		this.mathCache = new Map(); // "domain + text" -> MathML, or null
	}

	// ---- the session ---------------------------------------------------

	load(text, view = {}) {
		this.applyAnswer(this.meth.load(text));
		this.stepsOpen.clear();

		const within = (list) => new Set((list ?? []).filter((i) => i < this.count));

		this.forced = within(view.forced);
		this.allResults = within(view.allResults);
		this.collapsed = within(view.collapsed);
		this.render();
		this.onChange();
	}

	get count() {
		return this.lines.length;
	}

	// Puts the answer ({ from, count, lines }) of the library in the copy of
	// the lines; returns where the change starts.
	applyAnswer(answer) {
		this.lines.length = answer.count;

		for (const line of answer.lines) {
			this.lines[line.i] = line;
		}

		this.findBlocks();
		return answer.from;
	}

	// The answer holds the block of the lines it sends; a change further down
	// can also change the block of a line above it, so they are found again
	// here, with the same rule as the library (merges ()).
	findBlocks() {
		let first = 0;

		for (let i = 0; i < this.count; i++) {
			if (i > 0 && !merges(this.lines[i - 1], this.lines[i])) {
				first = i;
			}

			this.lines[i].first = first;
		}

		let last = this.count - 1;

		for (let i = this.count - 1; i >= 0; i--) {
			if (i < this.count - 1 && !merges(this.lines[i], this.lines[i + 1])) {
				last = i;
			}

			this.lines[i].last = last;
		}
	}

	checkpoint() {
		if (this.burst) {
			this.burst = false;
			clearTimeout(this.timer);
			this.meth.edit(EDIT.checkpoint, 0);
		}
	}

	// A change of the session. `typing` joins the undo step of the typing
	// before it; any other change ends that step first.
	change(op, index, kind, domain, text, { typing = false } = {}) {
		if (typing) {
			if (!this.burst) {
				this.meth.edit(EDIT.checkpoint, 0);
				this.burst = true;
			}

			clearTimeout(this.timer);
			this.timer = setTimeout(() => {
				this.burst = false;
			}, IDLE_MS);
		} else {
			this.checkpoint();
		}

		const from = this.applyAnswer(this.meth.edit(op, index, kind, domain, text));

		if (op === EDIT.insert || op === EDIT.delete) {
			this.shiftView(op === EDIT.insert ? 1 : -1, index);
		}

		return from;
	}

	// The lines that were at or after `index` have moved by `by` (a deleted
	// line is gone).
	shiftView(by, index) {
		const move = (set) =>
			new Set(
				[...set]
					.filter((i) => !(by < 0 && i === index))
					.map((i) => (i >= index ? i + by : i)),
			);

		this.forced = move(this.forced);
		this.allResults = move(this.allResults);
		this.collapsed = move(this.collapsed);
		this.stepsOpen = move(this.stepsOpen);
	}

	// What the page keeps with the text of a file: the results shown against
	// the rule, the cells that show every result and the cells that are
	// folded (line numbers).
	viewState() {
		return {
			forced: [...this.forced],
			allResults: [...this.allResults],
			collapsed: [...this.collapsed],
		};
	}

	// ---- what is shown ---------------------------------------------------

	// The line of the cell that has the result shown by default: the last one
	// that is an expression (not a definition) and has a value.
	lastExpression(line) {
		let found = -1;

		for (let j = line.last; j >= line.first && found < 0; j--) {
			const l = this.lines[j];

			if (l.result.kind === "value" && !isDefinition(l)) {
				found = j;
			}
		}

		return found;
	}

	// Whether the value of a result is shown. Errors and the verdicts of a
	// proof always are. Of the others: the last expression of the cell, or
	// every line when the cell is set to show all; and the user can turn any
	// line the other way round (forced).
	visible(i) {
		const line = this.lines[i];
		const result = line.result;
		let shown = false;

		if (result.kind === "error" || line.domain === "proof") {
			shown = result.kind !== "none";
		} else if (result.kind === "value") {
			const byRule = this.allResults.has(line.first) || i === this.lastExpression(line);

			shown = byRule !== this.forced.has(i);
		}

		return shown;
	}

	toggleResult(i) {
		if (this.forced.has(i)) {
			this.forced.delete(i);
		} else {
			this.forced.add(i);
		}

		this.repaintBlock(i);
		this.onChange();
	}

	// The button of the cell: every result of the cell, or the last one.
	toggleAllResults(first) {
		const line = this.lines[first];

		if (this.allResults.has(first)) {
			this.allResults.delete(first);
		} else {
			this.allResults.add(first);
		}

		// a choice made for the whole cell replaces the ones made for lines
		for (let k = line.first; k <= line.last; k++) {
			this.forced.delete(k);
		}

		this.repaintBlock(first);
		this.onChange();
		this.render();
	}

	toggleFold(first) {
		if (this.collapsed.has(first)) {
			this.collapsed.delete(first);
		} else {
			this.collapsed.add(first);
		}

		this.render(null);
		this.onChange();
	}

	// Paints again every line of the cell of line i (what is the last
	// expression of a cell can change with any of its lines).
	repaintBlock(i) {
		const line = this.lines[i];

		for (let k = line.first; k <= line.last; k++) {
			if (this.lines[k].kind === "math") {
				this.paintLine(k);
			}
		}
	}

	// ---- drawing -------------------------------------------------------

	structure() {
		return this.lines
			.map((l) => `${l.kind}${l.domain ?? ""}${l.first}${l.last}`)
			.join("|");
	}

	// Draws the whole notebook again, keeping the focus where it was.
	render(focus = this.currentFocus()) {
		this.hideCompletion();
		this.root.replaceChildren();
		this.signature = this.structure();

		const insertAt = (pos, last = false) => this.insertBar(pos, last);
		let i = 0;

		this.root.append(insertAt(0));

		while (i < this.count) {
			const line = this.lines[i];

			if (line.kind === "blank") {
				this.root.append(insertAt(i + 1));
				i += 1;
			} else {
				this.root.append(this.cell(line.first, line.last));
				i = line.last + 1;

				const next = this.lines[i];

				if (next && next.kind !== "blank") {
					this.root.append(insertAt(i));
				}
			}
		}

		this.root.append(insertAt(this.count, true));

		for (let k = 0; k < this.count; k++) {
			if (this.lines[k].kind === "math") {
				this.paintLine(k);
			}
		}

		if (focus) {
			this.focusLine(focus.line, focus.start, focus.end);
		}
	}

	insertBar(pos, last = false) {
		const bar = el("div", last ? "insert last" : "insert");

		bar.append(
			button("+ Code", () => this.insertCell(pos, "math"), "", "Add a code cell"),
			button("+ Text", () => this.insertCell(pos, "note"), "", "Add a text cell"),
		);
		return bar;
	}

	cell(first, last) {
		const kind = this.lines[first].kind;
		let cell;

		if (kind === "math") {
			cell = this.codeCell(first, last);
		} else if (kind === "note") {
			cell = this.textCell(first, last);
		} else {
			cell = this.rawCell(first, last);
		}

		return cell;
	}

	head(first, last, domainSelect) {
		const head = el("div", "cell-head");

		if (domainSelect) {
			const select = el("select", "", { "aria-label": "Domain of the cell" });

			for (const name of this.domains) {
				const option = el("option");

				option.value = name;
				option.textContent = name;
				select.append(option);
			}

			select.value = this.lines[first].domain;
			select.addEventListener("change", () => this.setDomain(first, last, select.value));
			head.append(select);
		}

		if (domainSelect) {
			const all = this.allResults.has(first);
			const results = button(
				all ? "all results" : "last result",
				() => this.toggleAllResults(first),
				"",
				"Show the result of every line of the cell, or of the last one only",
			);

			results.setAttribute("aria-pressed", String(all));
			head.append(results);
		}

		head.append(
			el("span", "spacer"),
			button("delete", () => this.deleteCell(first, last), "danger", "Delete the cell"),
		);
		return head;
	}

	codeCell(first, last) {
		const cell = el("section", "cell code", { "data-first": first });
		const folded = this.collapsed.has(first);
		const fold = button(
			folded ? "▸" : "▾",
			() => this.toggleFold(first),
			"fold",
			folded ? "Unfold the cell" : "Fold the cell",
		);

		fold.setAttribute("aria-expanded", String(!folded));
		cell.classList.toggle("folded", folded);
		cell.append(fold, this.head(first, last, true));

		if (folded) {
			const summary = el("div", "summary");
			const count = last - first + 1;
			const preview = el("span", "preview");

			preview.textContent = this.lines[first].text;
			summary.append(preview);

			if (count > 1) {
				const more = el("span", "more");

				more.textContent = `${count} lines`;
				summary.append(more);
			}

			summary.addEventListener("click", () => this.toggleFold(first));
			cell.append(summary);
		} else {
			for (let i = first; i <= last; i++) {
				cell.append(this.codeLine(i));
			}
		}

		return cell;
	}

	codeLine(i) {
		const line = this.lines[i];
		const row = el("div", "line", { "data-i": i });
		const input = el("div", "input");
		const shown = el("pre", "", { "aria-hidden": "true" });
		const area = el("textarea", "", {
			rows: 1,
			spellcheck: "false",
			autocapitalize: "off",
			autocomplete: "off",
			"aria-label": `Line ${i + 1}`,
			"data-i": i,
		});
		const out = el("div", "out");

		area.value = line.text;
		area.addEventListener("input", () => this.typed(i, area));
		area.addEventListener("keydown", (event) => this.keydown(event, i, area));
		area.addEventListener("focus", () => {
			row.classList.add("focus");
			this.refreshCompletion(i, area);
		});
		area.addEventListener("blur", () => {
			row.classList.remove("focus");
			setTimeout(() => this.hideCompletionIfIdle(), 120);
		});
		area.addEventListener("click", () => this.refreshCompletion(i, area));
		input.append(shown, area);
		row.append(input, out);
		return row;
	}

	// A note line is shown rendered, except the one being edited, which shows
	// its Markdown: so the text reads as the page it will be, and the marks are
	// there to change when the caret is in the line.
	textCell(first, last) {
		const cell = el("section", "cell text", { "data-first": first });

		cell.append(this.head(first, last, false));

		for (let i = first; i <= last; i++) {
			const row = this.noteLine(i);

			row.classList.toggle("only", first === last);
			cell.append(row);
		}

		return cell;
	}

	noteLine(i) {
		const row = el("div", "nline", { "data-i": i });
		const view = el("div", "view");
		const area = el("textarea", "", {
			rows: 1,
			spellcheck: "true",
			"aria-label": `Note line ${i + 1}`,
			"data-i": i,
		});
		const grow = () => {
			area.style.height = "auto";
			area.style.height = `${area.scrollHeight}px`;
		};

		area.value = this.lines[i].text;
		renderNote(view, [this.lines[i].text]);
		const edit = () => {
			row.classList.add("editing");
			grow();
		};

		row.edit = edit;
		view.addEventListener("click", () => this.focusLine(i, "end"));
		area.addEventListener("focus", edit);
		area.addEventListener("input", () => {
			grow();
			this.typed(i, area);

			// the rendered copy follows the text, so it is right whenever the
			// line is left
			if (row.isConnected && this.lines[i]) {
				renderNote(view, [this.lines[i].text]);
			}
		});
		area.addEventListener("keydown", (event) => this.keydown(event, i, area));
		area.addEventListener("blur", () => {
			row.classList.remove("editing");

			const current = this.lines[i];

			if (current && current.kind === "note") {
				renderNote(view, [current.text]);
			}
		});
		row.append(view, area);
		return row;
	}

	rawCell(first, last) {
		const cell = el("section", "cell raw", { "data-first": first });

		cell.append(this.head(first, last, false));

		for (let i = first; i <= last; i++) {
			const row = el("div");

			row.textContent = this.lines[i].text;
			cell.append(row);
		}

		return cell;
	}

	// The highlighted copy and the result of one code line.
	paintLine(i) {
		const row = this.root.querySelector(`.line[data-i="${i}"]`);

		if (!row) {
			return;
		}

		const line = this.lines[i];
		const shown = row.querySelector(".input pre");
		const area = row.querySelector("textarea");
		const result = line.result;
		const error = result.kind === "error" ? result : { start: 0, len: 0 };

		if (area.value !== line.text) {
			area.value = line.text;
		}

		const runs = this.meth.highlight(line.domain, i, line.text, error.start, error.len);
		const bytes = new TextEncoder().encode(line.text);
		const decoder = new TextDecoder();

		shown.replaceChildren();

		for (const [start, length, cls] of runs) {
			const part = el("span", `c-${cls}`);

			part.textContent = decoder.decode(bytes.subarray(start, start + length));
			shown.append(part);
		}

		if (this.completion && this.completion.index === i && this.completion.ghost) {
			const ghost = el("span", "ghost");

			ghost.textContent = this.completion.ghost;
			shown.append(ghost);
		}

		shown.append("​");
		this.paintResult(row.querySelector(".out"), i);
	}

	paintResult(out, i) {
		const line = this.lines[i];
		const result = line.result;

		out.replaceChildren();
		out.className = "out";

		if (result.kind === "error") {
			out.classList.add("error");
			out.append(result.text);
		} else if (result.kind === "value") {
			const shown = this.visible(i);
			const math = shown ? this.resultMath(line, result) : null;

			if (!shown) {
				// the value is not shown: only the steps, when they are open
			} else if (line.domain === "proof") {
				out.append(this.proofMark(result.text));
			} else if (math) {
				const eq = el("span", "eq");
				const value = el("span", "math-val");

				eq.textContent = result.text.startsWith("≈") ? "≈" : "=";
				value.title = result.text;
				value.append(math);
				out.append(eq, value);
			} else {
				if (!result.block) {
					const approx = result.text.startsWith("≈");
					const value = el("span", "val");

					value.textContent = result.text;

					if (!approx) {
						const eq = el("span", "eq");

						eq.textContent = "=";
						out.append(eq);
					}

					out.append(value);
				} else {
					const block = el("pre", "block");

					block.textContent = result.block.join("\n");
					out.append(el("span", "eq"), block);
					out.firstChild.textContent = "=";
				}

				if (result.plot) {
					const wrap = el("div", "plot-wrap");

					wrap.append(plotSvg(result.plot));
					out.append(wrap);
				}
			}

			if (result.steps) {
				const steps = el("pre", "steps");

				steps.textContent = result.steps.join("\n");
				out.append(steps);
			}

			this.stepsRows(out, i);
		}

		this.paintTools(out.parentElement, i);
	}

	// The small buttons at the right of a line: show or hide its result, and
	// its steps.
	paintTools(row, i) {
		const line = this.lines[i];

		if (!row) {
			return;
		}

		let tools = row.querySelector(".line-tools");

		if (!tools) {
			tools = el("div", "line-tools");
			row.append(tools);
		}

		tools.replaceChildren();

		if (line.result.kind === "value" && line.domain !== "proof") {
			const shown = this.visible(i);

			tools.append(
				button(
					shown ? "hide result" : "show result",
					() => this.toggleResult(i),
					"tool-btn",
					"Show or hide the result of this line",
				),
			);

			if (!line.wants_steps) {
				const open = this.stepsOpen.has(i);
				const steps = button("steps", () => this.toggleSteps(i), "tool-btn", "Show the steps");

				steps.setAttribute("aria-expanded", String(open));
				tools.append(steps);
			}
		}
	}

	// The value of a result as a <math> element, or null when it has no
	// typeset form (a list, a text, an error of the typesetting).
	resultMath(line, result) {
		const logic = line.domain === "boolean" || line.domain === "lambda";
		const typeset = logic || ["exact", "approx", "expr", "matrix"].includes(result.type);
		let element = null;

		if (typeset && line.domain !== "proof") {
			const text = result.text.replace(/^≈\s*/, "");
			const domain = logic ? line.domain : "algebra";
			const key = `${domain}\n${text}`;
			let markup = this.mathCache.get(key);

			if (markup === undefined) {
				try {
					markup = this.meth.typeset(domain, text);
				} catch {
					markup = null;
				}

				if (this.mathCache.size > 2000) {
					this.mathCache.clear();
				}

				this.mathCache.set(key, markup);
			}

			if (markup) {
				// the markup comes from the library, which escapes the text
				// of the formula; it is parsed as an HTML document, so scripts
				// would not run in any case
				const parsed = new DOMParser().parseFromString(markup, "text/html");

				element = parsed.body.firstElementChild
					? document.importNode(parsed.body.firstElementChild, true)
					: null;

				// full size fractions and matrices (display style)
				element?.setAttribute("display", "block");
			}
		}

		return element;
	}

	proofMark(text) {
		const mark = el(
			"span",
			text.startsWith("✓") ? "proof-ok" : text.startsWith("?") ? "proof-open" : "proof-bad",
		);

		mark.textContent = text;
		return mark;
	}

	// The steps of a line on request (the lines that ask for them show them).
	stepsRows(out, i) {
		const line = this.lines[i];

		if (!line.wants_steps && line.domain !== "proof" && this.stepsOpen.has(i)) {
			const answer = this.meth.steps(i);

			if (answer.found) {
				const steps = el("pre", "steps");

				steps.textContent = answer.rows.join("\n");
				out.append(steps);
			}
		}
	}

	toggleSteps(i) {
		if (this.stepsOpen.has(i)) {
			this.stepsOpen.delete(i);
		} else if (this.meth.steps(i).found) {
			this.stepsOpen.add(i);
		} else {
			this.say("This line has no steps.");
		}

		const row = this.root.querySelector(`.line[data-i="${i}"]`);

		if (row) {
			this.paintResult(row.querySelector(".out"), i);
		}
	}

	// After a change from line `from`: update the results in place when the
	// cells are the same, else draw again.
	refresh(from) {
		if (this.structure() !== this.signature) {
			this.render();
		} else {
			const start = this.lines[Math.min(from, this.count - 1)]?.first ?? 0;

			for (let i = 0; i < this.count; i++) {
				if (this.lines[i].kind === "math" && (i >= start || i === this.typedLine)) {
					this.paintLine(i);
				}
			}
		}

		this.onChange();
	}

	// ---- focus -----------------------------------------------------------

	currentFocus() {
		const active = document.activeElement;
		let focus = null;

		if (active && active.matches && active.matches("textarea") && this.root.contains(active)) {
			const line = active.dataset.i !== undefined ? Number(active.dataset.i) : Number(active.dataset.first);

			focus = { line, start: active.selectionStart, end: active.selectionEnd };
		}

		return focus;
	}

	focusLine(i, start = "end", end = start) {
		const index = Math.min(Math.max(i, 0), this.count - 1);
		const line = this.lines[index];

		if (line && this.collapsed.has(line.first)) {
			this.collapsed.delete(line.first);
			this.render(null);
		}

		let area = null;

		if (line && line.kind === "math") {
			area = this.root.querySelector(`textarea[data-i="${index}"]`);
		} else if (line && line.kind === "note") {
			area = this.root.querySelector(`textarea[data-i="${index}"]`);
		}

		if (area) {
			area.closest(".nline")?.edit?.();
			area.focus();

			const length = area.value.length;
			const at = (v) => (v === "end" ? length : v === "start" ? 0 : Math.min(v, length));

			area.setSelectionRange(at(start), at(end));
		}
	}

	// ---- typing ----------------------------------------------------------

	typed(i, area) {
		const line = this.lines[i];

		this.typedLine = i;

		const from = this.change(EDIT.type, i, line.kind, line.domain ?? "", area.value, { typing: true });

		this.refresh(Math.min(from, i));

		if (line.kind === "math") {
			this.refreshCompletion(i, area);
		}
		this.typedLine = -1;
	}

	history(event) {
		const key = event.key.toLowerCase();
		const mod = event.ctrlKey || event.metaKey;
		let handled = false;

		if (mod && (key === "z" || key === "y")) {
			handled = true;

			if (key === "y" || event.shiftKey) {
				this.redo();
			} else {
				this.undo();
			}
		}

		return handled;
	}

	undo() {
		this.afterHistory(this.meth.edit(EDIT.undo, 0));
	}

	redo() {
		this.afterHistory(this.meth.edit(EDIT.redo, 0));
	}

	afterHistory(answer) {
		const focus = this.currentFocus();

		this.burst = false;
		this.applyAnswer(answer);
		this.render(focus && focus.line < this.count ? focus : null);
		this.onChange();
	}

	keydown(event, i, area) {
		const line = this.lines[i];
		const popup =
			line.kind === "math" && this.completion && this.completion.index === i && this.completion.items.length > 0;
		const atStart = area.selectionStart === 0 && area.selectionEnd === 0;
		const atEnd = area.selectionStart === area.value.length && area.selectionEnd === area.value.length;
		const single = area.scrollHeight <= area.clientHeight + 4;

		if (this.history(event)) {
			event.preventDefault();
		} else if (event.key === "Tab" && !event.shiftKey && popup && this.completion.ghost !== "") {
			event.preventDefault();
			this.accept(i, area, this.completion.selected);
		} else if (event.key === "ArrowDown" && popup) {
			event.preventDefault();
			this.completion.selected = (this.completion.selected + 1) % this.completion.items.length;
			this.completion.chosen = true;
			this.drawCompletion(i, area);
		} else if (event.key === "ArrowUp" && popup) {
			event.preventDefault();

			const n = this.completion.items.length;

			this.completion.selected = (this.completion.selected + n - 1) % n;
			this.completion.chosen = true;
			this.drawCompletion(i, area);
		} else if (event.key === "Enter" && popup && this.completion.chosen) {
			event.preventDefault();
			this.accept(i, area, this.completion.selected);
		} else if (event.key === "Escape") {
			this.hideCompletion();

			if (line.kind === "math") {
				this.paintLine(i);
			} else {
				area.blur();
			}
		} else if (event.key === "Enter" && event.shiftKey) {
			event.preventDefault();
			this.insertCell(line.last + 1, "math", ml_domain(line, this.domain));
		} else if (event.key === "Enter" && !event.ctrlKey && !event.altKey) {
			event.preventDefault();
			this.splitLine(i, area);
		} else if (event.key === "Backspace" && atStart) {
			event.preventDefault();
			this.joinWithPrevious(i);
		} else if (event.key === "Delete" && atEnd && i < line.last) {
			event.preventDefault();
			this.joinWithPrevious(i + 1);
		} else if (event.key === "ArrowUp" && (single || atStart) && !event.shiftKey) {
			event.preventDefault();
			this.moveFocus(i - 1, "end");
		} else if (event.key === "ArrowDown" && (single || atEnd) && !event.shiftKey) {
			event.preventDefault();
			this.moveFocus(i + 1, "start");
		}
	}

	// Moves to the line above or below, skipping the lines that are blank.
	moveFocus(target, where) {
		const step = target < (this.currentFocus()?.line ?? 0) ? -1 : 1;
		let i = target;

		while (i >= 0 && i < this.count && this.lines[i].kind !== "math" && this.lines[i].kind !== "note") {
			i += step;
		}

		if (i >= 0 && i < this.count) {
			this.checkpoint();
			this.focusLine(i, where);
		}
	}

	splitLine(i, area) {
		const line = this.lines[i];
		const before = area.value.slice(0, area.selectionStart);
		const after = area.value.slice(area.selectionEnd);

		this.change(EDIT.replace, i, line.kind, line.domain ?? "", before);

		const from = this.change(EDIT.insert, i + 1, line.kind, line.domain ?? "", after);

		this.render({ line: i + 1, start: 0, end: 0 });
		this.onChange();
		return from;
	}

	// Backspace at the start of a line: the text joins the line above, or an
	// empty first line of a cell goes.
	joinWithPrevious(i) {
		const line = this.lines[i];
		const area = this.root.querySelector(`textarea[data-i="${i}"]`);
		const text = area ? area.value : line.text;

		if (i > line.first) {
			const above = this.lines[i - 1].text;

			this.change(EDIT.replace, i - 1, line.kind, line.domain ?? "", above + text);
			this.change(EDIT.delete, i);
			this.render({ line: i - 1, start: above.length, end: above.length });
			this.onChange();
		} else if (text === "") {
			this.change(EDIT.delete, i);
			this.render(this.count > 0 ? { line: Math.max(i - 1, 0), start: "end", end: "end" } : null);
			this.onChange();
		}
	}

	// ---- cells -----------------------------------------------------------

	// Adds a cell with one empty line at `pos`; a blank line keeps it from
	// joining the cell before or after it.
	insertCell(pos, kind, domain = this.domain) {
		const made = { kind, domain: kind === "math" ? domain : undefined };
		let at = pos;

		this.checkpoint();

		if (merges(this.lines[at - 1], made)) {
			this.change(EDIT.insert, at, "blank", "", "");
			at += 1;
		}

		this.change(EDIT.insert, at, kind, domain, "");

		if (merges(made, this.lines[at + 1])) {
			this.change(EDIT.insert, at + 1, "blank", "", "");
		}

		this.render({ line: at, start: 0, end: 0 });
		this.onChange();
	}

	deleteCell(first, last) {
		this.checkpoint();

		for (let i = last; i >= first; i--) {
			this.change(EDIT.delete, i);
		}

		this.stepsOpen.clear();
		this.render(null);
		this.onChange();
	}

	setDomain(first, last, domain) {
		this.checkpoint();
		this.domain = domain;

		for (let i = first; i <= last; i++) {
			this.change(EDIT.replace, i, "math", domain, this.lines[i].text);
		}

		this.stepsOpen.clear();
		this.render({ line: first, start: "end", end: "end" });
		this.onChange();
	}

	// Puts the lines of a text (a snippet) after line `after` as a new cell.
	insertText(after, fileText) {
		this.checkpoint();

		const parsed = fileText.split("\n").filter((l, k, all) => l !== "" || k < all.length - 1);
		let at = after + 1;

		for (const raw of parsed) {
			const math = /^m:(\w+) (.*)$/.exec(raw);
			const note = /^# ?(.*)$/.exec(raw);

			if (math) {
				this.change(EDIT.insert, at, "math", math[1], math[2]);
			} else if (note) {
				this.change(EDIT.insert, at, "note", "", note[1]);
			} else {
				this.change(EDIT.insert, at, raw === "" ? "blank" : "note", "", raw);
			}

			at += 1;
		}

		this.render(null);
		this.onChange();
	}

	// ---- completion ------------------------------------------------------

	refreshCompletion(i, area) {
		const line = this.lines[i];

		if (!line || line.kind !== "math" || area.selectionStart !== area.selectionEnd) {
			this.hideCompletion();
			return;
		}

		const answer = this.meth.complete(line.domain, i, area.value.slice(0, area.selectionStart));
		const previous = this.completion && this.completion.index === i ? this.completion : null;

		this.completion = answer.items.length
			? {
					index: i,
					items: answer.items,
					selected: previous && previous.chosen ? Math.min(previous.selected, answer.items.length - 1) : 0,
					chosen: Boolean(previous && previous.chosen),
					ghost: area.selectionStart === area.value.length ? answer.ghost : "",
					start: answer.start,
				}
			: null;
		this.paintLine(i);
		this.drawCompletion(i, area);
	}

	measureChar(area) {
		if (!this.charWidth) {
			const probe = el("span");

			probe.style.cssText = "position:absolute;visibility:hidden;white-space:pre;font:inherit";
			probe.textContent = "0".repeat(20);
			area.parentElement.append(probe);
			this.charWidth = probe.getBoundingClientRect().width / 20;
			probe.remove();
		}

		return this.charWidth;
	}

	drawCompletion(i, area) {
		this.root.querySelectorAll(".completion").forEach((e) => e.remove());

		const c = this.completion;

		if (!c || c.index !== i || c.items.length < 2) {
			return;
		}

		const list = el("ul", "completion", { role: "listbox" });
		const left = 8 + this.measureChar(area) * [...area.value.slice(0, c.start)].length;

		c.items.forEach((item, k) => {
			const li = el("li", "", { role: "option", "aria-selected": String(k === c.selected) });

			li.textContent = item;
			li.addEventListener("mousedown", (event) => {
				event.preventDefault();
				this.accept(i, area, k);
			});
			list.append(li);
		});
		list.style.left = `${Math.min(left, area.clientWidth - 160)}px`;
		list.style.top = "100%";
		area.parentElement.append(list);
	}

	// Types the rest of the chosen candidate.
	accept(i, area, k) {
		const c = this.completion;
		const item = c.items[k];
		const cursor = area.selectionStart;
		const text = area.value.slice(0, c.start) + item + area.value.slice(cursor);
		const caret = c.start + item.length;

		area.value = text;
		area.setSelectionRange(caret, caret);
		this.completion = null;
		this.typed(i, area);
	}

	hideCompletion() {
		this.completion = null;
		this.root.querySelectorAll(".completion").forEach((e) => e.remove());
	}

	hideCompletionIfIdle() {
		if (!this.root.contains(document.activeElement)) {
			const index = this.completion?.index;

			this.hideCompletion();

			if (index !== undefined && this.lines[index]?.kind === "math") {
				this.paintLine(index);
			}
		}
	}
}
