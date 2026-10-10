// The application: the workspace (the files), the explorer and the tabs that
// show it, and one notebook for each file that is open. Each notebook has a
// session of the library of its own, so the files are independent: names,
// undo and results do not cross from one to the other. The text of a file is
// written back to the workspace shortly after it changes.

import { Explorer } from "./explorer.js";
import { Notebook } from "./notebook.js";
import { Tabs } from "./tabs.js";
import { baseName, titleOf } from "./workspace.js";

const SAVE_MS = 300;

export class App {
	constructor({ meth, ws, explorer, tabs, panes, say, confirm, download, onActive, empty, requestSymbols }) {
		this.meth = meth;
		this.ws = ws;
		this.panes = panes;
		this.say = say;
		this.confirm = confirm;
		this.download = download;
		this.onActive = onActive ?? (() => {});
		this.domains = meth.domains();
		this.docs = new Map(); // path -> { session, notebook, pane, timer }
		this.symbols = false;
		this.empty = empty;
		this.requestSymbols = requestSymbols;
		this.listeners = []; // told what the user does in a notebook

		this.explorer = new Explorer(explorer, ws, {
			open: (path) => this.ws.open(path),
			createFile: (folder) => this.createFile(folder),
			createFolder: (folder) => this.createFolder(folder),
			rename: (path, name) => this.ws.rename(path, name),
			remove: (path) => this.remove(path),
			duplicate: (path) => this.ws.duplicate(path),
			download: (path) => this.downloadFile(path),
			move: (path, folder) => this.move(path, folder),
			say,
		});
		this.tabs = new Tabs(tabs, ws, {
			activate: (path) => this.ws.activate(path),
			close: (path) => this.ws.closeTab(path),
			rename: (path, name) => this.ws.rename(path, name),
			create: () => this.createFile(this.explorer.targetFolder()),
			say,
		});
		ws.on((event) => this.sync(event));
		this.sync({ type: "start" });
	}

	// Something the user did in a notebook or in the page ("steps", "fold",
	// "domain", "symbols", "help" ...): the tutorial watches these.
	onEvent(listener) {
		this.listeners.push(listener);
	}

	emit(type, detail) {
		this.listeners.forEach((l) => l(type, detail));
	}

	// ---- the files --------------------------------------------------------

	createFile(folder = "") {
		const path = this.ws.createFile(folder, "untitled");

		this.explorer.startRename(path);
		return path;
	}

	createFolder(folder = "") {
		const path = this.ws.createFolder(folder, "folder");

		this.explorer.startRename(path);
		return path;
	}

	// Files from the disk of the user (their texts): each becomes a file of the
	// workspace, in `folder`.
	importFiles(files, folder = "") {
		for (const { name, text } of files) {
			this.ws.createFile(folder, name.replace(/[\\/]/g, "-"), text);
		}
	}

	remove(path) {
		const folder = this.ws.isFolder(path);
		const what = folder ? "the folder and everything in it" : "the file";

		if (this.confirm(`Delete “${baseName(path)}”? This deletes ${what}.`)) {
			this.ws.remove(path);
		}
	}

	move(path, folder) {
		try {
			this.ws.move(path, folder);
		} catch (error) {
			this.say(error.message);
		}
	}

	downloadFile(path) {
		this.flush();
		this.download(baseName(path), this.ws.text(path), "text/plain");
	}

	// ---- what is open -----------------------------------------------------

	get activePath() {
		return this.ws.active;
	}

	get active() {
		return this.docs.get(this.ws.active) ?? null;
	}

	// Something about the workspace changed: bring the notebooks, the tabs and
	// the explorer in line with it.
	sync(event) {
		if (event.type === "move") {
			const swap = (p) => (p === event.from ? event.to : p.startsWith(`${event.from}/`) ? event.to + p.slice(event.from.length) : p);

			for (const [path, doc] of [...this.docs]) {
				if (swap(path) !== path) {
					this.docs.delete(path);
					this.docs.set(swap(path), doc);
					doc.pane.dataset.path = swap(path);
					doc.path = swap(path); // what its saving writes to
				}
			}
		}

		// a file that was closed or deleted gives its session back
		for (const [path, doc] of [...this.docs]) {
			if (!this.ws.files.has(path) || !this.ws.tabs.includes(path)) {
				if (this.ws.files.has(path)) {
					this.save(path);
				}

				this.destroy(path, doc);
			}
		}

		if (this.ws.active !== null && !this.docs.has(this.ws.active)) {
			this.openDocument(this.ws.active);
		}

		this.explorer.render();
		this.tabs.render();

		for (const [path, doc] of this.docs) {
			doc.pane.hidden = path !== this.ws.active;
		}

		this.showEmpty(this.ws.active === null);
		this.onActive(this.ws.active);
	}

	showEmpty(show) {
		let box = this.panes.querySelector(".empty-state");

		if (show && !box) {
			box = document.createElement("div");
			box.className = "empty-state";
			this.empty(box);
			this.panes.append(box);
		} else if (!show && box) {
			box.remove();
		}
	}

	// A notebook for the file, with a session of its own.
	openDocument(path) {
		let session;

		try {
			session = this.meth.create();
		} catch {
			this.say("Too many files are open: close some of them.");
			this.ws.closeTab(path);
			return;
		}

		const pane = document.createElement("div");
		const main = document.createElement("main");
		const doc = { session, pane, timer: 0, notebook: null, path };

		pane.className = "pane";
		pane.dataset.path = path;
		main.className = "notebook";
		main.setAttribute("aria-label", titleOf(path));
		pane.append(main);
		this.panes.append(pane);

		doc.notebook = new Notebook(session, main, {
			domains: this.domains,
			say: this.say,
			enableSymbols: this.requestSymbols,
			event: (type, detail) => this.emit(type, detail),
			onChange: () => {
				clearTimeout(doc.timer);
				doc.timer = setTimeout(() => this.save(doc.path), SAVE_MS);
				this.emit("change");
			},
		});
		this.docs.set(path, doc);
		session.setSymbols(this.symbols);
		doc.notebook.load(this.ws.text(path), this.ws.view(path));
		doc.path = path;
	}

	destroy(path, doc) {
		clearTimeout(doc.timer);
		doc.pane.remove();
		doc.session.destroy();
		this.docs.delete(path);
	}

	// Writes the text and the view of a file to the workspace.
	save(path) {
		const doc = this.docs.get(path);

		if (doc) {
			clearTimeout(doc.timer);
			this.ws.setText(path, doc.session.save());
			this.ws.setView(path, doc.notebook.viewState());
		}
	}

	// Writes every file that is open (before leaving the page, or saving).
	flush() {
		for (const path of this.docs.keys()) {
			this.save(path);
		}
	}

	setSymbols(on) {
		this.symbols = on;

		for (const doc of this.docs.values()) {
			doc.notebook.applyAnswer(doc.session.setSymbols(on));
			doc.notebook.render();
		}
	}
}
