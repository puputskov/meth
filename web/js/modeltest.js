// The workspace model (js/workspace.js) without the page: files, folders, names,
// tabs and what is kept in the storage. The outcome is put in the page as JSON:
// [{ name, ok, detail }]. tools/test_web.py reads it.

import { Workspace, baseName, folderOf, isFilePath, titleOf, validName } from "./workspace.js";

const status = document.getElementById("status");
const results = document.getElementById("results");
const outcome = [];

function check(name, ok, detail = "") {
	outcome.push({ name, ok: Boolean(ok), detail: String(detail) });
}

// A storage in memory with the interface of localStorage; it counts the
// writes and can be made to refuse them.
class MemoryStorage {
	constructor() {
		this.map = new Map();
		this.writes = [];
		this.full = false;
	}

	getItem(key) {
		return this.map.get(key) ?? null;
	}

	setItem(key, value) {
		if (this.full) {
			throw new Error("QuotaExceededError");
		}

		this.writes.push(key);
		this.map.set(key, String(value));
	}

	removeItem(key) {
		this.map.delete(key);
	}
}

function throws(fn) {
	let error = null;

	try {
		fn();
	} catch (e) {
		error = e;
	}

	return error;
}

try {
	// names
	check("names: valid", validName("physics") && validName("my file") && validName("a.b"));
	check("names: invalid", ["", " x", "x ", "a/b", "a\\b", ".", "..", "x".repeat(81), "a\nb"].every((n) => !validName(n)));
	check("paths", baseName("a/b/c.meth") === "c.meth" && folderOf("a/b/c.meth") === "a/b" && folderOf("c.meth") === "" && titleOf("a/c.meth") === "c" && isFilePath("X.MESH") === false && isFilePath("x.MeTh"));

	// a new workspace
	let store = new MemoryStorage();
	let ws = Workspace.load(store, { name: "Welcome.meth", text: "# hello\n" });

	check("start: one file", [...ws.files.keys()].join() === "Welcome.meth" && ws.text("Welcome.meth") === "# hello\n");
	check("start: open and active", ws.tabs.join() === "Welcome.meth" && ws.active === "Welcome.meth");

	// files
	const a = ws.createFile("", "untitled");
	const b = ws.createFile("", "untitled");

	check("create: extension and number", a === "untitled.meth" && b === "untitled 2.meth", [a, b]);
	check("create: opened", ws.tabs.length === 3 && ws.active === b);
	check("create: invalid name throws", throws(() => ws.createFile("", "a/b")) !== null && throws(() => ws.createFile("", "")) !== null);

	// folders
	const f = ws.createFolder("", "physics");
	const g = ws.createFolder(f, "mechanics");
	const h = ws.createFile(g, "pendulum", "m:algebra 1\n");

	check("folders: paths", f === "physics" && g === "physics/mechanics" && h === "physics/mechanics/pendulum.meth", [f, g, h]);
	check("folders: expanded", ws.expanded.has(f) && ws.expanded.has(g));
	const tree = ws.tree();

	check("tree: folders first, then by name", tree.map((n) => n.name).join("|") === "physics|untitled.meth|untitled 2.meth|Welcome.meth".split("|").sort((x, y) => (x === "physics" ? -1 : y === "physics" ? 1 : x.localeCompare(y, undefined, { numeric: true, sensitivity: "base" }))).join("|"), tree.map((n) => n.name));
	check("tree: nested", tree[0].children[0].name === "mechanics" && tree[0].children[0].children[0].name === "pendulum.meth");

	// a name that is taken, in any case
	check("create: free name in a folder", ws.createFile(g, "PENDULUM") === "physics/mechanics/PENDULUM 2.meth", ws.files.has("physics/mechanics/PENDULUM 2.meth"));

	// renaming
	check("rename: adds the extension", ws.rename("untitled.meth", "energy") === "energy.meth" && ws.files.has("energy.meth") && !ws.files.has("untitled.meth"));
	check("rename: tabs follow", ws.tabs.includes("energy.meth") && !ws.tabs.includes("untitled.meth"));
	check("rename: storage follows", store.getItem("meth.f:energy.meth") !== null && store.getItem("meth.f:untitled.meth") === null);
	check("rename: clash throws", throws(() => ws.rename("energy.meth", "UNTITLED 2")) !== null && throws(() => ws.rename("energy.meth", "Welcome.meth")) !== null);
	check("rename: same name is nothing", ws.rename("energy.meth", "energy") === "energy.meth");
	check("rename: invalid throws", throws(() => ws.rename("energy.meth", "a/b")) !== null);

	// renaming a folder moves everything in it
	ws.setView(h, { collapsed: [0] });
	const moved = ws.rename("physics", "science");

	check("rename folder: files", moved === "science" && ws.files.has("science/mechanics/pendulum.meth") && !ws.files.has("physics/mechanics/pendulum.meth"));
	check("rename folder: text kept", ws.text("science/mechanics/pendulum.meth") === "m:algebra 1\n");
	check("rename folder: tabs, folders, view", ws.tabs.includes("science/mechanics/pendulum.meth") && ws.isFolder("science/mechanics") && ws.expanded.has("science") && JSON.stringify(ws.view("science/mechanics/pendulum.meth")) === '{"collapsed":[0]}');
	check("rename folder: old keys gone", store.getItem("meth.f:physics/mechanics/pendulum.meth") === null && store.getItem("meth.f:science/mechanics/pendulum.meth") !== null);

	// moving
	check("move: into a folder", ws.move("energy.meth", "science") === "science/energy.meth" && ws.files.has("science/energy.meth"));
	check("move: out to the top", ws.move("science/energy.meth", "") === "energy.meth");
	check("move: into itself throws", throws(() => ws.move("science", "science/mechanics")) !== null && throws(() => ws.move("science", "science")) !== null);
	ws.addFile("energy", "x", "science");
	check("move: clash throws", throws(() => ws.move("energy.meth", "science")) !== null);

	// text and what is written
	store.writes.length = 0;
	ws.setText("Welcome.meth", "# changed\n");
	check("text: only that file is written", store.writes.join() === "meth.f:Welcome.meth", store.writes);
	store.writes.length = 0;
	ws.setText("Welcome.meth", "# changed\n");
	check("text: no change, no write", store.writes.length === 0);

	// duplicating
	const copy = ws.duplicate("Welcome.meth");

	check("duplicate", copy === "Welcome copy.meth" && ws.text(copy) === "# changed\n" && ws.active === copy);

	// tabs
	ws.activate("Welcome.meth");
	check("tab: activate", ws.active === "Welcome.meth");
	const before = [...ws.tabs];

	ws.closeTab("Welcome.meth");
	check("tab: closing the active one brings its neighbour", !ws.tabs.includes("Welcome.meth") && ws.tabs.includes(ws.active) && ws.tabs.length === before.length - 1, ws.active);
	check("tab: the file is still there", ws.files.has("Welcome.meth"));
	ws.open("Welcome.meth");
	check("tab: open again", ws.tabs.at(-1) === "Welcome.meth" && ws.active === "Welcome.meth");
	ws.moveTab("Welcome.meth", 0);
	check("tab: move", ws.tabs[0] === "Welcome.meth");

	// removing
	ws.remove("science");
	check("remove folder: files and tabs", ![...ws.files.keys()].some((p) => p.startsWith("science")) && !ws.isFolder("science") && !ws.tabs.some((p) => p.startsWith("science")));
	check("remove: storage cleared", store.getItem("meth.f:science/mechanics/pendulum.meth") === null && ![...store.map.keys()].some((k) => k.startsWith("meth.f:science")));
	check("remove: meta cleared", JSON.stringify(ws.view("science/mechanics/pendulum.meth")) === "{}");
	check("remove: the active tab is a tab that exists", ws.active === null || ws.tabs.includes(ws.active));

	// events
	const events = [];

	ws.on((event) => events.push(event.type));
	ws.createFile("", "ev");
	ws.rename("ev.meth", "ev2");
	ws.remove("ev2.meth");
	check("events", events.join() === "create,move,remove", events);

	// everything survives a reload
	ws.toggleFolder("a-folder");
	ws.setSidebar({ open: false, width: 300 });
	ws.setView("Welcome.meth", { forced: [2], allResults: [0], collapsed: [4] });
	const again = Workspace.load(store, { name: "Other.meth", text: "never" });

	check("reload: files", [...again.files.keys()].sort().join() === [...ws.files.keys()].sort().join() && again.text("Welcome.meth") === "# changed\n");
	check("reload: folders, tabs, active", [...again.folders].sort().join() === [...ws.folders].sort().join() && again.tabs.join() === ws.tabs.join() && again.active === ws.active);
	check("reload: open folders and the side bar", again.expanded.has("a-folder") && again.sidebar.open === false && again.sidebar.width === 300);
	check("reload: view of a file", JSON.stringify(again.view("Welcome.meth")) === '{"forced":[2],"allResults":[0],"collapsed":[4]}');
	check("reload: no starter file", !again.files.has("Other.meth"));

	// the files of the first version
	store = new MemoryStorage();
	store.setItem("meth.session", "m:algebra 7\n");
	store.setItem("meth.name", "old work");
	ws = Workspace.load(store, { name: "Welcome.meth", text: "" });
	check("migration: one file from the old session", [...ws.files.keys()].join() === "old work.meth" && ws.text("old work.meth") === "m:algebra 7\n" && ws.active === "old work.meth");

	// a storage that refuses
	store = new MemoryStorage();
	store.full = true;
	ws = Workspace.load(store, { name: "Welcome.meth", text: "x" });
	ws.createFile("", "more", "y");
	check("full storage: the work goes on", ws.persistent === false && ws.files.size === 2 && ws.text("more.meth") === "y");

	// a storage that breaks
	const broken = {
		getItem() {
			throw new Error("blocked");
		},
		setItem() {
			throw new Error("blocked");
		},
		removeItem() {
			throw new Error("blocked");
		},
	};

	ws = Workspace.load(broken, { name: "Welcome.meth", text: "z" });
	check("blocked storage: a starter file in memory", ws.files.size === 1 && ws.text("Welcome.meth") === "z" && ws.persistent === false);

	// damaged index
	store = new MemoryStorage();
	store.setItem("meth.ws", "{not json");
	ws = Workspace.load(store, { name: "Welcome.meth", text: "w" });
	check("damaged index: starts over", ws.files.size === 1);
	store = new MemoryStorage();
	store.setItem("meth.ws", JSON.stringify({ v: 1, files: ["gone.meth", "kept.meth"], tabs: ["gone.meth", "nowhere.meth"], active: "nowhere.meth" }));
	store.setItem("meth.f:kept.meth", "k");
	ws = Workspace.load(store, { name: "Welcome.meth", text: "" });
	check("damaged index: missing text is empty, tabs are repaired", ws.text("gone.meth") === "" && ws.text("kept.meth") === "k" && ws.tabs.every((p) => ws.files.has(p)) && ws.tabs.includes(ws.active), [ws.tabs, ws.active]);
} catch (error) {
	check("model test ran", false, error.stack ?? error);
}

results.textContent = JSON.stringify(outcome);
status.textContent = `${outcome.filter((c) => c.ok).length} of ${outcome.length} passed`;
