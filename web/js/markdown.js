// The Markdown of a note, as much as the export knows: headings (#), rules
// (---), lists (- ), paragraphs, `code`, **bold** and *italic*. The text is
// never taken as HTML: it is put in elements with textContent.

function span(parent, text) {
	// inline marks: `code`, **bold**, *italic*
	const pattern = /`([^`]+)`|\*\*([^*]+)\*\*|\*([^*\s][^*]*)\*/g;
	let last = 0;

	for (const match of text.matchAll(pattern)) {
		if (match.index > last) {
			parent.append(text.slice(last, match.index));
		}

		const [, code, bold, italic] = match;
		const element = document.createElement(
			code !== undefined ? "code" : bold !== undefined ? "strong" : "em",
		);

		element.textContent = code ?? bold ?? italic;
		parent.append(element);
		last = match.index + match[0].length;
	}

	if (last < text.length) {
		parent.append(text.slice(last));
	}
}

// Fills `root` with the rendering of the note lines.
export function renderNote(root, lines) {
	root.replaceChildren();

	let paragraph = null;
	let list = null;

	for (const line of lines) {
		const heading = /^(#{1,3})\s+(.*)$/.exec(line);

		if (heading) {
			const h = document.createElement(`h${heading[1].length}`);

			span(h, heading[2]);
			root.append(h);
			paragraph = null;
			list = null;
		} else if (/^([-*_])\1{2,}\s*$/.test(line)) {
			root.append(document.createElement("hr"));
			paragraph = null;
			list = null;
		} else if (/^\s*[-*]\s+/.test(line)) {
			if (!list) {
				list = document.createElement("ul");
				root.append(list);
			}

			const item = document.createElement("li");

			span(item, line.replace(/^\s*[-*]\s+/, ""));
			list.append(item);
			paragraph = null;
		} else if (line.trim() === "") {
			paragraph = null;
			list = null;
		} else {
			if (!paragraph) {
				paragraph = document.createElement("p");
				root.append(paragraph);
			} else {
				paragraph.append(" ");
			}

			span(paragraph, line);
			list = null;
		}
	}
}

// The same inline marks for a short text (help pages): also [words](id)
// links, which call `onLink(id)`.
export function inline(parent, text, onLink) {
	const pattern = /\[([^\]]+)\]\(([^)]+)\)/g;
	let last = 0;

	for (const match of text.matchAll(pattern)) {
		if (match.index > last) {
			span(parent, text.slice(last, match.index));
		}

		const link = document.createElement("a");

		link.textContent = match[1];
		link.href = `#${match[2]}`;
		link.addEventListener("click", (event) => {
			event.preventDefault();
			onLink(match[2]);
		});
		parent.append(link);
		last = match.index + match[0].length;
	}

	if (last < text.length) {
		span(parent, text.slice(last));
	}
}
