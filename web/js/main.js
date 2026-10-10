// The page: loads the module, opens the workspace of the user (the files are
// kept in the browser storage), builds the explorer, the tabs and the
// notebooks, and wires the bar, the menus and the help.

import { App } from "./app.js";
import { Help } from "./help.js";
import { Tutorial } from "./tutorial.js";
import { loadMeth } from "./meth.js";
import { cycleMode, initTheme } from "./theme.js";
import { MemoryStorage, Workspace, baseName } from "./workspace.js";

const WELCOME = [
	"# # meth",
	"# A notebook for mathematics. Every line is evaluated as you type. The result of the last line of a cell is shown; **show result** and **steps** at the right of a line show more, and the cell has a button for all results. Press `Shift+Enter` for a new cell and `F1` for help.",
	"",
	"m:algebra 1/3 + 1/6",
	"m:algebra sqrt(2) * 3",
	"m:algebra 2^100",
	"",
	"# ## Matrices",
	"m:algebra A = [[1, 2], [3, 4]]",
	"m:algebra A^-1",
	"m:algebra det(A)",
	"",
	"# ## Calculus",
	"m:algebra diff(x^3 + sin(x), x)",
	"m:algebra integrate(x * exp(x), x)",
	"",
].join("\n");

const toast = document.getElementById("toast");
let toastTimer = 0;

function say(message) {
	toast.textContent = message;
	toast.classList.add("show");
	clearTimeout(toastTimer);
	toastTimer = setTimeout(() => toast.classList.remove("show"), 2800);
}

function download(name, text, type) {
	const link = document.createElement("a");
	const url = URL.createObjectURL(new Blob([text], { type }));

	link.href = url;
	link.download = name;
	document.body.append(link);
	link.click();
	link.remove();
	setTimeout(() => URL.revokeObjectURL(url), 1000);
}

// The storage of the browser, or one that keeps nothing when it is blocked.
function storage() {
	let store = null;

	try {
		store = window.localStorage;
		store.getItem("meth.test");
	} catch {
		store = new MemoryStorage();
	}

	return store;
}

async function main() {
	const meth = await loadMeth("meth.wasm");
	const ws = Workspace.load(storage(), { name: "Welcome.meth", text: WELCOME });
	const shell = document.getElementById("shell");
	const titleElement = document.getElementById("title");
	const fileInput = document.getElementById("file-input");
	const exports = {
		latex: ["tex", "application/x-tex"],
		mathml: ["html", "text/html"],
		markdown: ["md", "text/markdown"],
	};
	let warned = false;

	initTheme();

	const app = new App({
		meth,
		ws,
		explorer: document.getElementById("explorer"),
		tabs: document.getElementById("tabs"),
		panes: document.getElementById("panes"),
		say,
		confirm: (message) => window.confirm(message),
		download,
		requestSymbols: () => actions.symbols(document.querySelector('[data-act="symbols"]'), true),
		empty(box) {
			const text = document.createElement("p");
			const make = document.createElement("button");
			const open = document.createElement("button");

			text.textContent = "No file is open.";
			make.textContent = "New file";
			make.addEventListener("click", () => app.createFile(app.explorer.targetFolder()));
			open.textContent = "Open a file…";
			open.addEventListener("click", () => fileInput.click());
			box.append(text, make, open);
		},
		onActive(path) {
			titleElement.textContent = path === null ? "" : path.split("/").join("  /  ").replace(/\.meth$/i, "");
			titleElement.title = path ?? "";
			document.title = path === null ? "meth" : `${baseName(path).replace(/\.meth$/i, "")} — meth`;

			if (!ws.persistent && !warned) {
				warned = true;
				say("The browser would not keep your files: download them to be safe.");
			}
		},
	});
	const help = new Help(meth, document.getElementById("help"), {
		tryExample(lines) {
			const doc = app.active;

			if (doc) {
				doc.notebook.insertText(doc.notebook.count - 1, lines.map((l) => `m:algebra ${l}`).join("\n"));
				say("Added to the notebook.");
			}
		},
	});

	const tutorial = new Tutorial(meth, document.getElementById("tutorial"), { app, ws, say, store: storage() });

	// ---- the side bar ------------------------------------------------------

	function layout() {
		shell.classList.toggle("no-sidebar", !ws.sidebar.open);
		shell.style.setProperty("--sidebar-w", `${ws.sidebar.width}px`);
		document.querySelector('[data-act="sidebar"]').setAttribute("aria-pressed", String(ws.sidebar.open));
	}

	ws.on((event) => event.type === "sidebar" && layout());

	if (window.innerWidth < 700) {
		ws.sidebar = { ...ws.sidebar, open: false }; // on a phone the files are behind the button
	}

	layout();

	const splitter = document.getElementById("splitter");

	splitter.addEventListener("pointerdown", (event) => {
		const start = event.clientX;
		const width = ws.sidebar.width;
		const move = (e) => {
			ws.sidebar = { ...ws.sidebar, width: Math.min(Math.max(width + e.clientX - start, 160), 520) };
			layout();
		};
		const up = () => {
			window.removeEventListener("pointermove", move);
			window.removeEventListener("pointerup", up);
			ws.setSidebar({});
		};

		event.preventDefault();
		window.addEventListener("pointermove", move);
		window.addEventListener("pointerup", up);
	});

	// ---- the menus ------------------------------------------------------------

	const menus = [...document.querySelectorAll("details.menu")];

	function closeMenus(except) {
		for (const menu of menus) {
			if (menu !== except) {
				menu.open = false;
			}
		}
	}

	for (const menu of menus) {
		menu.addEventListener("toggle", () => menu.open && closeMenus(menu));
	}

	document.addEventListener("click", (event) => {
		if (!event.target.closest("details.menu")) {
			closeMenus(null);
		}
	});

	function fillMenu(popup, groups, choose) {
		popup.replaceChildren();

		for (const [title, items] of groups) {
			const heading = document.createElement("h3");

			heading.textContent = title;
			popup.append(heading);

			for (const item of items) {
				const choice = document.createElement("button");
				const small = document.createElement("small");

				choice.append(item.label);
				small.textContent = item.detail;
				choice.append(small);
				choice.addEventListener("click", () => {
					closeMenus(null);
					choose(item);
				});
				popup.append(choice);
			}
		}
	}

	const grouped = (list, label, detail) => {
		const groups = new Map();

		for (const entry of list) {
			const key = entry.area ?? "Snippets";

			groups.set(key, [...(groups.get(key) ?? []), { ...entry, label: label(entry), detail: detail(entry) }]);
		}

		return [...groups];
	};

	// An example becomes a file in the folder "Examples", where it can be
	// changed; one that is there already is opened as it is.
	fillMenu(
		document.getElementById("examples-popup"),
		grouped(meth.examples(), (e) => e.title, (e) => e.summary),
		(item) => {
			const name = `${item.title.replace(/[\\/]/g, "-")}.meth`;

			if (ws.files.has(`Examples/${name}`)) {
				ws.open(`Examples/${name}`);
			} else {
				ws.createFile("Examples", name, meth.example(item.name));
			}
		},
	);
	fillMenu(
		document.getElementById("snippets-popup"),
		grouped(meth.snippets(), (s) => s.name, (s) => s.summary),
		(item) => {
			const doc = app.active;

			if (!doc) {
				say("Open a file to put the snippet in.");
				return;
			}

			const at = doc.notebook.currentFocus()?.line ?? doc.notebook.count - 1;
			const last = doc.notebook.lines[at]?.last ?? at;

			doc.notebook.insertText(last, meth.snippet(item.name));
			say(`Inserted “${item.name}”.`);
		},
	);

	// ---- the actions of the bar ---------------------------------------------

	fileInput.addEventListener("change", async () => {
		const files = await Promise.all(
			[...fileInput.files].map(async (file) => ({ name: file.name, text: await file.text() })),
		);

		app.importFiles(files, app.explorer.targetFolder());
		fileInput.value = "";
	});

	const withActive = (action) => {
		if (app.active) {
			action(app.active);
		} else {
			say("No file is open.");
		}
	};
	const actions = {
		"new-file": () => app.createFile(app.explorer.targetFolder()),
		"new-folder": () => app.createFolder(app.explorer.targetFolder()),
		open: () => fileInput.click(),
		save: () => ws.active && app.downloadFile(ws.active),
		rename: () => ws.active && app.explorer.startRename(ws.active),
		close: () => ws.active && ws.closeTab(ws.active),
		delete: () => ws.active && app.remove(ws.active),
		sidebar: () => ws.setSidebar({ open: !ws.sidebar.open }),
		undo: () => withActive((doc) => doc.notebook.undo()),
		redo: () => withActive((doc) => doc.notebook.redo()),
		// symbols: names without a value stay symbols (x + x is 2x) instead of
		// being an error. `force` turns it on whatever it was.
		symbols(button, force = false) {
			const on = force || button.getAttribute("aria-pressed") !== "true";

			button.setAttribute("aria-pressed", String(on));
			app.setSymbols(on);
			say(
				on
					? "Symbols on: a name without a value stays a symbol, so x + x is 2x."
					: "Symbols off: a name without a value is an error.",
			);
		},
		theme: () => say(`Theme: ${cycleMode()}`),
		help: () => help.toggle(),
		tutorial: () => (tutorial.active ? tutorial.end() : tutorial.begin()),
	};

	for (const button of document.querySelectorAll("[data-act]")) {
		button.addEventListener("click", () => {
			const act = button.dataset.act;

			closeMenus(null);

			if (act.startsWith("export-")) {
				withActive((doc) => {
					const format = act.slice(7);
					const [extension, type] = exports[format];
					const name = baseName(ws.active).replace(/\.meth$/i, "");

					download(`${name}.${extension}`, doc.session.exportAs(format), type);
				});
			} else {
				actions[act](button);
			}
		});
	}

	titleElement.addEventListener("dblclick", () => ws.active && app.tabs.startRename(ws.active));

	document.addEventListener("keydown", (event) => {
		const mod = event.ctrlKey || event.metaKey;

		if (event.key === "F1") {
			event.preventDefault();
			help.toggle();
		} else if (mod && event.key.toLowerCase() === "s") {
			event.preventDefault();
			actions.save();
		} else if (mod && event.key.toLowerCase() === "b") {
			event.preventDefault();
			actions.sidebar();
		}
	});

	// keep what was typed when the page is left
	window.addEventListener("pagehide", () => app.flush());
	document.addEventListener("visibilitychange", () => document.hidden && app.flush());

	if (location.hash.startsWith("#help")) {
		help.open(location.hash.slice(6) || "index");
	}

	if (location.hash === "#tutorial") {
		tutorial.begin();
	}

	window.meth = { app, ws, tutorial }; // for looking around in the console
}

main().catch((error) => {
	const message = document.createElement("pre");

	message.textContent = `meth could not start: ${error.message}`;
	document.getElementById("panes").append(message);
});
