// Loads meth.wasm and wraps its C API (source/web/api.c). The module has no
// imports besides the math functions of the page and format_g, and the page
// reads and writes text in its memory only through meth_alloc and the result
// pointer, so nothing here knows a structure of the library.

const encoder = new TextEncoder();
const decoder = new TextDecoder();

// printf's %.<prec>f for a double.
export function formatF(value, precision) {
	let text;

	if (Number.isNaN(value)) {
		text = "nan";
	} else if (!Number.isFinite(value)) {
		text = value < 0 ? "-inf" : "inf";
	} else {
		text = value.toFixed(Math.min(precision, 100));
	}

	return text;
}

// printf's %.<prec>g for a double, the way the C library writes it.
export function formatG(value, precision) {
	const p = precision === 0 ? 1 : precision;
	let text;

	if (Number.isNaN(value)) {
		text = "nan";
	} else if (!Number.isFinite(value)) {
		text = value < 0 ? "-inf" : "inf";
	} else if (value === 0) {
		text = Object.is(value, -0) ? "-0" : "0";
	} else {
		const exponent = Number(value.toExponential(p - 1).split("e")[1]);

		if (exponent < -4 || exponent >= p) {
			let [mantissa, exp] = value.toExponential(p - 1).split("e");

			if (mantissa.includes(".")) {
				mantissa = mantissa.replace(/\.?0+$/, "");
			}

			const sign = exp[0] === "-" ? "-" : "+";
			const digits = exp.replace(/^[+-]/, "").padStart(2, "0");

			text = `${mantissa}e${sign}${digits}`;
		} else {
			text = value.toFixed(Math.max(p - 1 - exponent, 0));

			if (text.includes(".")) {
				text = text.replace(/\.?0+$/, "");
			}
		}
	}

	return text;
}

// The functions a module imports from the page: the math functions of Math,
// %g, and the console. `memory()` gives the memory of the module once it is
// made; `write` gets what the module prints.
export function moduleImports(memory, write) {
	const bytes = () => new Uint8Array(memory().buffer);

	return {
		env: {
			sin: Math.sin,
			cos: Math.cos,
			tan: Math.tan,
			asin: Math.asin,
			acos: Math.acos,
			atan: Math.atan,
			atan2: Math.atan2,
			sinh: Math.sinh,
			cosh: Math.cosh,
			tanh: Math.tanh,
			asinh: Math.asinh,
			acosh: Math.acosh,
			atanh: Math.atanh,
			exp: Math.exp,
			log: Math.log,
			log10: Math.log10,
			pow: Math.pow,
			format_double(value, conv, precision, buffer, capacity) {
				const text =
					conv === 102
						? formatF(value, precision)
						: formatG(value, precision);
				const data = encoder.encode(text);
				const n = Math.min(data.length, capacity);

				bytes().set(data.subarray(0, n), buffer);
				return n;
			},
			console_write(text, length) {
				write(decoder.decode(bytes().subarray(text, text + length)));
			},
		},
	};
}

export async function loadModule(url, write) {
	let memory = null;
	const response = await fetch(url);
	const { instance } = await WebAssembly.instantiate(
		await response.arrayBuffer(),
		moduleImports(() => memory, write),
	);

	memory = instance.exports.memory;
	return instance.exports;
}

// The library as the page uses it: sessions, and the services of the editor
// (see include/meth/web/web.h for what each answer holds). `loadMeth` gives the
// first session with `create ()` for more (one for each open file); every
// method that can fail throws an Error with the number of the failure in
// `code` (1 is out of memory, 2 an invalid index or name, 5 a file that cannot
// be read, 6 a text that is not a formula).
const FAILURES = {
	1: "out of memory",
	2: "invalid argument",
	5: "cannot read",
	6: "not a formula",
};

// type is replace without an undo step of its own (for text that is being
// typed); checkpoint ends the step, so a burst of typing is undone as one.
export const EDIT = {
	replace: 0,
	insert: 1,
	delete: 2,
	undo: 3,
	redo: 4,
	type: 5,
	checkpoint: 6,
	// or'ed into replace, insert or delete: the change belongs to the undo
	// step of the one before it (Notebook.grouped uses it)
	join: 16,
};

export async function loadMeth(url) {
	const api = await loadModule(url, () => {});
	const bytes = () => new Uint8Array(api.memory.buffer);

	// The methods of the session with this handle.
	function sessionOf(handle) {
		// Puts the texts in the memory of the module, calls `name` with them
		// (each text is a pointer and a length) and the numbers around them,
		// and reads the answer. `args` holds numbers and strings in call order.
		function call(name, args, parse) {
			const allocated = [];
			const values = [handle];

			for (const arg of args) {
				if (typeof arg === "string") {
					const data = encoder.encode(arg);
					const at = api.meth_alloc(data.length);

					bytes().set(data, at);
					allocated.push(at);
					values.push(at, data.length);
				} else {
					values.push(arg);
				}
			}

			const code = api[name](...values);

			for (const at of allocated) {
				api.meth_free(at);
			}

			if (code !== 0 && !name.endsWith("eval")) {
				const error = new Error(FAILURES[code] ?? `failure ${code}`);

				error.code = code;
				throw error;
			}

			const start = api.meth_result_ptr(handle);
			const text = decoder.decode(
				bytes().subarray(start, start + api.meth_result_len(handle)),
			);

			return parse ? JSON.parse(text) : { code, text };
		}

		const json = (name, ...args) => call(name, args, true);
		const text = (name, ...args) => call(name, args, false).text;

		return {
			handle,

			// Ends the session; its methods must not be used afterwards.
			destroy: () => api.meth_destroy(handle),

			// `meth eval --format json EXPR` (a session of its own): the exit
			// code and the output text.
			evaluate: (expression) => call("meth_eval", [expression], false),

			// Replaces the session by a .meth file; every method that changes
			// the session answers { from, count, lines } like lines().
			load: (file) => json("meth_load", file),
			save: () => text("meth_save"),
			lines: (from = 0) => json("meth_lines", from),
			edit: (op, index, kind = "", domain = "", line = "") =>
				json("meth_edit", op, index, kind, domain, line),
			setSymbols: (on) => json("meth_set_symbols", on ? 1 : 0),

			// The services of the editor.
			highlight: (domain, index, line, errStart = 0, errLength = 0) =>
				json("meth_highlight", domain, index, line, errStart, errLength),
			ghost: (domain, index, line) => json("meth_ghost", domain, index, line),
			complete: (domain, index, line) =>
				json("meth_complete", domain, index, line),
			steps: (index) => json("meth_steps", index),

			// The text of a formula (or of a value) as MathML: a <math>
			// element as text. Throws (code 6) when the text is not a formula.
			typeset: (domain, formula) => text("meth_typeset", domain, formula),
			hint: (index) => json("meth_hint", index),
			exportAs: (format, words = "") => text("meth_export", format, words),
			domains: () => json("meth_domains"),

			// Help, examples, snippets, the tutorial and the themes.
			docs: () => json("meth_docs"),
			doc: (id) => json("meth_doc", id),
			searchDocs: (query) => json("meth_docs_search", query),
			examples: () => json("meth_examples"),
			example: (name) => text("meth_example", name),
			snippets: () => json("meth_snippets"),
			snippet: (name) => text("meth_snippet", name),
			lessons: () => json("meth_lessons"),
			themes: () => json("meth_themes"),
			themeFrom: (file) => json("meth_theme_text", file),
		};
	}

	const first = api.meth_create();

	if (first === 0) {
		throw new Error("out of memory");
	}

	return {
		...sessionOf(first),

		// Another session, for another file. Throws when there is no room.
		create() {
			const handle = api.meth_create();

			if (handle === 0) {
				const error = new Error("too many open sessions");

				error.code = 1;
				throw error;
			}

			return sessionOf(handle);
		},
	};
}
