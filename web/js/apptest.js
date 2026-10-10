// The workspace as the user sees it: the explorer, the tabs and one notebook
// for each file, with the library behind them. Typing, clicks, the keys of the
// tree, the menu of the right button and drag and drop are done on the real
// elements. The outcome is put in the page as JSON: [{ name, ok, detail }].
// tools/test_web.py reads it.

import { App } from "./app.js";
import { loadMeth } from "./meth.js";
import { Tutorial } from "./tutorial.js";
import { MemoryStorage, Workspace } from "./workspace.js";

const status = document.getElementById("status");
const results = document.getElementById("results");
const outcome = [];
const NL = String.fromCharCode(10);

function check(name, ok, detail = "") {
	outcome.push({ name, ok: Boolean(ok), detail: String(detail) });
}

try {
	const meth = await loadMeth("meth.wasm");
	const store = new MemoryStorage();
	const ws = Workspace.load(store, { name: "Welcome.meth", text: `m:algebra a = 3${NL}m:algebra a * 2${NL}` });
	const said = [];
	const downloads = [];
	let answer = true;
	const app = new App({
		meth,
		ws,
		explorer: document.getElementById("explorer"),
		tabs: document.getElementById("tabs"),
		panes: document.getElementById("panes"),
		say: (message) => said.push(message),
		confirm: () => answer,
		download: (name, text) => downloads.push({ name, text }),
		empty: (box) => {
			box.textContent = "No file is open.";
		},
	});

	const explorer = document.getElementById("explorer");
	const tabs = document.getElementById("tabs");
	const panes = document.getElementById("panes");
	const node = (path) => explorer.querySelector(`.node[data-path="${path}"]`);
	const tab = (path) => tabs.querySelector(`.tab[data-path="${path}"]`);
	const pane = (path) => panes.querySelector(`.pane[data-path="${path}"]`);
	const shown = () => [...panes.querySelectorAll(".pane")].filter((p) => !p.hidden).map((p) => p.dataset.path);
	const typeInto = (path, i, value) => {
		const area = pane(path).querySelector(`textarea[data-i="${i}"]`);

		area.value = value;
		area.setSelectionRange(value.length, value.length);
		area.dispatchEvent(new Event("input", { bubbles: true }));
	};
	const key = (target, k, init = {}) => target.dispatchEvent(new KeyboardEvent("keydown", { key: k, bubbles: true, cancelable: true, ...init }));
	const rename = (input, value) => {
		input.value = value;
		key(input, "Enter");
	};

	// the start
	check("start: file in the explorer", node("Welcome.meth") !== null && node("Welcome.meth").textContent.includes("Welcome"));
	check("start: tab, active", tab("Welcome.meth")?.classList.contains("active") && tab("Welcome.meth").getAttribute("aria-selected") === "true");
	check("start: notebook of the file", shown().join() === "Welcome.meth" && pane("Welcome.meth").querySelectorAll(".line").length === 2);
	check("start: one session for it", app.docs.size === 1);

	// a new file from the button: named in place
	explorer.querySelector('button[aria-label="New file"]').click();
	let input = explorer.querySelector("input.rename");

	check("new file: name asked in the explorer", input !== null && input.value === "untitled", input?.value);
	check("new file: opened", ws.active === "untitled.meth" && shown().join() === "untitled.meth");
	rename(input, "physics");
	check("new file: named", ws.files.has("physics.meth") && !ws.files.has("untitled.meth") && node("physics.meth") !== null && explorer.querySelector("input.rename") === null);
	check("new file: tab has the name", tab("physics.meth")?.textContent.includes("physics") && tab("untitled.meth") === null);
	check("new file: its notebook went on", shown().join() === "physics.meth" && app.docs.has("physics.meth") && !app.docs.has("untitled.meth"));

	// two files apart
	check("new file: empty notebook offers a first cell", pane("physics.meth").querySelectorAll(".line").length === 0 && pane("physics.meth").querySelector(".insert.last") !== null);
	pane("physics.meth").querySelector(".insert.last button").click();
	check("new file: first cell", pane("physics.meth").querySelectorAll(".line").length === 1);
	typeInto("physics.meth", 0, "a = 100");
	ws.activate("Welcome.meth");
	check("tabs: switch shows the other file", shown().join() === "Welcome.meth");
	typeInto("Welcome.meth", 1, "a * 3");
	check("files: apart", pane("Welcome.meth").querySelector('.line[data-i="1"] .out')?.textContent.includes("9") && app.docs.get("physics.meth").session.lines().lines[0].result.text === "100");
	ws.activate("physics.meth");
	check("tabs: switching back keeps the text", pane("physics.meth").querySelector('textarea[data-i="0"]').value === "a = 100");

	// what is written to the workspace
	app.flush();
	check("save: text reaches the workspace", ws.text("physics.meth").includes("a = 100") && ws.text("Welcome.meth").includes("a * 3"));
	check("save: and the storage", store.getItem("meth.f:physics.meth").includes("a = 100"));
	await new Promise((resolve) => setTimeout(resolve, 600));
	typeInto("physics.meth", 0, "a = 7");
	await new Promise((resolve) => setTimeout(resolve, 600));
	check("save: after a pause without flush", ws.text("physics.meth").includes("a = 7"), ws.text("physics.meth"));

	// renaming an open file keeps its notebook and its saving
	tab("physics.meth").querySelector(".tab-name").dispatchEvent(new MouseEvent("dblclick", { bubbles: true }));
	input = tabs.querySelector("input.rename");
	check("rename: tab asks", input !== null && input.value === "physics");
	rename(input, "mechanics");
	check("rename: everywhere", ws.files.has("mechanics.meth") && tab("mechanics.meth") !== null && node("mechanics.meth") !== null && pane("mechanics.meth") !== null && app.docs.has("mechanics.meth"));
	typeInto("mechanics.meth", 0, "a = 8");
	app.flush();
	check("rename: still saving", ws.text("mechanics.meth").includes("a = 8") && !ws.files.has("physics.meth"));
	typeInto("mechanics.meth", 0, "a = 9");
	await new Promise((resolve) => setTimeout(resolve, 600));
	check("rename: saving after a pause too (the new path)", ws.text("mechanics.meth").includes("a = 9"), ws.text("mechanics.meth"));
	tab("mechanics.meth").querySelector(".tab-name").dispatchEvent(new MouseEvent("dblclick", { bubbles: true }));
	rename(tabs.querySelector("input.rename"), "Welcome");
	check("rename: a name that is taken is refused", ws.files.has("mechanics.meth") && said.some((m) => /already/.test(m)), said.join("|"));
	key(tabs.querySelector("input.rename"), "Escape");
	check("rename: escape leaves it", tabs.querySelector("input.rename") === null && ws.files.has("mechanics.meth"));

	// folders
	explorer.querySelector('button[aria-label="New folder"]').click();
	rename(explorer.querySelector("input.rename"), "notes");
	check("folder: made and named", ws.isFolder("notes") && node("notes")?.classList.contains("folder"));
	node("notes").click();
	check("folder: selected folder is the target", app.explorer.targetFolder() === "notes");
	explorer.querySelector('button[aria-label="New file"]').click();
	rename(explorer.querySelector("input.rename"), "first");
	check("folder: file in it", ws.files.has("notes/first.meth") && node("notes/first.meth") !== null && ws.expanded.has("notes"));
	check("folder: indented", Number.parseFloat(node("notes/first.meth").style.paddingLeft) > Number.parseFloat(node("notes").style.paddingLeft));

	// open and close folders
	node("notes").click();
	check("folder: close", node("notes/first.meth") === null && !ws.expanded.has("notes"));
	node("notes").click();
	check("folder: open", node("notes/first.meth") !== null);

	// drag a file onto a folder
	const transfer = new DataTransfer();

	transfer.setData("text/x-meth-path", "mechanics.meth");
	node("mechanics.meth").dispatchEvent(new DragEvent("dragstart", { bubbles: true, dataTransfer: transfer }));
	node("notes").dispatchEvent(new DragEvent("dragover", { bubbles: true, cancelable: true, dataTransfer: transfer }));
	check("drag: target lights up", node("notes").classList.contains("drop"));
	node("notes").dispatchEvent(new DragEvent("drop", { bubbles: true, cancelable: true, dataTransfer: transfer }));
	check("drag: moved into the folder", ws.files.has("notes/mechanics.meth") && !ws.files.has("mechanics.meth") && app.docs.has("notes/mechanics.meth"));
	check("drag: the notebook came along", pane("notes/mechanics.meth") !== null && app.docs.get("notes/mechanics.meth").session.lines().lines[0].text === "a = 9");

	// drop on the empty space: back to the top
	const back = new DataTransfer();

	back.setData("text/x-meth-path", "notes/mechanics.meth");
	explorer.querySelector(".tree").dispatchEvent(new DragEvent("drop", { bubbles: true, cancelable: true, dataTransfer: back }));
	check("drag: to the top", ws.files.has("mechanics.meth") && !ws.files.has("notes/mechanics.meth"));

	// a folder into itself is refused
	const into = new DataTransfer();

	into.setData("text/x-meth-path", "notes");
	node("notes").dispatchEvent(new DragEvent("drop", { bubbles: true, cancelable: true, dataTransfer: into }));
	check("drag: folder into itself", ws.isFolder("notes") && said.some((m) => /itself/.test(m)), said.join("|"));

	// the keys of the tree
	explorer.querySelector(".tree").focus();
	node("Welcome.meth").click();
	key(explorer.querySelector(".tree"), "ArrowUp");
	check("keys: arrow moves the selection", app.explorer.selected !== "Welcome.meth", app.explorer.selected);
	key(explorer.querySelector(".tree"), "ArrowDown");
	check("keys: and back", app.explorer.selected === "Welcome.meth", app.explorer.selected);
	key(explorer.querySelector(".tree"), "F2");
	check("keys: F2 renames", explorer.querySelector("input.rename") !== null);
	key(explorer.querySelector("input.rename"), "Escape");
	app.explorer.select("notes");
	key(explorer.querySelector(".tree"), "ArrowLeft");
	check("keys: left closes a folder", !ws.expanded.has("notes"));
	key(explorer.querySelector(".tree"), "ArrowRight");
	check("keys: right opens it", ws.expanded.has("notes"));

	// the menu of the right button
	node("notes/first.meth").dispatchEvent(new MouseEvent("contextmenu", { bubbles: true, cancelable: true, clientX: 40, clientY: 40 }));
	const menu = document.querySelector(".context-menu");
	const labels = [...menu.querySelectorAll("button")].map((b) => b.textContent);

	check("menu: commands", !menu.hidden && ["New file", "New folder", "Rename", "Duplicate", "Download", "Delete"].every((l) => labels.includes(l)), labels);
	[...menu.querySelectorAll("button")].find((b) => b.textContent === "Duplicate").click();
	check("menu: duplicate", ws.files.has("notes/first copy.meth") && menu.hidden);
	node("notes").dispatchEvent(new MouseEvent("contextmenu", { bubbles: true, cancelable: true, clientX: 50, clientY: 50 }));
	check("menu: a folder has no duplicate", ![...document.querySelectorAll(".context-menu button")].some((b) => b.textContent === "Duplicate"));
	document.querySelector(".context-menu").dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true }));
	check("menu: escape closes it", document.querySelector(".context-menu").hidden);

	// download
	node("Welcome.meth").dispatchEvent(new MouseEvent("contextmenu", { bubbles: true, cancelable: true, clientX: 40, clientY: 40 }));
	[...document.querySelectorAll(".context-menu button")].find((b) => b.textContent === "Download").click();
	check("download: name and text", downloads.length === 1 && downloads[0].name === "Welcome.meth" && downloads[0].text.includes("a * 3"), JSON.stringify(downloads));

	// deleting asks first
	answer = false;
	node("notes/first copy.meth").click();
	key(explorer.querySelector(".tree"), "Delete");
	check("delete: refused", ws.files.has("notes/first copy.meth"));
	answer = true;
	key(explorer.querySelector(".tree"), "Delete");
	check("delete: file gone, tab gone, session given back", !ws.files.has("notes/first copy.meth") && tab("notes/first copy.meth") === null && !app.docs.has("notes/first copy.meth") && pane("notes/first copy.meth") === null);
	app.remove("notes");
	check("delete: folder with its files", !ws.isFolder("notes") && ![...ws.files.keys()].some((p) => p.startsWith("notes/")) && node("notes") === null);

	// closing tabs
	const open = ws.tabs.length;

	tab("Welcome.meth").querySelector(".tab-close").click();
	check("close: tab and session", tab("Welcome.meth") === null && !app.docs.has("Welcome.meth") && ws.tabs.length === open - 1);
	check("close: the file is still in the explorer", node("Welcome.meth") !== null && ws.text("Welcome.meth").includes("a * 3"));
	node("Welcome.meth").click();
	check("open again: text is there", app.docs.has("Welcome.meth") && pane("Welcome.meth").querySelector('textarea[data-i="1"]').value === "a * 3");
	tab("Welcome.meth").dispatchEvent(new MouseEvent("auxclick", { button: 1, bubbles: true, cancelable: true }));
	check("close: middle button", tab("Welcome.meth") === null);

	// a tab dragged to another place
	ws.open("Welcome.meth");
	const order = [...ws.tabs];
	const swap = new DataTransfer();

	swap.setData("text/x-meth-tab", order.at(-1));
	tab(order[0]).dispatchEvent(new DragEvent("dragover", { bubbles: true, cancelable: true, dataTransfer: swap }));
	tab(order[0]).dispatchEvent(new DragEvent("drop", { bubbles: true, cancelable: true, dataTransfer: swap }));
	check("tabs: dragged to the front", ws.tabs[0] === order.at(-1), ws.tabs);

	// many files at once
	const handles = new Set([...app.docs.values()].map((d) => d.session.handle));

	for (let i = 0; i < 25; i++) {
		ws.createFile("", `bulk ${i}`, `m:algebra 2^${i}${NL}`);
	}

	check("many: all open with a session of their own", app.docs.size === ws.tabs.length && ws.tabs.length >= 27, [app.docs.size, ws.tabs.length]);
	check("many: each shows its own file", pane("bulk 24.meth").querySelectorAll(".line").length === 1 && app.docs.get("bulk 24.meth").session.lines().lines[0].result.text === "16777216");
	check("many: the tab bar has no scroll bar", getComputedStyle(tabs.querySelector(".tab-list")).overflowY === "hidden" && getComputedStyle(tabs.querySelector(".tab-list")).scrollbarWidth === "none");
	for (let i = 0; i < 25; i++) {
		ws.closeTab(`bulk ${i}.meth`);
	}

	check("many: closed, sessions given back", app.docs.size === ws.tabs.length, [app.docs.size, ws.tabs.length]);
	check("many: handles are reused", app.docs.size < 5 && [...app.docs.values()].every((d) => d.session.handle <= Math.max(...handles) + 1) === true);

	// the symbols setting reaches every open file
	ws.createFile("", "sym", `m:algebra x + x${NL}`);
	check("symbols: off, an error", app.docs.get("sym.meth").session.lines().lines[0].result.kind === "error");
	app.setSymbols(true);
	check("symbols: on, a value in every file", [...app.docs.values()].every((d) => d.session.lines().lines.every((l) => l.kind !== "math" || l.result.kind !== "error" || !/undefined name/.test(l.result.text))) && app.docs.get("sym.meth").session.lines().lines[0].result.kind === "value");
	app.setSymbols(false);

	// the files that are open when the page is left come back
	app.flush();
	const index = JSON.parse(store.getItem("meth.ws"));

	check("storage: index has tabs and files", index.tabs.length === ws.tabs.length && index.files.length === ws.files.size && index.active === ws.active);

	// nothing open
	for (const path of [...ws.tabs]) {
		ws.closeTab(path);
	}

	check("empty: no notebook, a message", app.docs.size === 0 && panes.querySelector(".empty-state") !== null && shown().length === 0);
	ws.open("Welcome.meth");
	check("empty: gone again", panes.querySelector(".empty-state") === null && shown().join() === "Welcome.meth");

	// the tutorial: lessons played in a scratch file
	{
		const panel = document.getElementById("tutorial");
		const tut = new Tutorial(meth, panel, { app, ws, say: () => {}, store: new MemoryStorage() });
		const before = [...ws.files.keys()];

		check("tutorial: lessons of the result kind", tut.lessons.length >= 5 && tut.lessons[0].id === "numbers", tut.lessons.length);
		tut.begin();
		check("tutorial: a scratch file and the first task", tut.active && !panel.hidden && ws.files.has(tut.path) && !before.includes(tut.path) && panel.querySelector(".task").textContent.startsWith("Write a line whose result is 1/2"), panel.textContent);

		const notebook = () => app.docs.get(tut.path).notebook;
		const write = (text) => notebook().insertText(notebook().count - 1, `m:algebra ${text}`);

		write("2/3");
		check("tutorial: a wrong result keeps the step", tut.step === 0 && tut.misses > 0, [tut.step, tut.misses]);
		write("2/4");
		check("tutorial: the right result moves on", tut.step === 1 && panel.querySelector(".task").textContent.includes("-4"), [tut.step, panel.textContent]);
		write("2/4");
		check("tutorial: an earlier line does not count again", tut.step === 1, tut.step);
		write("-2^2");
		check("tutorial: the next step", tut.step === 2, tut.step);

		tut.skip();
		tut.skip();
		tut.back();
		check("tutorial: skip and back", tut.step >= 2 && !tut.done, tut.step);

		while (!tut.done) {
			tut.skip();
		}

		check("tutorial: lesson done, remembered", panel.querySelector(".ok") !== null && tut.finished() === "numbers", tut.finished());
		tut.enter(tut.lesson + 1);
		check("tutorial: next lesson", tut.lesson === 1 && !tut.done && panel.querySelector(".dots") !== null, tut.lesson);
		tut.end();
		check("tutorial: leaving hides the panel", !tut.active && panel.hidden);

		const again = new Tutorial(meth, panel, { app, ws, say: () => {}, store: tut.store });

		again.begin();
		check("tutorial: resumes after the last finished lesson", again.lesson === 1, again.lesson);
		again.end();
	}

	// the explorer with nothing in it
	for (const path of [...ws.files.keys()]) {
		ws.remove(path);
	}

	for (const path of [...ws.folders]) {
		ws.remove(path);
	}

	check("empty explorer: a hint", explorer.querySelector(".tree .empty") !== null);
} catch (error) {
	check("app test ran", false, error.stack ?? error);
}

results.textContent = JSON.stringify(outcome);
status.textContent = `${outcome.filter((c) => c.ok).length} of ${outcome.length} passed`;
