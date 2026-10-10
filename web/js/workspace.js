// The files of the user: a tree of folders and .meth files, the tabs that are
// open and the folders that are expanded. Everything is kept in a storage with
// the interface of localStorage (getItem, setItem, removeItem), so a test can
// give it one in memory. A file is a path like "physics/pendulum.meth"; a folder
// is a path without the extension. The text of a file is the text of a .meth
// file, which the library reads and writes (js/meth.js).
//
// Storage keys: "meth.ws" holds the index (the paths, the tabs, the folders
// that are open) and "meth.f:<path>" the text of one file, so typing in a file
// writes that file only.

const INDEX = "meth.ws";
const FILE = "meth.f:";
const EXTENSION = ".meth";
const LIMIT = 80;

export const FIRST_NAME = "untitled";

// A name that a file or a folder may have: not empty, no slash, no control
// character, not . or .., and not too long.
export function validName(name) {
	return (
		typeof name === "string" &&
		name.trim() === name &&
		name.length > 0 &&
		name.length <= LIMIT &&
		!/[\\/\u0000-\u001f]/.test(name) &&
		name !== "." &&
		name !== ".."
	);
}

export const isFilePath = (path) => path.toLowerCase().endsWith(EXTENSION);

export const baseName = (path) => path.slice(path.lastIndexOf("/") + 1);

export const folderOf = (path) =>
	path.includes("/") ? path.slice(0, path.lastIndexOf("/")) : "";

export const joinPath = (folder, name) => (folder ? `${folder}/${name}` : name);

// The name of a file without its extension, for tabs and titles.
export const titleOf = (path) => {
	const name = baseName(path);

	return isFilePath(name) ? name.slice(0, -EXTENSION.length) : name;
};

// A storage that keeps its values in memory only, with the interface of
// localStorage: for when the browser's storage is blocked, and for tests.
export class MemoryStorage {
	constructor() {
		this.map = new Map();
	}

	getItem(key) {
		return this.map.get(key) ?? null;
	}

	setItem(key, value) {
		this.map.set(key, String(value));
	}

	removeItem(key) {
		this.map.delete(key);
	}
}

function compare(a, b) {
	return a.localeCompare(b, undefined, { numeric: true, sensitivity: "base" });
}

export class Workspace {
	constructor(storage) {
		this.storage = storage;
		this.files = new Map(); // path -> text
		this.folders = new Set(); // paths of folders (also the empty ones)
		this.tabs = []; // paths of the files that are open
		this.active = null; // the path of the tab in front
		this.expanded = new Set(); // folders shown open in the tree
		this.sidebar = { open: true, width: 260 };
		this.meta = new Map(); // path -> what the page remembers of a file
		this.persistent = true; // false when the storage refused a write
		this.listeners = [];
	}

	// ---- storage -----------------------------------------------------------

	read(key) {
		let value = null;

		try {
			value = this.storage.getItem(key);
		} catch {
			// the storage is not available
		}

		return value;
	}

	write(key, value) {
		try {
			if (value === null) {
				this.storage.removeItem(key);
			} else {
				this.storage.setItem(key, value);
			}
		} catch {
			// full, blocked or a private window: the work stays in memory
			this.persistent = false;
		}
	}

	// Reads the index and the files; the files of an older version (one
	// notebook under "meth.session") become one file. `starter` is the text
	// of the file made when there is nothing at all.
	static load(storage, starter = { name: "Welcome.meth", text: "" }) {
		const ws = new Workspace(storage);
		let index = null;

		try {
			index = JSON.parse(ws.read(INDEX));
		} catch {
			index = null;
		}

		if (index && Array.isArray(index.files)) {
			for (const path of index.files) {
				ws.files.set(path, ws.read(FILE + path) ?? "");
			}

			(index.folders ?? []).forEach((f) => ws.folders.add(f));
			ws.tabs = (index.tabs ?? []).filter((p) => ws.files.has(p));
			ws.active = ws.files.has(index.active) ? index.active : (ws.tabs[0] ?? null);
			(index.expanded ?? []).forEach((f) => ws.expanded.add(f));
			ws.sidebar = { ...ws.sidebar, ...(index.sidebar ?? {}) };
			Object.entries(index.meta ?? {})
				.filter(([path]) => ws.files.has(path))
				.forEach(([path, view]) => ws.meta.set(path, view));
		} else {
			const old = ws.read("meth.session");

			if (old !== null) {
				const name = ws.read("meth.name") ?? FIRST_NAME;

				ws.addFile(validName(name) ? name : FIRST_NAME, old);
			} else {
				ws.addFile(starter.name, starter.text);
			}
		}

		if (ws.files.size > 0 && ws.tabs.length === 0) {
			ws.openTab([...ws.files.keys()].sort(compare)[0]);
		}

		ws.saveIndex();
		return ws;
	}

	saveIndex() {
		this.write(
			INDEX,
			JSON.stringify({
				v: 1,
				files: [...this.files.keys()],
				folders: [...this.folders],
				tabs: this.tabs,
				active: this.active,
				expanded: [...this.expanded],
				sidebar: this.sidebar,
				meta: Object.fromEntries(this.meta),
			}),
		);
	}

	on(listener) {
		this.listeners.push(listener);
	}

	changed(what) {
		this.saveIndex();
		this.listeners.forEach((l) => l(what));
	}

	// ---- looking ---------------------------------------------------------

	has(path) {
		return this.files.has(path) || this.folders.has(path);
	}

	isFolder(path) {
		return this.folders.has(path);
	}

	// A path that is free in its folder: the same, or with a number.
	free(folder, name) {
		const taken = (n) =>
			[...this.files.keys(), ...this.folders].some(
				(p) => p.toLowerCase() === joinPath(folder, n).toLowerCase(),
			);
		const dot = name.toLowerCase().endsWith(EXTENSION) ? name.length - EXTENSION.length : name.length;
		const stem = name.slice(0, dot);
		const extension = name.slice(dot);
		let candidate = name;

		for (let n = 2; taken(candidate); n++) {
			candidate = `${stem} ${n}${extension}`;
		}

		return joinPath(folder, candidate);
	}

	// The tree: [{ name, path, folder: true, children }, { name, path }] with
	// the folders first, each group by name.
	tree(folder = "") {
		const names = new Map();

		for (const path of [...this.folders, ...this.files.keys()]) {
			if (folderOf(path) === folder) {
				names.set(path, this.folders.has(path));
			}
		}

		return [...names]
			.sort(([a, af], [b, bf]) => (af === bf ? compare(baseName(a), baseName(b)) : af ? -1 : 1))
			.map(([path, isFolder]) =>
				isFolder
					? { name: baseName(path), path, folder: true, children: this.tree(path) }
					: { name: baseName(path), path, folder: false },
			);
	}

	text(path) {
		return this.files.get(path) ?? "";
	}

	// ---- files -----------------------------------------------------------

	// Makes a file without opening it; the extension is added when missing.
	addFile(name, text = "", folder = "") {
		const base = isFilePath(name) ? name : name + EXTENSION;
		const path = this.free(folder, base);

		this.files.set(path, text);
		this.write(FILE + path, text);
		this.ensureFolders(folder);
		return path;
	}

	ensureFolders(folder) {
		for (let at = folder; at; at = folderOf(at)) {
			this.folders.add(at);
		}
	}

	createFile(folder, name, text = "") {
		if (!validName(name)) {
			throw new Error("That is not a name a file can have.");
		}

		const path = this.addFile(name, text, folder);

		this.expanded.add(folder);
		this.openTab(path);
		this.changed({ type: "create", path });
		return path;
	}

	createFolder(folder, name) {
		if (!validName(name)) {
			throw new Error("That is not a name a folder can have.");
		}

		const path = this.free(folder, name);

		this.folders.add(path);
		this.ensureFolders(folder);
		this.expanded.add(folder);
		this.expanded.add(path);
		this.changed({ type: "create", path });
		return path;
	}

	// Called as the text of a file changes: written on its own.
	setText(path, text) {
		if (this.files.has(path) && this.files.get(path) !== text) {
			this.files.set(path, text);
			this.write(FILE + path, text);
		}
	}

	// What the page remembers of a file besides its text (the cells that are
	// folded, the results that are shown): kept in the index.
	view(path) {
		return this.meta.get(path) ?? {};
	}

	setView(path, view) {
		if (this.files.has(path) && JSON.stringify(this.view(path)) !== JSON.stringify(view)) {
			this.meta.set(path, view);
			this.saveIndex();
		}
	}

	// Gives a file or a folder (with all it holds) another name in its folder;
	// returns the new path. The name of a file may leave out the extension.
	rename(path, name) {
		if (!validName(name)) {
			throw new Error("That is not a name that can be used.");
		}

		const isFolder = this.isFolder(path);
		const fixed = !isFolder && !isFilePath(name) ? name + EXTENSION : name;
		const target = joinPath(folderOf(path), fixed);

		if (target === path) {
			return path;
		}

		const clash = [...this.files.keys(), ...this.folders].some(
			(p) => p !== path && p.toLowerCase() === target.toLowerCase(),
		);

		if (clash) {
			throw new Error(`“${fixed}” is already there.`);
		}

		this.moveTo(path, target);
		return target;
	}

	// Puts a file or a folder in another folder ("" is the top).
	move(path, folder) {
		const target = joinPath(folder, baseName(path));

		if (folder === path || folder.startsWith(`${path}/`)) {
			throw new Error("A folder cannot go into itself.");
		}

		if (target === path) {
			return path;
		}

		const clash = [...this.files.keys(), ...this.folders].some(
			(p) => p.toLowerCase() === target.toLowerCase(),
		);

		if (clash) {
			throw new Error(`“${baseName(path)}” is already in that folder.`);
		}

		this.ensureFolders(folder);
		this.expanded.add(folder);
		this.moveTo(path, target);
		return target;
	}

	// Changes the path of a file, or of a folder and everything under it, in
	// the files, the folders, the tabs, the open folders and the storage.
	moveTo(from, to) {
		const swap = (p) => (p === from ? to : p.startsWith(`${from}/`) ? to + p.slice(from.length) : p);

		for (const [path, text] of [...this.files]) {
			if (swap(path) !== path) {
				this.files.delete(path);
				this.write(FILE + path, null);
				this.files.set(swap(path), text);
				this.write(FILE + swap(path), text);
			}
		}

		this.meta = new Map([...this.meta].map(([path, view]) => [swap(path), view]));
		this.folders = new Set([...this.folders].map(swap));
		this.expanded = new Set([...this.expanded].map(swap));
		this.tabs = this.tabs.map(swap);
		this.active = this.active === null ? null : swap(this.active);
		this.changed({ type: "move", from, to });
	}

	// Removes a file, or a folder and everything in it.
	remove(path) {
		for (const file of [...this.files.keys()]) {
			if (file === path || file.startsWith(`${path}/`)) {
				this.files.delete(file);
				this.meta.delete(file);
				this.write(FILE + file, null);
				this.closeTab(file, false);
			}
		}

		this.folders = new Set([...this.folders].filter((f) => f !== path && !f.startsWith(`${path}/`)));
		this.expanded = new Set([...this.expanded].filter((f) => f !== path && !f.startsWith(`${path}/`)));
		this.changed({ type: "remove", path });
	}

	// A copy next to the original; returns its path.
	duplicate(path) {
		const copy = this.addFile(
			baseName(path).replace(/\.meth$/i, "") + " copy",
			this.text(path),
			folderOf(path),
		);

		this.openTab(copy);
		this.changed({ type: "create", path: copy });
		return copy;
	}

	// ---- tabs and the tree -----------------------------------------------

	openTab(path) {
		if (this.files.has(path)) {
			if (!this.tabs.includes(path)) {
				this.tabs.push(path);
			}

			this.active = path;
		}
	}

	open(path) {
		this.openTab(path);
		this.changed({ type: "open", path });
	}

	// Closes a tab; the one next to it comes to the front.
	closeTab(path, notify = true) {
		const at = this.tabs.indexOf(path);

		if (at >= 0) {
			this.tabs.splice(at, 1);

			if (this.active === path) {
				this.active = this.tabs[Math.min(at, this.tabs.length - 1)] ?? null;
			}
		}

		if (notify) {
			this.changed({ type: "close", path });
		}
	}

	activate(path) {
		if (this.tabs.includes(path) && this.active !== path) {
			this.active = path;
			this.changed({ type: "activate", path });
		}
	}

	moveTab(path, to) {
		const at = this.tabs.indexOf(path);

		if (at >= 0) {
			this.tabs.splice(at, 1);
			this.tabs.splice(Math.min(Math.max(to, 0), this.tabs.length), 0, path);
			this.changed({ type: "tabs" });
		}
	}

	toggleFolder(path) {
		if (this.expanded.has(path)) {
			this.expanded.delete(path);
		} else {
			this.expanded.add(path);
		}

		this.changed({ type: "expand", path });
	}

	setSidebar(patch) {
		this.sidebar = { ...this.sidebar, ...patch };
		this.changed({ type: "sidebar" });
	}
}
