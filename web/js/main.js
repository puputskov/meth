// The page: loads the module, builds the notebook, the menus and the help,
// and keeps the session in the browser storage.

import { loadMeth } from "./meth.js";
import { Notebook } from "./notebook.js";
import { Help } from "./help.js";
import { cycleMode, initTheme } from "./theme.js";

const STORE = "meth.session";
const NAME = "meth.name";

const WELCOME = [
	"# # meth",
	"# A notebook for mathematics. Every line is evaluated as you type. Click **steps** under a result for the working, press `Shift+Enter` for a new cell and `F1` for help.",
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

function remember(key, value) {
	try {
		if (value === null) {
			localStorage.removeItem(key);
		} else {
			localStorage.setItem(key, value);
		}
	} catch {
		// private window or blocked storage: the session is not kept
	}
}

function recall(key) {
	let value = null;

	try {
		value = localStorage.getItem(key);
	} catch {
		// nothing kept
	}

	return value;
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

const toast = document.getElementById("toast");
let toastTimer = 0;

function say(message) {
	toast.textContent = message;
	toast.classList.add("show");
	clearTimeout(toastTimer);
	toastTimer = setTimeout(() => toast.classList.remove("show"), 2600);
}

async function main() {
	const meth = await loadMeth("meth.wasm");
	let name = recall(NAME) ?? "untitled";
	let saveTimer = 0;
	let symbols = false;

	initTheme();

	const titleElement = document.getElementById("title");
	const notebook = new Notebook(meth, document.getElementById("notebook"), {
		domains: meth.domains(),
		say,
		onChange() {
			clearTimeout(saveTimer);
			saveTimer = setTimeout(() => remember(STORE, meth.save()), 400);
		},
	});
	const help = new Help(meth, document.getElementById("help"), {
		tryExample(lines) {
			notebook.insertText(notebook.count - 1, lines.map((l) => `m:algebra ${l}`).join("\n"));
			say("Added to the notebook.");
		},
	});

	function setName(next) {
		name = next;
		titleElement.textContent = name;
		document.title = `${name} — meth`;
		remember(NAME, name);
	}

	function open(text, title) {
		notebook.load(text);
		setName(title);
	}

	// ---- the menus --------------------------------------------------

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

	fillMenu(
		document.getElementById("examples-popup"),
		grouped(meth.examples(), (e) => e.title, (e) => e.summary),
		(item) => {
			if (!window.confirm("Replace the notebook with this example?")) {
				return;
			}

			open(meth.example(item.name), item.title);
		},
	);
	fillMenu(
		document.getElementById("snippets-popup"),
		grouped(meth.snippets(), (s) => s.name, (s) => s.summary),
		(item) => {
			const at = notebook.currentFocus()?.line ?? notebook.count - 1;
			const last = notebook.lines[at]?.last ?? at;

			notebook.insertText(last, meth.snippet(item.name));
			say(`Inserted “${item.name}”.`);
		},
	);

	// ---- the actions of the bar -----------------------------------------

	const fileInput = document.getElementById("file-input");

	fileInput.addEventListener("change", async () => {
		const [file] = fileInput.files;

		if (file) {
			open(await file.text(), file.name.replace(/\.meth$/, ""));
		}

		fileInput.value = "";
	});

	const exports = {
		latex: ["tex", "application/x-tex"],
		mathml: ["html", "text/html"],
		markdown: ["md", "text/markdown"],
	};
	const actions = {
		new() {
			if (window.confirm("Start a new, empty notebook?")) {
				open("", "untitled");
			}
		},
		open: () => fileInput.click(),
		save() {
			download(`${name}.meth`, meth.save(), "text/plain");
		},
		undo: () => notebook.undo(),
		redo: () => notebook.redo(),
		symbols(button) {
			symbols = !symbols;
			button.setAttribute("aria-pressed", String(symbols));
			notebook.applyAnswer(meth.setSymbols(symbols));
			notebook.render();
		},
		theme() {
			say(`Theme: ${cycleMode()}`);
		},
		help: () => help.toggle(),
	};

	for (const button of document.querySelectorAll("[data-act]")) {
		button.addEventListener("click", () => {
			const act = button.dataset.act;

			closeMenus(null);

			if (act.startsWith("export-")) {
				const format = act.slice(7);
				const [extension, type] = exports[format];

				download(`${name}.${extension}`, meth.exportAs(format), type);
			} else {
				actions[act](button);
			}
		});
	}

	document.addEventListener("keydown", (event) => {
		if (event.key === "F1") {
			event.preventDefault();
			help.toggle();
		} else if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "s") {
			event.preventDefault();
			actions.save();
		}
	});

	// ---- start ------------------------------------------------------------

	const kept = recall(STORE);

	setName(name);
	notebook.load(kept ?? WELCOME);

	if (location.hash.startsWith("#help")) {
		help.open(location.hash.slice(6) || "index");
	}
}

main().catch((error) => {
	const message = document.createElement("pre");

	message.textContent = `meth could not start: ${error.message}`;
	document.getElementById("notebook").append(message);
});
