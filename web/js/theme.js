// Light and dark mode. The mode is "auto" (the system's), "light" or "dark";
// it is kept in the browser storage, which may be missing, so every access is
// guarded. The syntax colours are the page's own (css/style.css); the themes of
// the library are for terminals and are used only when applyColors () is called
// with one of them (a theme the user chooses or loads).

const MODES = ["auto", "light", "dark"];
const KEY = "meth.mode";

function stored() {
	let mode = "auto";

	try {
		mode = localStorage.getItem(KEY) ?? "auto";
	} catch {
		// the storage is not available: the default stays
	}

	return MODES.includes(mode) ? mode : "auto";
}

function systemDark() {
	return window.matchMedia("(prefers-color-scheme: dark)").matches;
}

// Sets --c-<class> from a theme of the library (js/meth.js: themes (), themeFrom ()).
export function applyColors(theme) {
	const root = document.documentElement;

	for (const [name, style] of Object.entries(theme.classes)) {
		if (style.fg) {
			root.style.setProperty(`--c-${name}`, style.fg);
		} else {
			root.style.removeProperty(`--c-${name}`);
		}
	}
}

function resolve(mode) {
	return mode === "auto" ? (systemDark() ? "dark" : "light") : mode;
}

function apply(mode) {
	const root = document.documentElement;

	root.dataset.mode = resolve(mode);
	root.dataset.setting = mode;
}

export function initTheme() {
	apply(stored());
	window
		.matchMedia("(prefers-color-scheme: dark)")
		.addEventListener("change", () => apply(stored()));
}

// auto -> light -> dark -> auto; returns the new setting.
export function cycleMode() {
	const next = MODES[(MODES.indexOf(stored()) + 1) % MODES.length];

	try {
		localStorage.setItem(KEY, next);
	} catch {
		// not kept
	}

	apply(next);
	return next;
}
