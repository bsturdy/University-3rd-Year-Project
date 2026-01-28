import express from "express";
import http from "http";
import path from "path";
import { fileURLToPath } from "url";
import { WebSocketServer } from "ws";
import dgram from "dgram";
import fs from "fs";
import { performance } from "perf_hooks";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const PORT_HTTP = 3000;
const DATA_DIR = path.join(__dirname, "data");
const PACKETS_FILE = path.join(DATA_DIR, "packets.json");
const JOBS_FILE = path.join(DATA_DIR, "jobs.json");

fs.mkdirSync(DATA_DIR, { recursive: true });

function safeReadJson(file, fallback) {
  try {
    if (!fs.existsSync(file)) return fallback;
    return JSON.parse(fs.readFileSync(file, "utf8"));
  } catch {
    return fallback;
  }
}
function safeWriteJson(file, obj) {
  fs.writeFileSync(file, JSON.stringify(obj, null, 2));
}
function nowIso() {
  return new Date().toISOString();
}
function isValidPort(p) {
  return Number.isInteger(p) && p >= 1 && p <= 65535;
}
function isValidIPv4(ip) {
  const parts = String(ip).split(".");
  if (parts.length !== 4) return false;
  for (const part of parts) {
    if (!/^\d+$/.test(part)) return false;
    const n = Number(part);
    if (n < 0 || n > 255) return false;
  }
  return true;
}
function clampBytes(arr) {
  if (!Array.isArray(arr)) return null;
  const out = [];
  for (const v of arr) {
    const n = Number(v);
    if (!Number.isFinite(n) || n < 0 || n > 255) return null;
    out.push(n | 0);
  }
  return out;
}

// -------------------- Saved packets (25 slots) --------------------
const packetsState = safeReadJson(PACKETS_FILE, {
  slots: Array.from({ length: 25 }, (_, i) => ({
    slot: i + 1,
    name: "",
    bytes: [],
    note: ""
  }))
});
function savePacketsState() {
  safeWriteJson(PACKETS_FILE, packetsState);
}

// -------------------- Jobs --------------------
const jobs = new Map();
let nextJobId = 1;

function jobPublic(job) {
  return {
    id: job.id,
    name: job.name,
    remoteIp: job.remoteIp,
    remotePort: job.remotePort,
    txPort: job.txPort,
    rxPort: job.rxPort,
    intervalMs: job.intervalMs,
    bytesLen: job.bytes.length,
    enabled: job.enabled,
    createdAt: job.createdAt,
    stats: job.stats
  };
}
function jobToPersisted(job) {
  return {
    id: job.id,
    name: job.name,
    remoteIp: job.remoteIp,
    remotePort: job.remotePort,
    txPort: job.txPort,
    rxPort: job.rxPort,
    intervalMs: job.intervalMs,
    bytes: job.bytes,
    createdAt: job.createdAt
  };
}
function saveJobsState() {
  safeWriteJson(JOBS_FILE, {
    savedAt: nowIso(),
    jobs: Array.from(jobs.values()).map(jobToPersisted)
  });
}
function loadJobsState() {
  const data = safeReadJson(JOBS_FILE, { jobs: [] });
  const arr = Array.isArray(data.jobs) ? data.jobs : [];
  let maxId = 0;

  for (const j of arr) {
    const id = Number(j.id) | 0;
    const name = String(j.name ?? "").slice(0, 64) || `Job ${id}`;
    const remoteIp = String(j.remoteIp ?? "");
    const remotePort = Number(j.remotePort) | 0;
    const txPort = j.txPort == null || j.txPort === "" ? null : (Number(j.txPort) | 0);
    const rxPort = j.rxPort == null || j.rxPort === "" ? null : (Number(j.rxPort) | 0);
    const intervalMs = Math.max(1, Number(j.intervalMs) | 0);
    const bytes = clampBytes(j.bytes);
    const createdAt = String(j.createdAt ?? nowIso());

    if (id <= 0) continue;
    if (!isValidIPv4(remoteIp)) continue;
    if (!isValidPort(remotePort)) continue;
    if (txPort != null && !isValidPort(txPort)) continue;
    if (rxPort != null && !isValidPort(rxPort)) continue;
    if (!bytes || bytes.length === 0) continue;

    jobs.set(id, {
      id,
      name,
      remoteIp,
      remotePort,
      txPort,
      rxPort,
      intervalMs,
      bytes,
      enabled: false, // always STOPPED on startup
      createdAt,
      stats: {
        sends: 0,
        missed: 0,
        lastSendIso: "",
        lastErr: "",
        startIso: "",
        sendsPerSec: 0
      },
      _stopFlag: true,
      _rateWindow: [],
      _task: null
    });

    if (id > maxId) maxId = id;
  }

  nextJobId = maxId + 1;
}
loadJobsState();

// -------------------- Express + WS --------------------
const app = express();
app.use(express.json({ limit: "1mb" }));
app.use(express.static(path.join(__dirname, "public")));
const server = http.createServer(app);
const wss = new WebSocketServer({ server });

function wsBroadcast(obj) {
  const msg = JSON.stringify(obj);
  for (const client of wss.clients) {
    if (client.readyState === 1) client.send(msg);
  }
}
function wsSend(ws, obj) {
  ws.send(JSON.stringify(obj));
}

// -------------------- Shared UDP socket pool --------------------
// portKey: integer port (1..65535) or 0 (shared ephemeral socket)
const socketPool = new Map(); // portKey -> { sock, refCount, rxJobIds:Set }

async function getOrCreateSocket(portKey) {
  let entry = socketPool.get(portKey);
  if (entry) return entry;

  const sock = dgram.createSocket("udp4");

  await new Promise((resolve, reject) => {
    sock.once("error", reject);
    sock.bind(portKey === 0 ? 0 : portKey, "0.0.0.0", () => resolve());
  });

  // swallow future error events to avoid crashes
  sock.on("error", () => {});

  entry = {
    sock,
    refCount: 0,
    rxJobIds: new Set()
  };

  // Single RX handler fans out to all jobs that registered RX on this port
  sock.on("message", (msg, rinfo) => {
    const bytes = Array.from(msg.values());
    const ts = nowIso();
    for (const jobId of entry.rxJobIds) {
      const job = jobs.get(jobId);
      if (!job) continue;
      if (!job.enabled) continue;
      wsBroadcast({
        type: "udp_rx",
        jobId: job.id,
        rxPort: job.rxPort,
        from: { address: rinfo.address, port: rinfo.port },
        ts,
        bytes
      });
    }
  });

  socketPool.set(portKey, entry);
  return entry;
}

function retainSocket(portKey) {
  const e = socketPool.get(portKey);
  if (!e) return;
  e.refCount += 1;
}

function releaseSocket(portKey) {
  const e = socketPool.get(portKey);
  if (!e) return;
  e.refCount -= 1;
  if (e.refCount > 0) return;
  try { e.sock.close(); } catch {}
  socketPool.delete(portKey);
}

function computeSendsPerSec(job) {
  const t = performance.now();
  job._rateWindow = job._rateWindow.filter((x) => t - x <= 1000);
  job.stats.sendsPerSec = job._rateWindow.length;
}

function jobKeys(job) {
  const txKey = job.txPort != null ? job.txPort : 0;
  const rxKey = job.rxPort != null ? job.rxPort : null;
  return { txKey, rxKey };
}

// -------------------- Scheduler --------------------
async function runJobLoop(job) {
  const interval = Math.max(1, job.intervalMs | 0);
  let nextT = performance.now() + interval;

  while (!job._stopFlag) {
    const t = performance.now();
    const dt = nextT - t;

    if (dt > 2) {
      await new Promise((r) => setTimeout(r, Math.min(50, dt - 1)));
      continue;
    }
    if (dt > 0) {
      await new Promise((r) => setImmediate(r));
      continue;
    }

    if (t - nextT > interval) {
      const missed = Math.floor((t - nextT) / interval);
      job.stats.missed += missed;
      nextT += missed * interval;
    }

    try {
      const { txKey } = jobKeys(job);
      const txEntry = await getOrCreateSocket(txKey);
      txEntry.sock.send(Buffer.from(job.bytes), job.remotePort, job.remoteIp);

      job.stats.sends += 1;
      job.stats.lastSendIso = nowIso();
      job._rateWindow.push(performance.now());
      computeSendsPerSec(job);
    } catch (e) {
      job.stats.lastErr = String(e?.message ?? e);
    }

    wsBroadcast({ type: "job_update", job: jobPublic(job) });

    nextT += interval;
    if (nextT < performance.now() - 5000) nextT = performance.now() + interval;
  }
}

// -------------------- Start/Stop --------------------
async function startJob(job) {
  if (job.enabled) return;

  if (!isValidIPv4(job.remoteIp)) throw new Error("Invalid IPv4 remote IP.");
  if (!isValidPort(job.remotePort)) throw new Error("Invalid remote port.");
  if (job.txPort != null && !isValidPort(job.txPort)) throw new Error("Invalid TX port.");
  if (job.rxPort != null && !isValidPort(job.rxPort)) throw new Error("Invalid RX port.");
  if (!job.bytes || job.bytes.length === 0) throw new Error("Bytes cannot be empty.");

  job._stopFlag = false;
  job.enabled = true;
  job.stats.lastErr = "";
  job.stats.startIso = nowIso();
  job._rateWindow = [];

  const { txKey, rxKey } = jobKeys(job);

  try {
    await getOrCreateSocket(txKey);
    retainSocket(txKey);

    if (rxKey != null) {
      const rxEntry = await getOrCreateSocket(rxKey);
      retainSocket(rxKey);
      rxEntry.rxJobIds.add(job.id);
    }

    console.log(`[JOB START] id=${job.id} name=${job.name} TX=${job.txPort ?? "auto"} RX=${job.rxPort ?? "none"} dest=${job.remoteIp}:${job.remotePort}`);
    wsBroadcast({ type: "job_update", job: jobPublic(job) });

    job._task = runJobLoop(job).catch((e) => stopJob(job, `loop error: ${String(e?.message ?? e)}`));
  } catch (e) {
    // rollback
    job.enabled = false;
    job._stopFlag = true;
    job.stats.lastErr = `start failed: ${String(e?.message ?? e)}`;

    try {
      if (rxKey != null) {
        const rxEntry = socketPool.get(rxKey);
        if (rxEntry) rxEntry.rxJobIds.delete(job.id);
        releaseSocket(rxKey);
      }
      releaseSocket(txKey);
    } catch {}

    console.log(`[JOB START FAIL] id=${job.id} err=${job.stats.lastErr}`);
    wsBroadcast({ type: "job_update", job: jobPublic(job) });
    throw e;
  }
}

function stopJob(job, reason = "stopped") {
  if (!job.enabled) {
    job.stats.lastErr = reason === "stopped" ? "" : String(reason);
    wsBroadcast({ type: "job_update", job: jobPublic(job) });
    return;
  }

  job.enabled = false;
  job._stopFlag = true;

  const { txKey, rxKey } = jobKeys(job);

  if (rxKey != null) {
    const rxEntry = socketPool.get(rxKey);
    if (rxEntry) rxEntry.rxJobIds.delete(job.id);
    releaseSocket(rxKey);
  }

  releaseSocket(txKey);

  job.stats.lastErr = reason === "stopped" ? "" : String(reason);
  console.log(`[JOB STOP] id=${job.id} reason=${job.stats.lastErr || "stopped"}`);
  wsBroadcast({ type: "job_update", job: jobPublic(job) });
}

// -------------------- Manual send --------------------
async function manualSend({ remoteIp, remotePort, txPort, bytes }) {
  const txKey = txPort != null ? txPort : 0;
  const entry = await getOrCreateSocket(txKey);

  await new Promise((resolve, reject) => {
    entry.sock.send(Buffer.from(bytes), remotePort, remoteIp, (err) => (err ? reject(err) : resolve()));
  });
}

// -------------------- WS Protocol --------------------
wss.on("connection", (ws) => {
  wsSend(ws, {
    type: "state",
    packets: packetsState,
    jobs: Array.from(jobs.values()).map(jobPublic)
  });

  ws.on("message", async (data) => {
    let msg;
    try {
      msg = JSON.parse(data.toString("utf8"));
    } catch {
      return wsSend(ws, { type: "error", message: "Invalid JSON" });
    }

    try {
      if (msg.type === "packets_save_slot") {
        const slot = msg.slot | 0;
        if (slot < 1 || slot > 25) throw new Error("Slot must be 1..25.");
        const bytes = clampBytes(msg.bytes);
        if (!bytes) throw new Error("Invalid bytes.");
        const name = String(msg.name ?? "").slice(0, 64);
        const note = String(msg.note ?? "").slice(0, 256);

        const idx = slot - 1;
        packetsState.slots[idx].name = name;
        packetsState.slots[idx].note = note;
        packetsState.slots[idx].bytes = bytes;

        savePacketsState();
        wsBroadcast({ type: "packets_update", packets: packetsState });
        return;
      }

      if (msg.type === "packets_delete_slot") {
        const slot = msg.slot | 0;
        if (slot < 1 || slot > 25) throw new Error("Slot must be 1..25.");
        const idx = slot - 1;

        packetsState.slots[idx].name = "";
        packetsState.slots[idx].note = "";
        packetsState.slots[idx].bytes = [];

        savePacketsState();
        wsBroadcast({ type: "packets_update", packets: packetsState });
        return;
      }

      if (msg.type === "manual_send") {
        const remoteIp = String(msg.remoteIp ?? "");
        const remotePort = msg.remotePort | 0;
        const txPort = msg.txPort == null || msg.txPort === "" ? null : (msg.txPort | 0);
        const bytes = clampBytes(msg.bytes);

        if (!isValidIPv4(remoteIp)) throw new Error("Invalid IPv4 remote IP.");
        if (!isValidPort(remotePort)) throw new Error("Invalid remote port.");
        if (txPort != null && !isValidPort(txPort)) throw new Error("Invalid TX port.");
        if (!bytes) throw new Error("Invalid bytes.");

        await manualSend({ remoteIp, remotePort, txPort, bytes });
        wsSend(ws, { type: "manual_send_ok" });
        return;
      }

      if (msg.type === "job_create") {
        const name = String(msg.name ?? "").slice(0, 64) || `Job ${nextJobId}`;
        const remoteIp = String(msg.remoteIp ?? "");
        const remotePort = msg.remotePort | 0;
        const txPort = msg.txPort == null || msg.txPort === "" ? null : (msg.txPort | 0);
        const rxPort = msg.rxPort == null || msg.rxPort === "" ? null : (msg.rxPort | 0);
        const intervalMs = Math.max(1, msg.intervalMs | 0);
        const bytes = clampBytes(msg.bytes);

        if (!isValidIPv4(remoteIp)) throw new Error("Invalid IPv4 remote IP.");
        if (!isValidPort(remotePort)) throw new Error("Invalid remote port.");
        if (txPort != null && !isValidPort(txPort)) throw new Error("Invalid TX port.");
        if (rxPort != null && !isValidPort(rxPort)) throw new Error("Invalid RX port.");
        if (!bytes || bytes.length === 0) throw new Error("Bytes cannot be empty.");

        const id = nextJobId++;
        const job = {
          id,
          name,
          remoteIp,
          remotePort,
          txPort,
          rxPort,
          intervalMs,
          bytes,
          enabled: false,
          createdAt: nowIso(),
          stats: {
            sends: 0,
            missed: 0,
            lastSendIso: "",
            lastErr: "",
            startIso: "",
            sendsPerSec: 0
          },
          _stopFlag: true,
          _rateWindow: [],
          _task: null
        };

        jobs.set(id, job);
        saveJobsState();
        wsBroadcast({ type: "job_update", job: jobPublic(job) });
        return;
      }

      if (msg.type === "job_start") {
        const id = msg.id | 0;
        const job = jobs.get(id);
        if (!job) throw new Error("Job not found.");
        await startJob(job);
        return;
      }

      if (msg.type === "job_stop") {
        const id = msg.id | 0;
        const job = jobs.get(id);
        if (!job) throw new Error("Job not found.");
        stopJob(job, "stopped");
        return;
      }

      if (msg.type === "job_delete") {
        const id = msg.id | 0;
        const job = jobs.get(id);
        if (!job) throw new Error("Job not found.");
        stopJob(job, "deleted");
        jobs.delete(id);
        saveJobsState();
        wsBroadcast({ type: "job_delete_ok", id });
        return;
      }

      wsSend(ws, { type: "error", message: "Unknown message type." });
    } catch (e) {
      wsSend(ws, { type: "error", message: String(e?.message ?? e) });
    }
  });
});

// -------------------- Shutdown --------------------
let shuttingDown = false;
function shutdown(signal) {
  if (shuttingDown) return;
  shuttingDown = true;

  try {
    for (const job of jobs.values()) stopJob(job, `shutdown:${signal}`);
  } catch {}

  try { saveJobsState(); } catch {}

  try {
    for (const e of socketPool.values()) {
      try { e.sock.close(); } catch {}
    }
    socketPool.clear();
  } catch {}

  try {
    server.close(() => process.exit(0));
    setTimeout(() => process.exit(0), 1000).unref();
  } catch {
    process.exit(0);
  }
}
process.on("SIGINT", () => shutdown("SIGINT"));
process.on("SIGTERM", () => shutdown("SIGTERM"));

server.listen(PORT_HTTP, "127.0.0.1", () => {
  console.log(`HTTP UI: http://127.0.0.1:${PORT_HTTP}`);
});