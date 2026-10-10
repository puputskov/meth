// The explorer, the side bar with the files and folders of the user, in the
// manner of an editor: click a file to open it, a folder to open or close it,
// F2 or the menu of the right button to rename, drag a file onto a folder to
// move it. It draws what the workspace (js/workspace.js) holds and asks the
// application (the `actions`) to change it.

import { ContextMenu } from "./menu.js";
import { isFilePath, titleOf } from "./workspace.js";

const ICONS = {
	file: '<svg viewBox="0 0 16 16" width="16" height="16" aria-hidden="true"><path d="M4 1.5h5l3.5 3.5v9.5H4z M9 1.5V5h3.5 M8 8v4 M6 10h4" fill="none" stroke="currentColor" stroke-width="1.2" stroke-linejoin="round"/></svg>',
	folder: '<svg viewBox="0 0 16 16" width="16" height="16" aria-hidden="true"><path d="M1.5 3.5h4.5l1.5 1.5h7v8.5h-13z M8 7.5v4 M6 9.5h4" fill="none" stroke="currentColor" stroke-width="1.2" stroke-linejoin="round"/></svg>',
	collapse: '<svg viewBox="0 0 16 16" width="16" height="16" aria-hidden="true"><path d="M3 5.5l5 5 5-5 M3 2h10" fill="none" stroke="currentColor" stroke-width="1.2" stroke-linejoin="round" stroke-linecap="round"/></svg>',
};

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

export class Explorer {
	constructor(root, ws, actions) {
		this.root = root;
		this.ws = ws;
		this.actions = actions;
		this.selected = "";
		this.renaming = null; // { path }
		this.menu = new ContextMenu();

		const header = el("header", "explorer-head");
		const title = el("span", "explorer-title");

		title.textContent = "Files";
		header.append(
			title,
			this.iconButton("file", "New file", () => this.actions.createFile(this.targetFolder())),
			this.iconButton("folder", "New folder", () => this.actions.createFolder(this.targetFolder())),
			this.iconButton("collapse", "Close all folders", () => this.collapseAll()),
		);

		this.tree = el("div", "tree", { role: "tree", tabindex: "0", "aria-label": "Files" });
		this.root.append(header, this.tree);

		this.tree.addEventListener("keydown", (event) => this.keydown(event));
		this.tree.addEventListener("contextmenu", (event) => {
			if (event.target === this.tree || event.target.classList.contains("empty")) {
				event.preventDefault();
				this.showMenu(event.clientX, event.clientY, "");
			}
		});
		this.tree.addEventListener("dragover", (event) => this.dragOver(event, ""));
		this.tree.addEventListener("dragleave", (event) => {
			if (event.target === this.tree) {
				this.tree.classList.remove("drop");
			}
		});
		this.tree.addEventListener("drop", (event) => this.drop(event, ""));
		this.tree.addEventListener("click", (event) => {
			if (event.target === this.tree) {
				this.selected = "";
				this.render();
			}
		});
	}

	iconButton(icon, label, handler) {
		const b = el("button", "icon", { type: "button", title: label, "aria-label": label });

		b.innerHTML = ICONS[icon]; // a constant of this file
		b.addEventListener("click", handler);
		return b;
	}

	// The folder a new file goes in: the selected folder, or the folder of the
	// selected file.
	targetFolder() {
		let folder = "";

		if (this.selected) {
			folder = this.ws.isFolder(this.selected)
				? this.selected
				: this.selected.includes("/")
					? this.selected.slice(0, this.selected.lastIndexOf("/"))
					: "";
		}

		return folder;
	}

	collapseAll() {
		this.ws.expanded.clear();
		this.ws.changed({ type: "expand" });
	}

	// ---- drawing -----------------------------------------------------------

	render() {
		const scroll = this.tree.scrollTop;
		const top = this.ws.tree();

		this.tree.replaceChildren();

		for (const item of top) {
			this.add(item, 0);
		}

		if (top.length === 0) {
			const empty = el("div", "empty");

			empty.textContent = "No files yet. Make one with the button above.";
			this.tree.append(empty);
		}

		this.tree.scrollTop = scroll;

		const input = this.tree.querySelector("input.rename");

		if (input) {
			input.focus();
			input.select();
		}
	}

	add(item, depth) {
		const row = el("div", "node", {
			role: "treeitem",
			"data-path": item.path,
			"aria-level": depth + 1,
			draggable: this.renaming && this.renaming.path === item.path ? "false" : "true",
		});
		const open = this.ws.expanded.has(item.path);

		row.style.paddingLeft = `${0.4 + depth * 0.95}rem`;
		row.classList.toggle("folder", item.folder);
		row.classList.toggle("file", !item.folder);
		row.classList.toggle("selected", this.selected === item.path);
		row.classList.toggle("active", !item.folder && this.ws.active === item.path);

		if (item.folder) {
			row.setAttribute("aria-expanded", String(open));
		}

		const twisty = el("span", "twisty");

		twisty.textContent = item.folder ? (open ? "▾" : "▸") : "";
		row.append(twisty);

		if (this.renaming && this.renaming.path === item.path) {
			row.append(this.renameInput(item));
		} else {
			const name = el("span", "name");

			name.textContent = item.folder ? item.name : titleOf(item.path);
			row.title = item.path;
			row.append(name);
		}

		row.addEventListener("click", () => this.click(item));
		row.addEventListener("contextmenu", (event) => {
			event.preventDefault();
			event.stopPropagation();
			this.showMenu(event.clientX, event.clientY, item.path);
		});
		row.addEventListener("dragstart", (event) => {
			event.dataTransfer.setData("text/x-meth-path", item.path);
			event.dataTransfer.effectAllowed = "move";
		});
		row.addEventListener("dragover", (event) => this.dragOver(event, item.folder ? item.path : this.parentOf(item.path), row));
		row.addEventListener("dragleave", () => row.classList.remove("drop"));
		row.addEventListener("drop", (event) => this.drop(event, item.folder ? item.path : this.parentOf(item.path)));
		this.tree.append(row);

		if (item.folder && open) {
			for (const child of item.children) {
				this.add(child, depth + 1);
			}
		}
	}

	parentOf(path) {
		return path.includes("/") ? path.slice(0, path.lastIndexOf("/")) : "";
	}

	click(item) {
		this.selected = item.path;

		if (item.folder) {
			this.ws.toggleFolder(item.path);
		} else {
			this.actions.open(item.path);
		}

		this.tree.focus({ preventScroll: true });
	}

	// ---- renaming ------------------------------------------------------------

	startRename(path) {
		this.selected = path;
		this.renaming = { path };
		this.render();
	}

	renameInput(item) {
		const input = el("input", "rename", {
			type: "text",
			spellcheck: "false",
			"aria-label": `Rename ${item.name}`,
		});
		let done = false;

		input.value = item.folder || !isFilePath(item.name) ? item.name : titleOf(item.path);

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
				const path = this.actions.rename(item.path, name);

				done = true;
				this.renaming = null;
				this.selected = path;
				this.render();
			} catch (error) {
				this.actions.say(error.message);
				input.focus();
			}
		};

		input.defaultValue = input.value;
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

	// ---- the menu of the right button ----------------------------------------

	showMenu(x, y, path) {
		const isFolder = path !== "" && this.ws.isFolder(path);
		const folder = path === "" ? "" : isFolder ? path : this.parentOf(path);
		const items = [
			{ label: "New file", action: () => this.actions.createFile(folder) },
			{ label: "New folder", action: () => this.actions.createFolder(folder) },
		];

		this.selected = path;
		this.render();

		if (path !== "") {
			items.push(
				"-",
				{ label: "Rename", action: () => this.startRename(path) },
				...(isFolder ? [] : [{ label: "Duplicate", action: () => this.actions.duplicate(path) }]),
				...(isFolder ? [] : [{ label: "Download", action: () => this.actions.download(path) }]),
				"-",
				{ label: "Delete", danger: true, action: () => this.actions.remove(path) },
			);
		}

		this.menu.show(x, y, items);
	}

	// ---- the keyboard ---------------------------------------------------------

	visible() {
		return [...this.tree.querySelectorAll(".node")].map((n) => n.dataset.path);
	}

	keydown(event) {
		const paths = this.visible();
		const at = paths.indexOf(this.selected);
		const path = this.selected;
		const folder = path !== "" && this.ws.isFolder(path);
		let handled = true;

		if (event.key === "ArrowDown") {
			this.select(paths[Math.min(at + 1, paths.length - 1)]);
		} else if (event.key === "ArrowUp") {
			this.select(paths[Math.max(at - 1, 0)]);
		} else if (event.key === "Home") {
			this.select(paths[0]);
		} else if (event.key === "End") {
			this.select(paths.at(-1));
		} else if (event.key === "ArrowRight" && folder) {
			if (!this.ws.expanded.has(path)) {
				this.ws.toggleFolder(path);
			} else {
				this.select(paths[at + 1]);
			}
		} else if (event.key === "ArrowLeft" && path !== "") {
			if (folder && this.ws.expanded.has(path)) {
				this.ws.toggleFolder(path);
			} else if (this.parentOf(path)) {
				this.select(this.parentOf(path));
			}
		} else if (event.key === "Enter" && path !== "") {
			if (folder) {
				this.ws.toggleFolder(path);
			} else {
				this.actions.open(path);
			}
		} else if (event.key === "F2" && path !== "") {
			this.startRename(path);
		} else if (event.key === "Delete" && path !== "") {
			this.actions.remove(path);
		} else {
			handled = false;
		}

		if (handled) {
			event.preventDefault();
		}
	}

	select(path) {
		if (path !== undefined) {
			this.selected = path;
			this.render();
			this.tree.querySelector(".node.selected")?.scrollIntoView({ block: "nearest" });
		}
	}

	// ---- drag and drop ---------------------------------------------------------

	dragOver(event, folder, row) {
		if (event.dataTransfer.types.includes("text/x-meth-path")) {
			event.preventDefault();
			event.stopPropagation();
			event.dataTransfer.dropEffect = "move";
			this.tree.querySelectorAll(".drop").forEach((e) => e.classList.remove("drop"));
			(row ?? this.tree).classList.add("drop");
		}
	}

	drop(event, folder) {
		const path = event.dataTransfer.getData("text/x-meth-path");

		this.tree.querySelectorAll(".drop").forEach((e) => e.classList.remove("drop"));
		this.tree.classList.remove("drop");

		if (path) {
			event.preventDefault();
			event.stopPropagation();
			this.actions.move(path, folder);
		}
	}
}

