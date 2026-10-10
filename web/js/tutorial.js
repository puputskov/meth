// The tutorial on the page: lessons of the library (the ones whose steps are
// checked by results) played in a scratch file of the workspace. It only
// watches the notebook, as the terminal tutorial watches the editor: a step
// is done when a line written since the step began has the result it asks for.

const RESULT = 3;
const ERROR = 4;
const VALUE = 5;
const HINT_AFTER = 5; // changes that did not do it
const KEY = "meth.tutorial";

function el(tag, className, text) {
	const e = document.createElement(tag);

	if (className) {
		e.className = className;
	}

	if (text !== undefined) {
		e.textContent = text;
	}

	return e;
}

export class Tutorial {
	constructor(meth, root, { app, ws, say, store }) {
		this.meth = meth;
		this.root = root;
		this.app = app;
		this.ws = ws;
		this.say = say;
		this.store = store ?? null;
		this.lessons = meth
			.lessons()
			.filter((l) => l.stage === 2 && l.steps.every((s) => [RESULT, ERROR, VALUE].includes(s.check)));
		this.path = null;
		this.lesson = 0;
		this.step = 0;
		this.start = 0;
		this.misses = 0;
		this.done = false; // the lesson is finished, waiting for "Next"
		app.onEvent((type) => type === "change" && this.watch());
	}

	get active() {
		return this.path !== null;
	}

	// ---- progress ---------------------------------------------------------

	finished() {
		let id = "";

		try {
			id = this.store?.getItem(KEY) ?? "";
		} catch {
			id = "";
		}

		return id;
	}

	remember(id) {
		try {
			this.store?.setItem(KEY, id);
		} catch {
			// a blocked storage only loses the place
		}
	}

	// ---- starting and leaving ----------------------------------------------

	// Starts after the last lesson that was finished, or at lesson `from`.
	begin(from = null) {
		if (this.lessons.length === 0) {
			this.say("This build has no lessons.");
			return;
		}

		if (!this.active) {
			this.path = this.ws.createFile("", "Tutorial", "");
			this.ws.open(this.path);
		}

		const last = this.lessons.findIndex((l) => l.id === this.finished());

		this.enter(from ?? (last + 1 >= this.lessons.length ? 0 : last + 1));
	}

	enter(lesson) {
		if (lesson >= this.lessons.length) {
			this.lesson = this.lessons.length;
			this.done = true;
		} else {
			this.lesson = lesson;
			this.step = 0;
			this.done = false;
			this.misses = 0;
			this.start = this.mark();
		}

		this.render();
	}

	end() {
		const path = this.path;

		this.path = null;
		this.root.hidden = true;

		if (path !== null && this.ws.files.has(path) && this.ws.text(path).trim() === "") {
			this.ws.remove(path);
		}
	}

	// ---- watching -----------------------------------------------------------

	get doc() {
		return this.path === null ? null : (this.app.docs.get(this.path) ?? null);
	}

	// The line after the last one that has something written in it.
	mark() {
		const lines = this.doc?.notebook.lines ?? [];
		let at = 0;

		lines.forEach((line, i) => {
			if (line.text.trim() !== "") {
				at = i + 1;
			}
		});

		return at;
	}

	// The index of the line that does the step, or -1.
	satisfied(step, doc) {
		const lines = doc.notebook.lines;
		let found = -1;

		for (let i = this.start; i < lines.length && found < 0; i++) {
			const line = lines[i];
			const res = line.result;

			if (line.kind === "math" && res) {
				if (step.check === RESULT && res.kind === "value" && res.text === step.param) {
					found = i;
				} else if (step.check === ERROR && res.kind === "error" && (step.param === "" || res.code === step.param)) {
					found = i;
				}
			}
		}

		if (step.check === VALUE && lines.length > this.start) {
			try {
				const g = doc.session.ghost("algebra", lines.length, step.param);

				found = g.shown && g.text === step.param2 ? lines.length - 1 : -1;
			} catch {
				found = -1;
			}
		}

		return found;
	}

	watch() {
		const doc = this.doc;

		if (!doc || this.done || this.lesson >= this.lessons.length) {
			return;
		}

		const lesson = this.lessons[this.lesson];
		const found = this.satisfied(lesson.steps[this.step], doc);

		if (found >= 0) {
			this.advance(found + 1);
		} else {
			this.misses += 1;

			if (this.misses === HINT_AFTER) {
				this.render();
			}
		}
	}

	advance(start) {
		const lesson = this.lessons[this.lesson];

		this.misses = 0;
		this.start = start;

		if (this.step + 1 < lesson.steps.length) {
			this.step += 1;
		} else {
			this.done = true;
			this.remember(lesson.id);
		}

		this.render();
	}

	skip() {
		if (!this.done) {
			this.advance(this.mark());
		}
	}

	back() {
		if (this.done) {
			this.done = false;
		} else if (this.step > 0) {
			this.step -= 1;
		} else if (this.lesson > 0) {
			this.lesson -= 1;
			this.step = this.lessons[this.lesson].steps.length - 1;
		}

		this.misses = 0;
		this.start = this.mark();
		this.render();
	}

	// ---- the panel ----------------------------------------------------------

	button(label, action) {
		const b = el("button", "", label);

		b.addEventListener("click", action);
		return b;
	}

	render() {
		this.root.replaceChildren();
		this.root.hidden = !this.active;

		if (!this.active) {
			return;
		}

		const head = el("div", "head");
		const body = el("div", "body");
		const keys = el("div", "keys");
		const total = this.lessons.length;

		if (this.lesson >= total) {
			head.append(el("strong", "", "Tutorial finished"));
			body.append(el("p", "", "You have been through every lesson. The help (F1) has the rest."));
			keys.append(this.button("Close", () => this.end()));
		} else {
			const lesson = this.lessons[this.lesson];
			const step = lesson.steps[this.step];
			const dots = el("span", "dots");

			head.append(el("strong", "", lesson.title), el("span", "count", `Lesson ${this.lesson + 1} of ${total}`));
			lesson.steps.forEach((_, i) => {
				dots.append(el("i", i < this.step || this.done ? "on" : i === this.step ? "now" : ""));
			});
			head.append(dots);

			if (this.done) {
				body.append(el("p", "ok", "✓ Lesson complete."));
				keys.append(
					this.button("← Back", () => this.back()),
					this.button(this.lesson + 1 < total ? "Next lesson →" : "Finish", () => this.enter(this.lesson + 1)),
				);
			} else {
				body.append(el("p", "task", step.web || step.text));

				if (this.misses >= HINT_AFTER && step.hint) {
					body.append(el("p", "hint", `Hint: ${step.hint}`));
				}

				keys.append(this.button("← Back", () => this.back()), this.button("Skip →", () => this.skip()));
			}

			keys.append(this.button("Leave", () => this.end()));
		}

		this.root.append(head, body, keys);
	}
}
