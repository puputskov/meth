// The tabs: one for each file that is open. Click to bring one to the front,
// the cross or the middle button to close it, a double click on the name to
// rename the file, drag a tab to put it somewhere else.

import { titleOf } from "./workspace.js";

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

export class Tabs {
	constructor(root, ws, actions) {
		this.root = root;
		this.ws = ws;
		this.actions = actions; // { activate, close, rename, create, say }
		this.renaming = null;
		this.list = el("div", "tab-list", { role: "tablist", "aria-label": "Open files" });

		const add = el("button", "tab-add", { type: "button", title: "New file", "aria-label": "New file" });

		add.textContent = "+";
		add.addEventListener("click", () => this.actions.create());
		this.root.append(this.list, add);

		// the wheel moves along the tabs (there is no scroll bar to grab)
		this.list.addEventListener(
			"wheel",
			(event) => {
				if (event.deltaY !== 0 && event.deltaX === 0) {
					this.list.scrollLeft += event.deltaY;
					event.preventDefault();
				}
			},
			{ passive: false },
		);

		this.list.addEventListener("dragover", (event) => {
			if (event.dataTransfer.types.includes("text/x-meth-tab")) {
				event.preventDefault();
			}
		});
	}

	render() {
		this.list.replaceChildren();

		for (const path of this.ws.tabs) {
			this.list.append(this.tab(path));
		}

		this.list.querySelector(".tab.active")?.scrollIntoView({ block: "nearest", inline: "nearest" });

		const input = this.list.querySelector("input.rename");

		if (input) {
			input.focus();
			input.select();
		}
	}

	tab(path) {
		const active = this.ws.active === path;
		const tab = el("div", "tab", {
			role: "tab",
			"data-path": path,
			"aria-selected": String(active),
			title: path,
			draggable: "true",
		});

		tab.classList.toggle("active", active);

		if (this.renaming === path) {
			tab.append(this.renameInput(path));
		} else {
			const name = el("span", "tab-name");

			name.textContent = titleOf(path);
			name.addEventListener("dblclick", () => this.startRename(path));
			tab.append(name);
		}

		const close = el("button", "tab-close", { type: "button", title: "Close", "aria-label": `Close ${titleOf(path)}` });

		close.textContent = "×";
		close.addEventListener("click", (event) => {
			event.stopPropagation();
			this.actions.close(path);
		});
		tab.append(close);

		tab.addEventListener("click", () => this.actions.activate(path));
		tab.addEventListener("auxclick", (event) => {
			if (event.button === 1) {
				event.preventDefault();
				this.actions.close(path);
			}
		});
		tab.addEventListener("mousedown", (event) => {
			if (event.button === 1) {
				event.preventDefault(); // no scrolling cursor
			}
		});
		tab.addEventListener("dragstart", (event) => {
			event.dataTransfer.setData("text/x-meth-tab", path);
			event.dataTransfer.effectAllowed = "move";
		});
		tab.addEventListener("dragover", (event) => {
			if (event.dataTransfer.types.includes("text/x-meth-tab")) {
				event.preventDefault();
				tab.classList.add("drop");
			}
		});
		tab.addEventListener("dragleave", () => tab.classList.remove("drop"));
		tab.addEventListener("drop", (event) => {
			const from = event.dataTransfer.getData("text/x-meth-tab");

			tab.classList.remove("drop");

			if (from) {
				event.preventDefault();
				this.ws.moveTab(from, this.ws.tabs.indexOf(path));
			}
		});
		return tab;
	}

	startRename(path) {
		this.renaming = path;
		this.render();
	}

	renameInput(path) {
		const input = el("input", "rename", { type: "text", spellcheck: "false", "aria-label": "File name" });
		let done = false;

		input.value = titleOf(path);
		input.defaultValue = input.value;

		const finish = (commit) => {
			if (done) {
				return;
			}

			const name = input.value.trim();

			if (!commit || name === "" || name === input.defaultValue) {
				done = true;
				this.renaming = null;
				this.render();
				return;
			}

			try {
				this.actions.rename(path, name);
				done = true;
				this.renaming = null;
				this.render();
			} catch (error) {
				this.actions.say(error.message);
				input.focus();
			}
		};

		input.addEventListener("keydown", (event) => {
			event.stopPropagation();

			if (event.key === "Enter") {
				event.preventDefault();
				finish(true);
			} else if (event.key === "Escape") {
				event.preventDefault();
				finish(false);
			}
		});
		input.addEventListener("blur", () => finish(true));
		input.addEventListener("click", (event) => event.stopPropagation());
		return input;
	}
}
