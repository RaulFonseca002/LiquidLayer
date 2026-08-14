// Headless regression for the Solid Scope renderer self-test.
//
// Runs apps/visualizer/app.js inside a minimal hand-rolled DOM shim (no
// packages, no browser) with ?selftest=1, which replays the canonical
// full-loop trace fixture through the real applyEvent pipeline and throws
// if any lane, lamp, rail, or terminal state disagrees with the recorded
// evidence. Passing means the renderer agrees with the trace v2 contract.
"use strict";

const fs = require("fs");
const path = require("path");
const vm = require("vm");

const visualizerRoot = path.join(__dirname, "..", "apps", "visualizer");
const appSource = fs.readFileSync(path.join(visualizerRoot, "app.js"), "utf8");
const indexSource = fs.readFileSync(path.join(visualizerRoot, "index.html"), "utf8");

class MiniNode {
    constructor(tagName) {
        this.tagName = String(tagName || "div").toUpperCase();
        this.children = [];
        this.ownText = "";
        this.dataset = {};
        this.attributes = {};
        this.hidden = false;
        this.disabled = false;
        this.value = "";
        this.scrollTop = 0;
        // className and classList stay in sync so shim querySelector sees
        // classes assigned either way, as a browser would.
        const classes = new Set();
        Object.defineProperty(this, "className", {
            get: () => [...classes].join(" "),
            set: (next) => {
                classes.clear();
                String(next).split(/\s+/).filter(Boolean).forEach((name) => classes.add(name));
            }
        });
        this.classList = {
            add: (name) => { classes.add(name); },
            remove: (name) => { classes.delete(name); },
            contains: (name) => classes.has(name)
        };
        const styleProperties = new Map();
        this.style = {
            setProperty: (name, propertyValue) => { styleProperties.set(name, String(propertyValue)); },
            getPropertyValue: (name) => styleProperties.get(name) || "",
            removeProperty: (name) => { styleProperties.delete(name); }
        };
    }

    get textContent() {
        return this.ownText + this.children.map((child) => child.textContent).join("");
    }

    set textContent(next) {
        this.ownText = next === undefined || next === null ? "" : String(next);
        this.children.length = 0;
    }

    get offsetWidth() { return 0; }

    appendChild(child) {
        this.children.push(child);
        child.parentNode = this;
        return child;
    }

    append(...next) {
        next.forEach((child) => this.appendChild(child));
    }

    replaceChildren(...next) {
        this.children.length = 0;
        next.forEach((child) => this.appendChild(child));
    }

    remove() {
        if (this.parentNode) {
            const siblings = this.parentNode.children;
            const index = siblings.indexOf(this);
            if (index >= 0) siblings.splice(index, 1);
            this.parentNode = null;
        }
    }

    querySelector(selector) {
        if (!selector.startsWith("."))
            throw new Error(`The harness only supports class selectors, got: ${selector}`);
        const className = selector.slice(1);
        for (const child of this.children) {
            if (child.classList.contains(className)) return child;
            const nested = child.querySelector(selector);
            if (nested) return nested;
        }
        return null;
    }

    setAttribute(name, attributeValue) { this.attributes[name] = String(attributeValue); }
    getAttribute(name) {
        return Object.prototype.hasOwnProperty.call(this.attributes, name)
            ? this.attributes[name]
            : null;
    }
    removeAttribute(name) { delete this.attributes[name]; }
    addEventListener() {}
    removeEventListener() {}
    focus() {}
}

// Elements are created on demand so the shim never drifts from the ids the
// application actually asks for.
const registry = new Map();
function elementById(id) {
    if (!registry.has(id)) {
        const node = new MiniNode("div");
        node.id = id;
        registry.set(id, node);
    }
    return registry.get(id);
}

// The phase rail is harvested from the real index.html so the harness fails
// when the markup and the renderer disagree about the frame phases.
const phaseItems = [];
const phasePattern = /data-phase="([a-z]+)"[^>]*>.*?<span>([^<]+)<\/span>/g;
for (let match = phasePattern.exec(indexSource); match; match = phasePattern.exec(indexSource)) {
    const item = new MiniNode("li");
    item.dataset.phase = match[1];
    item.textContent = match[2];
    phaseItems.push(item);
}
if (phaseItems.length === 0)
    throw new Error("The harness could not harvest phase rail items from index.html.");

const tokenMeta = new MiniNode("meta");
tokenMeta.setAttribute("content", "harness-token");
tokenMeta.content = "harness-token";

const body = new MiniNode("body");
const documentShim = {
    body,
    getElementById: elementById,
    createElement: (tagName) => new MiniNode(tagName),
    querySelector: (selector) => (selector === 'meta[name="solid-scope-token"]' ? tokenMeta : null),
    querySelectorAll: (selector) => (selector === "#phase-rail li" ? phaseItems.slice() : [])
};

const windowShim = {
    location: { search: "?selftest=1" },
    matchMedia: () => ({ matches: true, addEventListener() {}, addListener() {} }),
    addEventListener() {},
    setTimeout: () => 0,
    clearTimeout() {}
};

const context = {
    window: windowShim,
    document: documentShim,
    navigator: { platform: "Linux", userAgentData: undefined },
    URLSearchParams,
    console,
    TextDecoder,
    fetch: () => Promise.reject(new Error("fetch is not available in the harness"))
};
context.globalThis = context;

vm.runInNewContext(appSource, context, { filename: "app.js" });

if (body.dataset.selftest !== "passed")
    throw new Error(`Solid Scope self-test did not pass (state: ${body.dataset.selftest || "not reached"}).`);

console.log("visualizer self-test passed headlessly");
