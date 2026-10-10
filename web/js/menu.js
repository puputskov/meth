// A context menu: a list of commands at a point of the page. It closes when
// something else is clicked, on Escape, or when a command is chosen. Arrow keys
// move through the commands, Enter chooses.

export class ContextMenu {
	constructor() {
		this.element = document.createElement("div");
		this.element.className = "context-menu";
		this.element.setAttribute("role", "menu");
		this.element.hidden = true;
		document.body.append(this.element);

		const close = (event) => {
			if (!this.element.hidden && !this.element.contains(event.target)) {
				this.hide();
			}
		};

		document.addEventListener("mousedown", close, true);
		window.addEventListener("blur", () => this.hide());
		window.addEventListener("resize", () => this.hide());
		this.element.addEventListener("keydown", (event) => this.keydown(event));
	}

	// items: [{ label, action, danger, disabled }] or "-" for a line between.
	show(x, y, items) {
		this.element.replaceChildren();

		for (const item of items) {
			if (item === "-") {
				const rule = document.createElement("hr");

				this.element.append(rule);
				continue;
			}

			const button = document.createElement("button");

			button.type = "button";
			button.setAttribute("role", "menuitem");
			button.textContent = item.label;
			button.disabled = Boolean(item.disabled);
			button.classList.toggle("danger", Boolean(item.danger));
			button.addEventListener("click", () => {
				this.hide();
				item.action();
			});
			this.element.append(button);
		}

		this.element.hidden = false;

		// keep the menu inside the window
		const box = this.element.getBoundingClientRect();

		this.element.style.left = `${Math.max(4, Math.min(x, window.innerWidth - box.width - 4))}px`;
		this.element.style.top = `${Math.max(4, Math.min(y, window.innerHeight - box.height - 4))}px`;
		this.element.querySelector("button:not(:disabled)")?.focus();
	}

	hide() {
		if (!this.element.hidden) {
			this.element.hidden = true;
			this.element.replaceChildren();
		}
	}

	get isOpen() {
		return !this.element.hidden;
	}

	keydown(event) {
		const buttons = [...this.element.querySelectorAll("button:not(:disabled)")];
		const at = buttons.indexOf(document.activeElement);

		if (event.key === "Escape") {
			event.preventDefault();
			this.hide();
		} else if (event.key === "ArrowDown") {
			event.preventDefault();
			buttons[(at + 1) % buttons.length]?.focus();
		} else if (event.key === "ArrowUp") {
			event.preventDefault();
			buttons[(at - 1 + buttons.length) % buttons.length]?.focus();
		}
	}
}
