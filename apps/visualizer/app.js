(function () {
    "use strict";

    const TRACE_SCHEMA = "liquid.trace.v2";
    const MAX_STREAM_BUFFER = 1024 * 1024;
    const MAX_QUEUED_EVENTS = 10000;
    const reducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)");
    const platform = navigator.userAgentData && navigator.userAgentData.platform
        ? navigator.userAgentData.platform
        : navigator.platform;
    const runShortcut = /Mac|iPhone|iPad|iPod/i.test(platform || "") ? "⌘↵" : "Ctrl+Enter";

    const elements = {
        body: document.body,
        token: document.querySelector('meta[name="solid-scope-token"]'),
        form: document.getElementById("scenario-form"),
        initialBrightness: document.getElementById("initial-brightness"),
        frameTimes: document.getElementById("frame-times"),
        feedbackTiming: document.getElementById("feedback-timing"),
        latencyMs: document.getElementById("latency-ms"),
        adapterOutcome: document.getElementById("adapter-outcome"),
        duplicateReports: document.getElementById("duplicate-reports"),
        silentAdapter: document.getElementById("silent-adapter"),
        reverseDelivery: document.getElementById("reverse-delivery"),
        scriptSource: document.getElementById("script-source"),
        editorShell: document.getElementById("editor-shell"),
        editorGutter: document.getElementById("editor-gutter"),
        formMessage: document.getElementById("form-message"),
        runButton: document.getElementById("run-button"),
        headerRunButton: document.getElementById("header-run-button"),
        stopButton: document.getElementById("stop-button"),
        pauseButton: document.getElementById("pause-button"),
        stepButton: document.getElementById("step-button"),
        speedSelect: document.getElementById("speed-select"),
        connectionStatus: document.getElementById("connection-status"),
        connectionLabel: document.getElementById("connection-label"),
        simulationTime: document.getElementById("simulation-time"),
        activeFrame: document.getElementById("active-frame"),
        scriptState: document.getElementById("script-state"),
        scriptStateLabel: document.getElementById("script-state-label"),
        playbackStatus: document.getElementById("playback-status"),
        eventCounter: document.getElementById("event-counter"),
        phaseItems: Array.from(document.querySelectorAll("#phase-rail li")),
        actualValue: document.getElementById("actual-value"),
        actualLamp: document.getElementById("actual-lamp"),
        actualDetail: document.getElementById("actual-detail"),
        selectedValue: document.getElementById("selected-value"),
        selectedDetail: document.getElementById("selected-detail"),
        commandValue: document.getElementById("command-value"),
        commandDetail: document.getElementById("command-detail"),
        resultValue: document.getElementById("result-value"),
        resultDetail: document.getElementById("result-detail"),
        deviceValue: document.getElementById("device-value"),
        deviceLamp: document.getElementById("device-lamp"),
        deviceDetail: document.getElementById("device-detail"),
        intentList: document.getElementById("intent-list"),
        intentCount: document.getElementById("intent-count"),
        frameLedger: document.getElementById("frame-ledger"),
        healthBadge: document.getElementById("health-badge"),
        runtimeHealth: document.getElementById("runtime-health"),
        transportHealth: document.getElementById("transport-health"),
        lastSequence: document.getElementById("last-sequence"),
        diagnosticPanel: document.getElementById("diagnostic-panel"),
        diagnosticText: document.getElementById("diagnostic-text"),
        liveRegion: document.getElementById("live-region")
    };

    const state = {
        abortController: null,
        reader: null,
        queue: [],
        queueHead: 0,
        lastEnqueuedSequence: -1,
        playbackTimer: null,
        playing: true,
        transportDone: false,
        terminal: false,
        userStopped: false,
        eventCount: 0,
        intents: new Map(),
        frames: new Map(),
        frameRows: new Map(),
        currentFrameKey: null,
        selectedIntentId: null,
        generation: 0
    };

    function firstDefined() {
        for (const value of arguments) {
            if (value !== undefined && value !== null) return value;
        }
        return undefined;
    }

    function eventType(event) {
        return typeof event.event === "string" ? event.event : "";
    }

    function eventSequence(event) {
        return event.seq;
    }

    function eventFrame(event) {
        return event.frame;
    }

    function eventTime(event) {
        return event.now_ms;
    }

    function eventDiagnostic(event) {
        return event.diagnostic;
    }

    function eventBrightness(event) {
        return firstDefined(event.actual_brightness, event.desired_brightness);
    }

    function eventIntent(event) {
        if (!Number.isSafeInteger(event.intent_id) || !Number.isSafeInteger(event.desired_brightness))
            throw new Error("The trace contained incomplete intent evidence.");
        if (typeof event.priority !== "string" || typeof event.lifetime !== "string")
            throw new Error("The trace contained invalid intent metadata.");

        let lifetimeLabel = event.lifetime;
        if (event.expires_at_ms !== undefined)
            lifetimeLabel += ` · expires ${event.expires_at_ms} ms`;
        return {
            id: String(event.intent_id),
            brightness: event.desired_brightness,
            priority: event.priority,
            lifetime: String(lifetimeLabel),
            live: true
        };
    }

    function setText(element, value) {
        const next = value === undefined || value === null || value === "" ? "—" : String(value);
        if (element.dataset && element.dataset.tick === "true" && element.textContent !== next) {
            element.classList.remove("value-tick");
            void element.offsetWidth;
            element.classList.add("value-tick");
        }
        element.textContent = next;
    }

    function setLamp(lamp, brightness) {
        if (!lamp) return;
        if (typeof brightness === "number" && Number.isFinite(brightness)) {
            lamp.dataset.known = "true";
            lamp.style.setProperty("--lamp-level", `${Math.max(0, Math.min(100, brightness))}%`);
        } else {
            delete lamp.dataset.known;
            lamp.style.removeProperty("--lamp-level");
        }
    }

    function setConnection(label, status) {
        elements.connectionStatus.dataset.state = status;
        elements.connectionLabel.textContent = label;
    }

    function setScriptState(label, status) {
        elements.scriptState.dataset.state = status;
        elements.scriptStateLabel.textContent = label;
        if (status === "executing") elements.editorShell.dataset.executing = "true";
        else delete elements.editorShell.dataset.executing;
    }

    function setHealth(label, status) {
        elements.healthBadge.dataset.state = status;
        elements.healthBadge.textContent = label;
    }

    function announce(message) {
        elements.liveRegion.textContent = "";
        window.setTimeout(function () {
            elements.liveRegion.textContent = String(message).slice(0, 240);
        }, 0);
    }

    function showDiagnostic(message) {
        if (message === undefined || message === null || message === "") {
            elements.diagnosticPanel.hidden = true;
            elements.diagnosticText.textContent = "";
            return;
        }
        elements.diagnosticText.textContent = String(message).slice(0, 8192);
        elements.diagnosticPanel.hidden = false;
    }

    function setPhaseState(phases) {
        const recorded = new Set();
        (phases || []).forEach(function (phase) {
            const normalized = String(phase).toLowerCase();
            if (normalized === "run_systems") {
                // Legacy single-phase traces recorded all system work at once.
                recorded.add("input");
                recorded.add("behavior");
                recorded.add("decision");
                return;
            }
            recorded.add(({
                begin_frame: "begin",
                expire_intents: "expire",
                run_input_systems: "input",
                run_behavior_systems: "behavior",
                run_decision_systems: "decision",
                resolve_intents: "resolve",
                end_frame: "end"
            })[normalized] || normalized);
        });
        elements.phaseItems.forEach(function (item) {
            const name = item.dataset.phase;
            const label = item.textContent.trim();
            if (recorded.has(name)) {
                item.dataset.state = "recorded";
                item.setAttribute("aria-label", `${label}, recorded`);
            } else {
                delete item.dataset.state;
                item.setAttribute("aria-label", `${label}, waiting`);
            }
        });
    }

    function renderIntents() {
        elements.intentList.replaceChildren();
        if (state.intents.size === 0) {
            const empty = document.createElement("li");
            empty.className = "empty-evidence";
            empty.textContent = "No committed intents were observed.";
            elements.intentList.append(empty);
            elements.intentCount.textContent = "0 live";
            return;
        }

        let liveCount = 0;
        state.intents.forEach(function (intent) {
            if (intent.live) liveCount += 1;
            const item = document.createElement("li");
            item.className = "intent-item";
            item.dataset.live = String(intent.live);
            item.dataset.selected = String(intent.id === state.selectedIntentId);

            const id = document.createElement("strong");
            id.textContent = `Intent ${intent.id}`;
            const value = document.createElement("span");
            value.textContent = intent.brightness === undefined ? "value —" : `${intent.brightness}%`;
            const meta = document.createElement("span");
            meta.className = "intent-meta";
            meta.textContent = `${intent.priority} priority · ${intent.lifetime}${intent.live ? "" : " · disappeared"}`;
            item.append(id, value, meta);
            elements.intentList.append(item);
        });
        elements.intentCount.textContent = `${liveCount} live`;
    }

    function ensureFrame(event) {
        const frame = eventFrame(event);
        const time = eventTime(event);
        if (!Number.isSafeInteger(frame) || !Number.isSafeInteger(time))
            throw new Error("The trace contained incomplete frame evidence.");
        const key = String(frame);
        if (!state.frames.has(key)) {
            state.frames.set(key, {
                frame: frame,
                time: time,
                script: "—",
                expired: "—",
                selected: "—"
            });
        }
        const entry = state.frames.get(key);
        if (time !== undefined) entry.time = time;
        state.currentFrameKey = key;
        return entry;
    }

    function currentFrame() {
        if (state.currentFrameKey === null) return null;
        return state.frames.get(state.currentFrameKey) || null;
    }

    function resetFrames() {
        elements.frameLedger.replaceChildren();
        state.frameRows.clear();
        if (state.frames.size === 0) {
            const row = document.createElement("tr");
            row.className = "empty-row";
            const cell = document.createElement("td");
            cell.colSpan = 5;
            cell.textContent = "Completed frames will appear here.";
            row.append(cell);
            elements.frameLedger.append(row);
        }
    }

    function updateFrameRow(frame) {
        const empty = elements.frameLedger.querySelector(".empty-row");
        if (empty) empty.remove();
        let rowState = state.frameRows.get(String(frame.frame));
        if (!rowState) {
            const row = document.createElement("tr");
            const cells = [];
            for (let index = 0; index < 5; index += 1) {
                const cell = document.createElement("td");
                row.append(cell);
                cells.push(cell);
            }
            elements.frameLedger.append(row);
            rowState = { cells: cells };
            state.frameRows.set(String(frame.frame), rowState);
        }
        const values = [
            frame.frame,
            frame.time === undefined ? "—" : `${frame.time} ms`,
            frame.script,
            frame.expired,
            frame.selected
        ];
        values.forEach(function (value, index) {
            rowState.cells[index].textContent = String(value);
        });
    }

    function eventPhases(event) {
        if (!Array.isArray(event.phases) || event.phases.some(function (phase) { return typeof phase !== "string"; }))
            throw new Error("The trace contained invalid phase evidence.");
        return event.phases;
    }

    function applyEvent(event) {
        const type = eventType(event);
        const sequence = eventSequence(event);
        const time = eventTime(event);

        state.eventCount += 1;
        elements.eventCounter.textContent = `${state.eventCount} ${state.eventCount === 1 ? "event" : "events"}`;
        if (sequence !== undefined) setText(elements.lastSequence, sequence);
        if (time !== undefined) setText(elements.simulationTime, `${time} ms`);

        switch (type) {
        case "run_started":
            elements.body.dataset.runState = "running";
            elements.playbackStatus.textContent = "Trace received · replaying evidence";
            elements.runtimeHealth.textContent = "Running";
            setConnection("Receiving trace", "streaming");
            setHealth("Running", "streaming");
            break;

        case "world_ready": {
            const brightness = eventBrightness(event);
            if (brightness !== undefined) setText(elements.actualValue, brightness);
            setLamp(elements.actualLamp, brightness);
            elements.actualDetail.textContent = "Light.officeLight · world initialization";
            break;
        }

        case "frame_started": {
            const frame = ensureFrame(event);
            setText(elements.activeFrame, `#${frame.frame}`);
            state.selectedIntentId = null;
            elements.selectedValue.textContent = "—";
            elements.selectedDetail.textContent = "Awaiting this frame's resolution evidence.";
            setPhaseState([]);
            renderIntents();
            updateFrameRow(frame);
            break;
        }

        case "script_started": {
            const frame = ensureFrame(event);
            frame.script = "Executing";
            setScriptState("Executing script", "executing");
            elements.playbackStatus.textContent = "Lua execution boundary is active";
            updateFrameRow(frame);
            announce("Lua script execution started.");
            break;
        }

        case "script_finished": {
            const frame = ensureFrame(event);
            const rawStatus = typeof event.status === "string" ? event.status.toLowerCase() : "missing";
            const failed = rawStatus !== "success" && rawStatus !== "completed" && rawStatus !== "ok";
            frame.script = failed ? "Failed · rolled back" : "Completed";
            setScriptState(failed ? "Failed · rolled back" : "Execution complete", failed ? "failed" : "complete");
            showDiagnostic(eventDiagnostic(event));
            updateFrameRow(frame);
            announce(failed ? "Lua script failed. Its proposals were rolled back." : "Lua script execution completed.");
            break;
        }

        case "desire_created": {
            const intent = eventIntent(event);
            state.intents.set(intent.id, intent);
            renderIntents();
            break;
        }

        case "frame_completed": {
            const frame = ensureFrame(event);
            if (!Number.isSafeInteger(event.expired_intents))
                throw new Error("The trace contained invalid expiration evidence.");
            frame.expired = event.expired_intents;
            setPhaseState(eventPhases(event));
            elements.playbackStatus.textContent = `Frame ${frame.frame} recorded · resolution available`;
            updateFrameRow(frame);
            break;
        }

        case "desire_selected": {
            const intent = eventIntent(event);
            const selectedId = intent.id;
            if (!state.intents.has(selectedId)) state.intents.set(selectedId, intent);
            state.selectedIntentId = selectedId;
            const brightness = eventBrightness(event);
            const selectedBrightness = firstDefined(brightness, intent.brightness);
            setText(elements.selectedValue, selectedBrightness);
            elements.selectedDetail.textContent = `Intent ${selectedId} · ${intent.priority} priority · desire only`;
            const frame = currentFrame() || ensureFrame(event);
            frame.selected = selectedBrightness === undefined ? `Intent ${selectedId}` : `${selectedBrightness}% · ${selectedId}`;
            renderIntents();
            updateFrameRow(frame);
            break;
        }

        case "command_issued": {
            const data = event.data || {};
            setText(elements.commandValue, data.desired);
            elements.commandDetail.textContent = `Command ${firstDefined(data.command_id, "—")} · ${firstDefined(data.route, "route —")} → ${firstDefined(data.target, "target —")}`;
            break;
        }

        case "command_attempt":
        case "command_retry": {
            const data = event.data || {};
            const action = type === "command_retry" ? "retry" : "attempt";
            elements.commandDetail.textContent = `Command ${firstDefined(data.command_id, "—")} · ${action} ${firstDefined(data.attempt, "—")}`;
            break;
        }

        case "command_result": {
            const data = event.data || {};
            setText(elements.resultValue, firstDefined(data.observed, data.status));
            elements.resultDetail.textContent = `Command ${firstDefined(data.command_id, "—")} · ${firstDefined(data.disposition, data.status, "reported")}`;
            break;
        }

        case "command_timeout": {
            const data = event.data || {};
            elements.resultValue.textContent = "timeout";
            elements.resultDetail.textContent = `Command ${firstDefined(data.command_id, "—")} exhausted its retry window.`;
            break;
        }

        case "observed_changed": {
            const data = event.data || {};
            const observed = firstDefined(data.observed, data.value);
            setText(elements.actualValue, observed);
            if (typeof observed === "number") setLamp(elements.actualLamp, observed);
            elements.actualDetail.textContent = `Authoritative ${firstDefined(data.route, "route —")} · revision ${firstDefined(data.state_revision, "—")}`;
            break;
        }

        case "command_status": {
            const data = event.data || {};
            elements.resultDetail.textContent = `Command ${firstDefined(data.command_id, "—")} · status ${firstDefined(data.status, data.value, "—")}`;
            break;
        }

        case "external_observation": {
            const data = event.data || {};
            const observed = firstDefined(data.observed, data.value);
            setText(elements.actualValue, observed);
            if (typeof observed === "number") setLamp(elements.actualLamp, observed);
            elements.actualDetail.textContent = `Unsolicited ${firstDefined(data.route, "route —")} · revision ${firstDefined(data.state_revision, "—")}`;
            elements.playbackStatus.textContent = "External observation accepted";
            break;
        }

        case "component_snapshot": {
            const brightness = eventBrightness(event);
            if (brightness !== undefined) setText(elements.actualValue, brightness);
            setLamp(elements.actualLamp, brightness);
            if (event.device_brightness !== undefined && event.device_brightness !== null) {
                setText(elements.deviceValue, event.device_brightness);
                setLamp(elements.deviceLamp, event.device_brightness);
            }
            elements.actualDetail.textContent = `Light.officeLight · projected at ${firstDefined(time, "—")} ms`;
            elements.deviceDetail.textContent = `Adapter state sampled at ${firstDefined(time, "—")} ms`;
            break;
        }

        case "desire_disappeared": {
            if (!Number.isSafeInteger(event.intent_id))
                throw new Error("The trace contained an invalid disappeared intent ID.");
            const intentId = String(event.intent_id);
            const known = state.intents.get(intentId);
            if (known) known.live = false;
            else throw new Error("The trace reported disappearance for an unknown intent.");
            if (state.selectedIntentId === intentId) state.selectedIntentId = null;
            renderIntents();
            break;
        }

        case "run_completed": {
            state.terminal = true;
            const scriptFailed = event.outcome === "script_error";
            elements.body.dataset.runState = scriptFailed ? "failed" : "complete";
            elements.playbackStatus.textContent = scriptFailed
                ? "Script failed · runtime remained healthy"
                : "Trace complete · deterministic evidence retained";
            elements.runtimeHealth.textContent = scriptFailed ? "Healthy · script isolated" : "Healthy";
            elements.transportHealth.textContent = "Complete";
            setConnection(scriptFailed ? "Script failed" : "Run complete", scriptFailed ? "failed" : "complete");
            setHealth(scriptFailed ? "Script failed" : "Healthy", scriptFailed ? "failed" : "complete");
            finishControls();
            announce(scriptFailed
                ? "Script failed and rolled back. Runtime remains healthy."
                : "Run complete. Runtime is healthy.");
            break;
        }

        case "run_failed":
            state.terminal = true;
            elements.body.dataset.runState = "failed";
            elements.playbackStatus.textContent = "Run failed · inspect the bounded diagnostic";
            elements.runtimeHealth.textContent = event.faulted === true ? "Faulted" : "Healthy · host boundary failed";
            elements.transportHealth.textContent = "Complete";
            setConnection("Run failed", "failed");
            setHealth(event.faulted === true ? "Runtime faulted" : "Host failure", "failed");
            showDiagnostic(eventDiagnostic(event));
            finishControls();
            announce("Host execution failed. A bounded diagnostic is available.");
            break;

        case "transport_failed":
            state.terminal = true;
            elements.body.dataset.runState = "failed";
            elements.playbackStatus.textContent = "Trace transport failed";
            elements.transportHealth.textContent = "Failed";
            setConnection("Transport failed", "failed");
            setHealth("Disconnected", "failed");
            showDiagnostic(eventDiagnostic(event));
            finishControls();
            announce("Trace transport failed. Check the local server and run again.");
            break;

        default:
            elements.playbackStatus.textContent = type ? `Recorded ${type}` : "Recorded an unknown event";
            break;
        }
    }

    function resetView() {
        window.clearTimeout(state.playbackTimer);
        state.playbackTimer = null;
        state.queue.length = 0;
        state.queueHead = 0;
        state.lastEnqueuedSequence = -1;
        state.playing = true;
        state.transportDone = false;
        state.terminal = false;
        state.userStopped = false;
        state.eventCount = 0;
        state.intents.clear();
        state.frames.clear();
        state.frameRows.clear();
        state.currentFrameKey = null;
        state.selectedIntentId = null;

        elements.body.dataset.runState = "idle";
        elements.form.setAttribute("aria-busy", "true");
        elements.eventCounter.textContent = "0 events";
        elements.simulationTime.textContent = "—";
        elements.activeFrame.textContent = "—";
        elements.actualValue.textContent = "—";
        elements.actualDetail.textContent = "No component snapshot received.";
        setLamp(elements.actualLamp, undefined);
        setLamp(elements.deviceLamp, undefined);
        elements.selectedValue.textContent = "—";
        elements.selectedDetail.textContent = "No intent has been selected.";
        elements.commandValue.textContent = "—";
        elements.commandDetail.textContent = "No command has been issued.";
        elements.resultValue.textContent = "—";
        elements.resultDetail.textContent = "No adapter result has arrived.";
        elements.deviceValue.textContent = "—";
        elements.deviceDetail.textContent = "No simulated device state is available.";
        elements.lastSequence.textContent = "—";
        elements.runtimeHealth.textContent = "Waiting";
        elements.transportHealth.textContent = "Connecting";
        elements.playbackStatus.textContent = "Opening a local trace";
        elements.pauseButton.textContent = "Pause playback";
        elements.pauseButton.disabled = false;
        elements.stepButton.disabled = true;
        elements.stopButton.disabled = false;
        setConnection("Connecting", "streaming");
        setHealth("Starting", "streaming");
        setScriptState("Not started", "idle");
        setPhaseState([]);
        showDiagnostic();
        renderIntents();
        resetFrames();
    }

    function finishControls() {
        elements.form.setAttribute("aria-busy", "false");
        elements.stopButton.disabled = true;
        elements.pauseButton.disabled = true;
        elements.stepButton.disabled = true;
    }

    function playbackDelay() {
        if (reducedMotion.matches) return 0;
        const speed = Number(elements.speedSelect.value) || 1;
        return Math.max(24, 260 / speed);
    }

    function queuedEventCount() {
        return state.queue.length - state.queueHead;
    }

    function dequeueEvent() {
        if (queuedEventCount() === 0) return undefined;
        const event = state.queue[state.queueHead];
        state.queueHead += 1;
        if (state.queueHead >= 1024 && state.queueHead * 2 >= state.queue.length) {
            state.queue.splice(0, state.queueHead);
            state.queueHead = 0;
        }
        return event;
    }

    function schedulePlayback() {
        if (!state.playing || state.playbackTimer !== null || queuedEventCount() === 0) return;
        const next = function () {
            state.playbackTimer = null;
            if (!state.playing || queuedEventCount() === 0) return;
            try {
                const batchSize = queuedEventCount() > 200 ? 50 : 1;
                for (let index = 0;
                    index < batchSize && queuedEventCount() > 0;
                    index += 1) {
                    applyEvent(dequeueEvent());
                }
            } catch (error) {
                localTransportFailure(error && error.message ? error.message : error);
                return;
            }
            updatePlaybackButtons();
            schedulePlayback();
        };
        const delay = queuedEventCount() > 200 ? 0 : playbackDelay();
        if (state.eventCount === 0) next();
        else state.playbackTimer = window.setTimeout(next, delay);
    }

    function enqueueEvent(event) {
        if (!event || typeof event !== "object" || Array.isArray(event)) {
            throw new Error("The trace contained a non-object event.");
        }
        if (event.schema !== TRACE_SCHEMA) {
            throw new Error(`Unsupported trace schema: ${String(event.schema)}`);
        }
        if (typeof event.event !== "string" || event.event === "") {
            throw new Error("The trace event name is missing.");
        }
        if (!Number.isSafeInteger(event.seq) || event.seq < 0 || event.seq <= state.lastEnqueuedSequence) {
            throw new Error("The trace sequence is not strictly increasing.");
        }
        if (state.lastEnqueuedSequence < 0 && event.seq !== 0) {
            throw new Error("The trace sequence must start at zero.");
        }
        if (queuedEventCount() >= MAX_QUEUED_EVENTS) {
            throw new Error("The trace exceeded the browser event queue limit.");
        }
        state.lastEnqueuedSequence = event.seq;
        state.queue.push(event);
        updatePlaybackButtons();
        schedulePlayback();
    }

    function updatePlaybackButtons() {
        if (state.terminal) return;
        elements.pauseButton.disabled = state.eventCount === 0 && queuedEventCount() === 0;
        elements.pauseButton.textContent = state.playing ? "Pause playback" : "Resume playback";
        elements.stepButton.disabled = state.playing || queuedEventCount() === 0;
    }

    function localTransportFailure(message) {
        if (state.userStopped) return;
        window.clearTimeout(state.playbackTimer);
        state.playbackTimer = null;
        state.queue.length = 0;
        state.queueHead = 0;
        applyEvent({ schema: TRACE_SCHEMA, event: "transport_failed", diagnostic: String(message) });
    }

    function parseScenario() {
        const brightness = Number(elements.initialBrightness.value);
        const latencyMs = Number(elements.latencyMs.value);
        const duplicateReports = Number(elements.duplicateReports.value);
        const rawTimes = elements.frameTimes.value.split(",");
        const frameTimes = rawTimes.map(function (part) {
            const value = part.trim();
            return value === "" ? NaN : Number(value);
        });

        elements.initialBrightness.removeAttribute("aria-invalid");
        elements.frameTimes.removeAttribute("aria-invalid");
        elements.scriptSource.removeAttribute("aria-invalid");
        elements.formMessage.hidden = true;

        if (!Number.isSafeInteger(brightness) || brightness < 0 || brightness > 100) {
            elements.initialBrightness.setAttribute("aria-invalid", "true");
            throw new Error("Initial brightness must be a whole number from 0 to 100.");
        }
        if (frameTimes.length === 0 || frameTimes.some(function (time) { return !Number.isSafeInteger(time) || time < 0; })) {
            elements.frameTimes.setAttribute("aria-invalid", "true");
            throw new Error("Frame times must be non-negative whole milliseconds separated by commas.");
        }
        if (frameTimes.length > 1000) {
            elements.frameTimes.setAttribute("aria-invalid", "true");
            throw new Error("A trace may contain at most 1,000 frame times.");
        }
        for (let index = 1; index < frameTimes.length; index += 1) {
            if (frameTimes[index] < frameTimes[index - 1]) {
                elements.frameTimes.setAttribute("aria-invalid", "true");
                throw new Error("Frame times must be nondecreasing.");
            }
        }
        if (!Number.isSafeInteger(latencyMs) || latencyMs < 0)
            throw new Error("Latency must be a non-negative whole number.");
        if (!Number.isSafeInteger(duplicateReports) ||
            duplicateReports < 0 || duplicateReports > 64)
            throw new Error("Duplicate reports must be a whole number from 0 to 64.");
        if (new TextEncoder().encode(elements.scriptSource.value).length > 65536) {
            elements.scriptSource.setAttribute("aria-invalid", "true");
            throw new Error("Behavior script exceeds the 65,536-byte limit.");
        }

        return {
            initial_brightness: brightness,
            script: elements.scriptSource.value,
            frame_times: frameTimes,
            feedback_timing: elements.feedbackTiming.value,
            latency_ms: latencyMs,
            adapter_outcome: elements.adapterOutcome.value,
            duplicate_reports: duplicateReports,
            silent: elements.silentAdapter.checked,
            reverse_delivery: elements.reverseDelivery.checked
        };
    }

    function showFormError(error) {
        elements.formMessage.textContent = error.message || String(error);
        elements.formMessage.hidden = false;
        const invalid = elements.form.querySelector('[aria-invalid="true"]');
        if (invalid) invalid.focus();
    }

    function updateEditorGutter() {
        const lineCount = elements.scriptSource.value.split("\n").length;
        const numbers = [];
        for (let line = 1; line <= lineCount; line += 1) numbers.push(String(line));
        elements.editorGutter.textContent = numbers.join("\n");
        elements.editorGutter.scrollTop = elements.scriptSource.scrollTop;
    }

    async function consumeTrace(response, generation) {
        if (!response.ok) {
            const body = (await response.text()).slice(0, 2048);
            throw new Error(body || `Local server returned HTTP ${response.status}.`);
        }
        if (!response.body) throw new Error("This browser did not provide a readable trace stream.");

        state.reader = response.body.getReader();
        const decoder = new TextDecoder("utf-8", { fatal: true });
        let buffer = "";
        while (true) {
            const result = await state.reader.read();
            if (generation !== state.generation) return;
            if (result.done) break;
            buffer += decoder.decode(result.value, { stream: true });
            if (buffer.length > MAX_STREAM_BUFFER && !buffer.includes("\n")) {
                throw new Error("A trace event exceeded the browser line limit.");
            }
            const lines = buffer.split("\n");
            buffer = lines.pop();
            lines.forEach(function (line) {
                if (line.trim() === "") return;
                enqueueEvent(JSON.parse(line));
            });
        }
        buffer += decoder.decode();
        if (generation !== state.generation) return;
        if (buffer.trim() !== "") enqueueEvent(JSON.parse(buffer));
        state.transportDone = true;
        elements.transportHealth.textContent = "Received";
        const terminalQueued = state.queue.slice(state.queueHead).some(function (event) {
            return event.event === "run_completed" || event.event === "run_failed" || event.event === "transport_failed";
        });
        if (!state.terminal && !terminalQueued) {
            localTransportFailure("The trace stream closed before a terminal event.");
        }
    }

    async function runScenario(event) {
        if (event) event.preventDefault();
        let scenario;
        try {
            scenario = parseScenario();
        } catch (error) {
            showFormError(error);
            return;
        }

        stopActiveRun(false);
        resetView();
        const generation = state.generation;
        state.abortController = new AbortController();

        try {
            const response = await fetch("/api/run", {
                method: "POST",
                headers: {
                    "Content-Type": "application/json",
                    "X-Solid-Scope-Token": elements.token ? elements.token.content : ""
                },
                body: JSON.stringify(scenario),
                signal: state.abortController.signal,
                credentials: "same-origin",
                cache: "no-store"
            });
            await consumeTrace(response, generation);
        } catch (error) {
            if (generation !== state.generation) return;
            if (error && error.name === "AbortError") return;
            localTransportFailure(error && error.message ? error.message : error);
        } finally {
            if (generation === state.generation) state.reader = null;
        }
    }

    function stopActiveRun(announceStop) {
        state.generation += 1;
        state.userStopped = true;
        if (state.abortController) state.abortController.abort();
        if (state.reader) state.reader.cancel().catch(function () {});
        state.abortController = null;
        state.reader = null;
        window.clearTimeout(state.playbackTimer);
        state.playbackTimer = null;
        state.queue.length = 0;
        state.queueHead = 0;
        if (announceStop) {
            state.terminal = true;
            elements.body.dataset.runState = "stopped";
            elements.playbackStatus.textContent = "Stopped by user · retained evidence is partial";
            elements.transportHealth.textContent = "Stopped";
            setConnection("Stopped", "idle");
            setHealth("Partial", "idle");
            setScriptState("Stopped", "idle");
            finishControls();
            announce("Trace stopped. The visible evidence is partial.");
        }
    }

    function togglePlayback() {
        state.playing = !state.playing;
        if (!state.playing) {
            window.clearTimeout(state.playbackTimer);
            state.playbackTimer = null;
            elements.playbackStatus.textContent = "Playback paused · trace reception may continue";
        } else {
            elements.playbackStatus.textContent = "Replaying recorded evidence";
            schedulePlayback();
        }
        updatePlaybackButtons();
    }

    function stepPlayback() {
        if (state.playing || queuedEventCount() === 0) return;
        try {
            applyEvent(dequeueEvent());
        } catch (error) {
            localTransportFailure(error && error.message ? error.message : error);
        }
        updatePlaybackButtons();
    }

    function runSelfTest() {
        stopActiveRun(false);
        resetView();
        const failureEvents = [
            { schema: TRACE_SCHEMA, event: "run_started", seq: 0, initial_brightness: 10, frame_times: [100], feedback_timing: "deferred", latency_ms: 0, adapter_outcome: "applied", duplicate_reports: 0, silent: false, reverse_delivery: false },
            { schema: TRACE_SCHEMA, event: "world_ready", seq: 1, behavior_id: 0, light_type_id: 0, light_slot_id: 0, actual_brightness: 10 },
            { schema: TRACE_SCHEMA, event: "frame_started", seq: 2, frame: 0, now_ms: 100 },
            { schema: TRACE_SCHEMA, event: "script_started", seq: 3, frame: 0, now_ms: 100 },
            { schema: TRACE_SCHEMA, event: "script_finished", seq: 4, frame: 0, now_ms: 100, status: "runtime_error", created_intents: 0, intent_ids: [], diagnostic: "bounded failure" },
            { schema: TRACE_SCHEMA, event: "frame_completed", seq: 5, frame: 0, now_ms: 100, completed: true, phases: ["begin_frame", "expire_intents", "run_input_systems", "run_behavior_systems", "run_decision_systems", "resolve_intents", "end_frame"], expired_intents: 0, resolution_requests: 1, selected_intents: 0, systems_completed: 2 },
            { schema: TRACE_SCHEMA, event: "component_snapshot", seq: 6, frame: 0, now_ms: 100, component: "officeLight", actual_brightness: 10, device_brightness: null },
            { schema: TRACE_SCHEMA, event: "run_completed", seq: 7, outcome: "script_error", script_status: "runtime_error", final_brightness: 10, device_brightness: null, commands_issued: 0, reports_applied: 0, observations_applied: 0, tracking_system_runs: 1, frames_completed: 1, faulted: false }
        ];
        failureEvents.forEach(applyEvent);
        const failureIsIsolated = elements.healthBadge.textContent === "Script failed"
            && elements.runtimeHealth.textContent.includes("Healthy")
            && elements.diagnosticText.textContent === "bounded failure";
        if (!failureIsIsolated) throw new Error("Solid Scope failure-state self-test failed.");

        resetView();
        const fullLoopPhases = ["begin_frame", "expire_intents", "run_input_systems", "run_behavior_systems", "run_decision_systems", "resolve_intents", "end_frame"];
        const events = [
            { schema: TRACE_SCHEMA, event: "run_started", seq: 0, initial_brightness: 10, frame_times: [100, 105, 110], feedback_timing: "deferred", latency_ms: 0, adapter_outcome: "applied", duplicate_reports: 0, silent: false, reverse_delivery: false },
            { schema: TRACE_SCHEMA, event: "world_ready", seq: 1, behavior_id: 0, light_type_id: 0, light_slot_id: 0, actual_brightness: 10 },
            { schema: TRACE_SCHEMA, event: "frame_started", seq: 2, frame: 0, now_ms: 100 },
            { schema: TRACE_SCHEMA, event: "script_started", seq: 3, frame: 0, now_ms: 100 },
            { schema: TRACE_SCHEMA, event: "script_finished", seq: 4, frame: 0, now_ms: 100, status: "success", created_intents: 2, intent_ids: [1, 2], diagnostic: "" },
            { schema: TRACE_SCHEMA, event: "desire_created", seq: 5, frame: 0, now_ms: 100, intent_id: 1, owner_id: 0, type: "Light", type_id: 0, component: "officeLight", name: "fallback", desired_brightness: 30, priority: "low", lifetime: "persistent" },
            { schema: TRACE_SCHEMA, event: "desire_created", seq: 6, frame: 0, now_ms: 100, intent_id: 2, owner_id: 0, type: "Light", type_id: 0, component: "officeLight", name: "boost", desired_brightness: 70, priority: "high", lifetime: "until_time", expires_at_ms: 105 },
            { schema: TRACE_SCHEMA, event: "desire_selected", seq: 7, frame: 0, now_ms: 100, intent_id: 2, owner_id: 0, type: "Light", type_id: 0, component: "officeLight", name: "boost", desired_brightness: 70, priority: "high", lifetime: "until_time", expires_at_ms: 105 },
            { schema: TRACE_SCHEMA, event: "command_issued", seq: 8, record_seq: 4, record_type: 11, data: { command_id: 1, desired: 70, issued_at: 100, route: "scope.light", target: "office-device", value: "pending" } },
            { schema: TRACE_SCHEMA, event: "command_attempt", seq: 9, record_seq: 5, record_type: 12, data: { attempt: 1, command_id: 1, now: 100 } },
            { schema: TRACE_SCHEMA, event: "frame_completed", seq: 10, frame: 0, now_ms: 100, completed: true, phases: fullLoopPhases, expired_intents: 0, resolution_requests: 1, selected_intents: 1, systems_completed: 2 },
            { schema: TRACE_SCHEMA, event: "component_snapshot", seq: 11, frame: 0, now_ms: 100, component: "officeLight", actual_brightness: 10, device_brightness: 70 },
            { schema: TRACE_SCHEMA, event: "frame_started", seq: 12, frame: 1, now_ms: 105 },
            { schema: TRACE_SCHEMA, event: "command_result", seq: 13, record_seq: 22, record_type: 14, data: { command_id: 1, disposition: "accepted", route: "scope.light", session: 1, status: "applied", target: "office-device" } },
            { schema: TRACE_SCHEMA, event: "command_status", seq: 14, record_seq: 23, record_type: 13, data: { command_id: 1, status: "applied", value: "applied" } },
            { schema: TRACE_SCHEMA, event: "observed_changed", seq: 15, record_seq: 24, record_type: 15, data: { command_id: 1, observed: 70, route: "scope.light", state_revision: 1, target: "office-device", value: 70 } },
            { schema: TRACE_SCHEMA, event: "script_started", seq: 16, frame: 1, now_ms: 105 },
            { schema: TRACE_SCHEMA, event: "script_finished", seq: 17, frame: 1, now_ms: 105, status: "success", created_intents: 0, intent_ids: [], diagnostic: "" },
            { schema: TRACE_SCHEMA, event: "desire_disappeared", seq: 18, frame: 1, now_ms: 105, intent_id: 2 },
            { schema: TRACE_SCHEMA, event: "desire_selected", seq: 19, frame: 1, now_ms: 105, intent_id: 1, owner_id: 0, type: "Light", type_id: 0, component: "officeLight", name: "fallback", desired_brightness: 30, priority: "low", lifetime: "persistent" },
            { schema: TRACE_SCHEMA, event: "command_issued", seq: 20, record_seq: 26, record_type: 11, data: { command_id: 2, desired: 30, issued_at: 105, route: "scope.light", target: "office-device", value: "pending" } },
            { schema: TRACE_SCHEMA, event: "command_attempt", seq: 21, record_seq: 27, record_type: 12, data: { attempt: 1, command_id: 2, now: 105 } },
            { schema: TRACE_SCHEMA, event: "frame_completed", seq: 22, frame: 1, now_ms: 105, completed: true, phases: fullLoopPhases, expired_intents: 1, resolution_requests: 1, selected_intents: 1, systems_completed: 2 },
            { schema: TRACE_SCHEMA, event: "component_snapshot", seq: 23, frame: 1, now_ms: 105, component: "officeLight", actual_brightness: 70, device_brightness: 30 },
            { schema: TRACE_SCHEMA, event: "frame_started", seq: 24, frame: 2, now_ms: 110 },
            { schema: TRACE_SCHEMA, event: "command_result", seq: 25, record_seq: 33, record_type: 14, data: { command_id: 2, disposition: "accepted", route: "scope.light", session: 1, status: "applied", target: "office-device" } },
            { schema: TRACE_SCHEMA, event: "command_status", seq: 26, record_seq: 34, record_type: 13, data: { command_id: 2, status: "applied", value: "applied" } },
            { schema: TRACE_SCHEMA, event: "observed_changed", seq: 27, record_seq: 35, record_type: 15, data: { command_id: 2, observed: 30, route: "scope.light", state_revision: 2, target: "office-device", value: 30 } },
            { schema: TRACE_SCHEMA, event: "script_started", seq: 28, frame: 2, now_ms: 110 },
            { schema: TRACE_SCHEMA, event: "script_finished", seq: 29, frame: 2, now_ms: 110, status: "success", created_intents: 0, intent_ids: [], diagnostic: "" },
            { schema: TRACE_SCHEMA, event: "desire_selected", seq: 30, frame: 2, now_ms: 110, intent_id: 1, owner_id: 0, type: "Light", type_id: 0, component: "officeLight", name: "fallback", desired_brightness: 30, priority: "low", lifetime: "persistent" },
            { schema: TRACE_SCHEMA, event: "frame_completed", seq: 31, frame: 2, now_ms: 110, completed: true, phases: fullLoopPhases, expired_intents: 0, resolution_requests: 1, selected_intents: 1, systems_completed: 2 },
            { schema: TRACE_SCHEMA, event: "component_snapshot", seq: 32, frame: 2, now_ms: 110, component: "officeLight", actual_brightness: 30, device_brightness: 30 },
            { schema: TRACE_SCHEMA, event: "run_completed", seq: 33, outcome: "success", script_status: "success", final_brightness: 30, device_brightness: 30, commands_issued: 2, reports_applied: 2, observations_applied: 0, tracking_system_runs: 3, frames_completed: 3, faulted: false }
        ];
        events.forEach(applyEvent);

        const loopIsTruthful = elements.actualValue.textContent === "30"
            && elements.deviceValue.textContent === "30"
            && elements.frameLedger.textContent.includes("70%")
            && elements.frameLedger.textContent.includes("30%");
        const selectedIsDesire = elements.selectedValue.textContent === "30"
            && elements.selectedDetail.textContent.includes("desire only");
        const commandLaneReported = elements.resultDetail.textContent.includes("status applied");
        const railIsComplete = elements.phaseItems.length === 7
            && elements.phaseItems.every(function (item) { return item.dataset.state === "recorded"; });
        const lampsShowConfirmedState = elements.actualLamp.dataset.known === "true"
            && elements.actualLamp.style.getPropertyValue("--lamp-level") === "30%"
            && elements.deviceLamp.dataset.known === "true"
            && elements.deviceLamp.style.getPropertyValue("--lamp-level") === "30%";
        const terminalIsComplete = elements.body.dataset.runState === "complete"
            && elements.healthBadge.dataset.state === "complete";
        if (loopIsTruthful && selectedIsDesire && commandLaneReported
            && railIsComplete && lampsShowConfirmedState && terminalIsComplete) {
            elements.body.dataset.selftest = "passed";
        } else {
            elements.body.dataset.selftest = "failed";
            throw new Error("Solid Scope UI self-test failed.");
        }
    }

    elements.runButton.textContent = `Run trace ${runShortcut}`;
    elements.headerRunButton.textContent = `Run trace ${runShortcut}`;

    elements.form.addEventListener("submit", runScenario);
    elements.headerRunButton.addEventListener("click", runScenario);
    elements.stopButton.addEventListener("click", function () { stopActiveRun(true); });
    elements.pauseButton.addEventListener("click", togglePlayback);
    elements.stepButton.addEventListener("click", stepPlayback);
    elements.speedSelect.addEventListener("change", function () {
        if (state.playing && state.playbackTimer !== null) {
            window.clearTimeout(state.playbackTimer);
            state.playbackTimer = null;
            schedulePlayback();
        }
    });
    window.addEventListener("keydown", function (event) {
        if (event.key !== "Enter" || (!event.ctrlKey && !event.metaKey) || event.altKey || event.shiftKey)
            return;
        event.preventDefault();
        runScenario();
    });
    elements.scriptSource.addEventListener("input", updateEditorGutter);
    elements.scriptSource.addEventListener("scroll", function () {
        elements.editorGutter.scrollTop = elements.scriptSource.scrollTop;
    });
    window.addEventListener("beforeunload", function () { stopActiveRun(false); });

    updateEditorGutter();

    if (new URLSearchParams(window.location.search).get("selftest") === "1") {
        runSelfTest();
    }
}());
