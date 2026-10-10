// The plot data of a result (kind, lo, hi, xs, ys) as an SVG picture.

const NS = "http://www.w3.org/2000/svg";
const WIDTH = 420;
const HEIGHT = 220;
const PAD = { left: 44, right: 12, top: 10, bottom: 28 };

function element(name, attributes = {}, text) {
	const e = document.createElementNS(NS, name);

	for (const [key, value] of Object.entries(attributes)) {
		e.setAttribute(key, String(value));
	}

	if (text !== undefined) {
		e.textContent = text;
	}

	return e;
}

function nice(value) {
	return Number(value.toPrecision(4)).toString();
}

function range(values) {
	const finite = values.filter((v) => v !== null && Number.isFinite(v));
	let lo = Math.min(...finite);
	let hi = Math.max(...finite);

	if (!(hi > lo)) {
		lo -= 1;
		hi += 1;
	}

	return [lo, hi];
}

export function plotSvg(plot) {
	const svg = element("svg", {
		class: "plot",
		viewBox: `0 0 ${WIDTH} ${HEIGHT}`,
		role: "img",
		"aria-label": `${plot.kind} plot`,
	});
	const w = WIDTH - PAD.left - PAD.right;
	const h = HEIGHT - PAD.top - PAD.bottom;
	let [xlo, xhi] = [plot.lo, plot.hi];
	let [ylo, yhi] = [0, 1];
	const ys = plot.ys.map((y) => (y === null ? NaN : y));

	if (plot.kind === "histogram") {
		yhi = Math.max(1, ...ys.filter(Number.isFinite));
	} else {
		[xlo, xhi] = range(plot.xs);
		[ylo, yhi] = range(plot.ys);
	}

	const x = (v) => PAD.left + ((v - xlo) / (xhi - xlo)) * w;
	const y = (v) => PAD.top + h - ((v - ylo) / (yhi - ylo)) * h;

	svg.append(
		element("path", {
			class: "axis",
			d: `M${PAD.left} ${PAD.top}V${PAD.top + h}H${PAD.left + w}`,
		}),
		element("text", { x: PAD.left - 6, y: y(ylo) + 4, "text-anchor": "end" }, nice(ylo)),
		element("text", { x: PAD.left - 6, y: y(yhi) + 4, "text-anchor": "end" }, nice(yhi)),
		element("text", { x: PAD.left, y: HEIGHT - 8 }, nice(xlo)),
		element("text", { x: PAD.left + w, y: HEIGHT - 8, "text-anchor": "end" }, nice(xhi)),
	);

	if (plot.kind === "histogram") {
		const bar = w / Math.max(ys.length, 1);

		ys.forEach((count, i) => {
			if (Number.isFinite(count)) {
				svg.append(
					element("rect", {
						class: "mark",
						x: PAD.left + i * bar + 1,
						y: y(count),
						width: Math.max(bar - 2, 1),
						height: y(0) - y(count),
					}),
				);
			}
		});
	} else if (plot.kind === "line") {
		const points = ys
			.map((v, i) => [plot.xs[i], v])
			.filter(([px, py]) => Number.isFinite(px) && Number.isFinite(py));

		svg.append(
			element("path", {
				class: "path",
				d: points.map(([px, py], i) => `${i ? "L" : "M"}${x(px)} ${y(py)}`).join(""),
			}),
		);
	} else {
		ys.forEach((v, i) => {
			if (Number.isFinite(v) && Number.isFinite(plot.xs[i])) {
				svg.append(element("circle", { class: "mark", cx: x(plot.xs[i]), cy: y(v), r: 3 }));
			}
		});
	}

	return svg;
}
