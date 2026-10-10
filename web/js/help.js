// The help drawer: a search box, the list of pages, and a page with its blocks
// (headings, paragraphs, lists, examples that can be tried, keys, commands).

import { inline } from "./markdown.js";

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

export class Help {
	constructor(meth, root, { tryExample, event }) {
		this.meth = meth;
		this.event = event ?? (() => {});
		this.root = root;
		this.tryExample = tryExample;
		this.history = [];

		const header = el("header");

		this.back = el("button", "", "←");
		this.back.title = "Back";
		this.back.setAttribute("aria-label", "Back");
		this.back.addEventListener("click", () => this.goBack());
		this.search = el("input");
		this.search.type = "search";
		this.search.placeholder = "Search the help";
		this.search.setAttribute("aria-label", "Search the help");
		this.search.addEventListener("input", () => this.find(this.search.value));

		const close = el("button", "", "✕");

		close.title = "Close (Esc)";
		close.setAttribute("aria-label", "Close the help");
		close.addEventListener("click", () => this.close());
		header.append(this.back, this.search, close);
		this.body = el("div", "body");
		this.root.append(header, this.body);
		this.root.addEventListener("keydown", (event) => {
			if (event.key === "Escape") {
				this.close();
			}
		});
	}

	get isOpen() {
		return !this.root.hidden;
	}

	toggle() {
		if (this.isOpen) {
			this.close();
		} else {
			this.open("index");
		}
	}

	open(id = "index") {
		this.root.hidden = false;
		this.show(id);
		this.event("help", id);
		this.search.focus();
	}

	close() {
		this.root.hidden = true;
		this.onClose?.();
	}

	goBack() {
		this.history.pop();

		const id = this.history.pop() ?? "index";

		this.show(id);
	}

	find(query) {
		if (query.trim() === "") {
			this.show(this.history.at(-1) ?? "index");
			return;
		}

		const hits = this.meth.searchDocs(query.trim());

		this.event("help-search", query.trim());
		const list = el("ul", "hits");

		this.body.replaceChildren();

		if (hits.length === 0) {
			this.body.append(el("p", "", `Nothing found for “${query}”.`));
			return;
		}

		for (const hit of hits) {
			const item = el("li");
			const open = el("button");

			open.append(el("strong", "", hit.title), el("span", "", hit.where));
			open.addEventListener("click", () => {
				this.search.value = "";
				this.show(hit.id);
			});
			item.append(open);
			list.append(item);
		}

		this.body.append(list);
	}

	show(id) {
		let page;

		try {
			page = this.meth.doc(id);
		} catch {
			page = this.meth.doc("index");
		}

		this.history.push(page.id);
		this.back.disabled = this.history.length < 2;
		this.body.replaceChildren();
		this.body.append(el("h1", "", page.title), el("p", "summary", page.summary));

		let list = null;

		for (const block of page.blocks) {
			if (block.kind !== "item") {
				list = null;
			}

			switch (block.kind) {
				case "heading": {
					this.body.append(el(`h${Math.min((block.level ?? 1) + 1, 3)}`, "", block.text));
					break;
				}

				case "paragraph": {
					const p = el("p");

					inline(p, block.text, (target) => this.show(target));
					this.body.append(p);
					break;
				}

				case "item": {
					if (!list) {
						list = el("ul");
						this.body.append(list);
					}

					const li = el("li");

					inline(li, block.text, (target) => this.show(target));
					list.append(li);
					break;
				}

				case "example": {
					this.example(block.text);
					break;
				}

				default: {
					this.body.append(el("pre", "", block.text.replace(/\n$/, "")));
					break;
				}
			}
		}

		if (page.see) {
			const see = el("div", "see", "See also");

			for (const target of page.see.split(",").map((s) => s.trim()).filter(Boolean)) {
				const link = el("button", "", target);

				link.addEventListener("click", () => this.show(target));
				see.append(link);
			}

			this.body.append(see);
		}

		this.body.scrollTop = 0;
	}

	// An example block: lines of input, each followed by `=> result`.
	example(text) {
		const pre = el("pre", "example");
		const inputs = [];

		for (const line of text.replace(/\n$/, "").split("\n")) {
			const row = el("div");

			if (line.startsWith("=>")) {
				row.className = "res";
				row.textContent = `= ${line.slice(2).trim()}`;
			} else {
				row.textContent = line;
				inputs.push(line);
			}

			pre.append(row);
		}

		this.body.append(pre);

		if (this.tryExample && inputs.length > 0) {
			const run = el("button", "try", "Try it in the notebook");

			run.addEventListener("click", () => this.tryExample(inputs));
			this.body.append(run);
		}
	}
}
