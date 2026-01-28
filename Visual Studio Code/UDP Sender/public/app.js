const wsStatus = document.getElementById("wsStatus");
const errBox = document.getElementById("errBox");
const errBoxJobs = document.getElementById("errBoxJobs");

const mRemoteIp = document.getElementById("mRemoteIp");
const mRemotePort = document.getElementById("mRemotePort");
const mTxPort = document.getElementById("mTxPort");
const mMode = document.getElementById("mMode");
const mBytes = document.getElementById("mBytes");
const mParseHint = document.getElementById("mParseHint");
const mSend = document.getElementById("mSend");
const mSaveSlot = document.getElementById("mSaveSlot");
const mSaveName = document.getElementById("mSaveName");
const mSaveNote = document.getElementById("mSaveNote");
const mSave = document.getElementById("mSave");

const jName = document.getElementById("jName");
const jRemoteIp = document.getElementById("jRemoteIp");
const jRemotePort = document.getElementById("jRemotePort");
const jTxPort = document.getElementById("jTxPort");
const jRxPort = document.getElementById("jRxPort");
const jInterval = document.getElementById("jInterval");
const jMode = document.getElementById("jMode");
const jBytes = document.getElementById("jBytes");
const jParseHint = document.getElementById("jParseHint");
const jCreate = document.getElementById("jCreate");
const jobsList = document.getElementById("jobsList");

const slotsEl = document.getElementById("slots");

const rxLog = document.getElementById("rxLog");
const rxClear = document.getElementById("rxClear");
const rxPause = document.getElementById("rxPause");
const rxCount = document.getElementById("rxCount");

let ws;
let packetsState = null;
let jobsState = new Map();
let rxPaused = false;
let rxLines = 0;

function setError(s) {
  const t = s || "";
  errBox.textContent = t;
  if (errBoxJobs) errBoxJobs.textContent = t;
}

function parseBytes(text, mode) {
  const t = (text || "").trim();
  if (!t) return [];

  if (mode === "dec") {
    const parts = t.split(/[\s,]+/).filter(Boolean);
    const out = [];
    for (const p of parts) {
      if (!/^\d+$/.test(p)) throw new Error(`DEC parse error: '${p}'`);
      const n = Number(p);
      if (n < 0 || n > 255) throw new Error(`DEC byte out of range: ${n}`);
      out.push(n | 0);
    }
    return out;
  }

  const cleaned = t.replace(/0x/gi, "").replace(/[,]/g, " ").trim();

  if (/\s/.test(cleaned)) {
    const parts = cleaned.split(/\s+/).filter(Boolean);
    const out = [];
    for (const p of parts) {
      if (!/^[0-9a-fA-F]{1,2}$/.test(p)) throw new Error(`HEX parse error: '${p}'`);
      out.push(parseInt(p, 16));
    }
    return out;
  }

  if (!/^[0-9a-fA-F]+$/.test(cleaned)) throw new Error("HEX parse error: invalid characters");
  if (cleaned.length % 2 !== 0) throw new Error("HEX parse error: odd-length hex string");

  const out = [];
  for (let i = 0; i < cleaned.length; i += 2) out.push(parseInt(cleaned.slice(i, i + 2), 16));
  return out;
}

function bytesToHex(bytes) {
  return bytes.map(b => b.toString(16).padStart(2, "0").toUpperCase()).join(" ");
}
function bytesToAscii(bytes) {
  return bytes.map(b => (b >= 32 && b <= 126) ? String.fromCharCode(b) : ".").join("");
}

function connectWs() {
  const proto = location.protocol === "https:" ? "wss" : "ws";
  ws = new WebSocket(`${proto}://${location.host}`);

  ws.onopen = () => {
    wsStatus.textContent = "WS: connected";
    wsStatus.className = "status ok";
  };
  ws.onclose = () => {
    wsStatus.textContent = "WS: disconnected";
    wsStatus.className = "status bad";
    setTimeout(connectWs, 600);
  };

  ws.onmessage = (ev) => {
    const msg = JSON.parse(ev.data);

    if (msg.type === "error") return setError(msg.message);

    if (msg.type === "state") {
      packetsState = msg.packets;
      jobsState = new Map((msg.jobs || []).map(j => [j.id, j]));
      renderSlots();
      renderJobs();
      return;
    }

    if (msg.type === "packets_update") {
      packetsState = msg.packets;
      renderSlots();
      return;
    }

    if (msg.type === "job_update") {
      jobsState.set(msg.job.id, msg.job);
      renderJobs();
      return;
    }

    if (msg.type === "job_delete_ok") {
      jobsState.delete(msg.id);
      renderJobs();
      return;
    }

    if (msg.type === "udp_rx") {
      if (!rxPaused) addRxLine(msg);
      return;
    }

    if (msg.type === "manual_send_ok") {
      setError("");
      return;
    }
  };
}

function sendWs(obj) {
  if (!ws || ws.readyState !== 1) return setError("WebSocket not connected.");
  ws.send(JSON.stringify(obj));
}

function updateParseHints() {
  try {
    const mb = parseBytes(mBytes.value, mMode.value);
    mParseHint.textContent = `Parsed: ${mb.length} bytes`;
  } catch (e) {
    mParseHint.textContent = String(e.message || e);
  }
  try {
    const jb = parseBytes(jBytes.value, jMode.value);
    jParseHint.textContent = `Parsed: ${jb.length} bytes`;
  } catch (e) {
    jParseHint.textContent = String(e.message || e);
  }
}

mBytes.addEventListener("input", updateParseHints);
mMode.addEventListener("change", updateParseHints);
jBytes.addEventListener("input", updateParseHints);
jMode.addEventListener("change", updateParseHints);

mSend.addEventListener("click", () => {
  setError("");
  let bytes;
  try { bytes = parseBytes(mBytes.value, mMode.value); }
  catch (e) { return setError(e.message); }

  sendWs({
    type: "manual_send",
    remoteIp: mRemoteIp.value.trim(),
    remotePort: Number(mRemotePort.value),
    txPort: mTxPort.value === "" ? null : Number(mTxPort.value),
    bytes
  });
});

mSave.addEventListener("click", () => {
  setError("");
  let bytes;
  try { bytes = parseBytes(mBytes.value, mMode.value); }
  catch (e) { return setError(e.message); }

  const slot = Number(mSaveSlot.value);
  if (!Number.isInteger(slot) || slot < 1 || slot > 25) return setError("Slot must be 1..25.");

  sendWs({
    type: "packets_save_slot",
    slot,
    name: mSaveName.value,
    note: mSaveNote.value,
    bytes
  });
});

jCreate.addEventListener("click", () => {
  setError("");
  let bytes;
  try { bytes = parseBytes(jBytes.value, jMode.value); }
  catch (e) { return setError(e.message); }

  sendWs({
    type: "job_create",
    name: jName.value,
    remoteIp: jRemoteIp.value.trim(),
    remotePort: Number(jRemotePort.value),
    txPort: jTxPort.value === "" ? null : Number(jTxPort.value),
    rxPort: jRxPort.value === "" ? null : Number(jRxPort.value),
    intervalMs: Number(jInterval.value || 1),
    bytes
  });
});

function renderSlots() {
  slotsEl.innerHTML = "";
  if (!packetsState) return;

  for (const s of packetsState.slots) {
    const div = document.createElement("div");
    div.className = "slot";

    const title = document.createElement("div");
    title.className = "sname";
    title.textContent = `Slot ${s.slot}: ${s.name || "(empty)"}`;
    div.appendChild(title);

    const meta = document.createElement("div");
    meta.className = "smeta";
    meta.textContent = `${(s.bytes?.length ?? 0)} bytes` + (s.note ? ` | ${s.note}` : "");
    div.appendChild(meta);

    const actions = document.createElement("div");
    actions.className = "sactions";

    const load = document.createElement("button");
    load.textContent = "Load → Manual";
    load.onclick = () => {
      mMode.value = "hex";
      mBytes.value = bytesToHex(s.bytes || []);
      updateParseHints();
    };

    const jobFrom = document.createElement("button");
    jobFrom.textContent = "Use → Job";
    jobFrom.onclick = () => {
      jMode.value = "hex";
      jBytes.value = bytesToHex(s.bytes || []);
      if (s.name && !jName.value) jName.value = s.name;
      updateParseHints();
    };

    const del = document.createElement("button");
    del.textContent = "Delete";
    del.onclick = () => sendWs({ type: "packets_delete_slot", slot: s.slot });

    actions.appendChild(load);
    actions.appendChild(jobFrom);
    actions.appendChild(del);
    div.appendChild(actions);

    slotsEl.appendChild(div);
  }
}

function renderJobs() {
  jobsList.innerHTML = "";
  const arr = Array.from(jobsState.values()).sort((a,b) => a.id - b.id);

  for (const j of arr) {
    const div = document.createElement("div");
    div.className = "job";

    const head = document.createElement("div");
    head.innerHTML = `<b>#${j.id}</b> ${escapeHtml(j.name)} ${j.enabled ? '<span class="ok">[RUNNING]</span>' : '<span class="bad">[STOPPED]</span>'}`;
    div.appendChild(head);

    const meta = document.createElement("div");
    meta.className = "meta";
    meta.textContent =
      `Dest ${j.remoteIp}:${j.remotePort} | interval ${j.intervalMs} ms | TX ${j.txPort ?? "(auto)"} | RX ${j.rxPort ?? "(none)"} | bytes ${j.bytesLen}
Sends ${j.stats?.sends ?? 0} | Missed ${j.stats?.missed ?? 0} | Rate ${j.stats?.sendsPerSec ?? 0}/s | Last ${j.stats?.lastSendIso ?? ""}${j.stats?.lastErr ? "\nErr: " + j.stats.lastErr : ""}`;

    div.appendChild(meta);

    const actions = document.createElement("div");
    actions.className = "actions";

    const start = document.createElement("button");
    start.textContent = "Start";
    start.onclick = () => { setError(""); sendWs({ type: "job_start", id: j.id }); };

    const stop = document.createElement("button");
    stop.textContent = "Stop";
    stop.onclick = () => { setError(""); sendWs({ type: "job_stop", id: j.id }); };

    const del = document.createElement("button");
    del.textContent = "Delete";
    del.onclick = () => { setError(""); sendWs({ type: "job_delete", id: j.id }); };

    actions.appendChild(start);
    actions.appendChild(stop);
    actions.appendChild(del);

    div.appendChild(actions);
    jobsList.appendChild(div);
  }
}

function addRxLine(msg) {
  rxLines++;
  rxCount.textContent = `${rxLines} packets`;

  const bytes = msg.bytes || [];
  const hex = bytesToHex(bytes);
  const ascii = bytesToAscii(bytes);

  const line = document.createElement("div");
  line.className = "rxline";
  line.textContent =
    `[${msg.ts}] job=${msg.jobId} rxPort=${msg.rxPort} from=${msg.from.address}:${msg.from.port} len=${bytes.length}\nHEX: ${hex}\nASC: ${ascii}`;

  rxLog.appendChild(line);
  rxLog.scrollTop = rxLog.scrollHeight;

  if (rxLog.childNodes.length > 400) rxLog.removeChild(rxLog.firstChild);
}

rxClear.addEventListener("click", () => {
  rxLog.innerHTML = "";
  rxLines = 0;
  rxCount.textContent = "0 packets";
});
rxPause.addEventListener("click", () => {
  rxPaused = !rxPaused;
  rxPause.textContent = rxPaused ? "Resume" : "Pause";
});

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({
    "&":"&amp;","<":"&lt;",">":"&gt;","\"":"&quot;","'":"&#39;"
  }[c]));
}

updateParseHints();
connectWs();