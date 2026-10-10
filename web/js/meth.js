// Loads meth.wasm and wraps its C API (source/web/api.c). The module has no
// imports besides the math functions of the page and format_g, and the page
// reads and writes text in its memory only through meth_alloc and the result
// pointer, so nothing here knows a structure of the library.

const encoder = new TextEncoder();
const decoder = new TextDecoder();

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

export async function loadMeth(url) {
	let memory = null;
	const bytes = () => new Uint8Array(memory.buffer);

	const imports = {
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
			format_g(value, precision, buffer, capacity) {
				const data = encoder.encode(formatG(value, precision));
				const n = Math.min(data.length, capacity);

				bytes().set(data.subarray(0, n), buffer);
				return n;
			},
		},
	};

	const response = await fetch(url);
	const { instance } = await WebAssembly.instantiate(
		await response.arrayBuffer(),
		imports,
	);
	const api = instance.exports;

	memory = api.memory;

	return {
		// `meth eval --format json EXPR`: the exit code and the output text.
		evaluate(expression) {
			const data = encoder.encode(expression);
			const at = api.meth_alloc(Math.max(data.length, 1));

			bytes().set(data, at);

			const code = api.meth_eval(at, data.length);
			const text = decoder.decode(
				bytes().subarray(
					api.meth_result_ptr(),
					api.meth_result_ptr() + api.meth_result_len(),
				),
			);

			api.meth_free(at);
			return { code, text };
		},
	};
}
