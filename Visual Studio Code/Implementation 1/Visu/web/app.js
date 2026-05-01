const svg = document.getElementById("topology");
const table = document.getElementById("device-table");
const totalCount = document.getElementById("total-count");
const onlineCount = document.getElementById("online-count");
const DISPLAY_ONLINE_GRACE_S = 15;
const HIDDEN_DEVICE_KEY = "esp-mesh-hmi-hidden-devices";
let hiddenDevices = loadHiddenDevices();

async function refresh() {
  try {
    const response = await fetch("/api/snapshot", { cache: "no-store" });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const snapshot = await response.json();
    render(snapshot);
  } catch (error) {
    render({ master_uid: 999999999, devices: [], error: error.message });
  }
}

function render(snapshot) {
  const visibleDevices = [];
  const devices = (snapshot.devices || []).map((device) => ({
    ...device,
    hmi_online: Boolean(device.online) || Number(device.last_seen_s || 0) <= DISPLAY_ONLINE_GRACE_S,
  }));

  for (const device of devices) {
    const uid = String(device.uid);
    if (device.hmi_online && hiddenDevices.has(uid)) {
      hiddenDevices.delete(uid);
      saveHiddenDevices();
    }

    if (!hiddenDevices.has(uid)) {
      visibleDevices.push(device);
    }
  }

  const online = visibleDevices.filter((device) => device.hmi_online).length;
  totalCount.textContent = visibleDevices.length;
  onlineCount.textContent = online;

  renderTopology(snapshot.master_uid || 999999999, visibleDevices);
  renderTable(visibleDevices);
}

function renderTopology(masterUid, devices) {
  svg.replaceChildren();
  const rect = svg.getBoundingClientRect();
  const width = Math.max(rect.width, 640);
  const height = Math.max(rect.height, 470);
  svg.setAttribute("viewBox", `0 0 ${width} ${height}`);

  const onlineDevices = devices.filter((device) => device.hmi_online);
  const offlineDevices = devices.filter((device) => !device.hmi_online);

  if (devices.length === 0) {
    addText(width / 2, height / 2, "No devices in TwinCAT snapshot yet", "node-label");
    return;
  }

  const nodes = new Map();
  nodes.set(masterUid, {
    uid: masterUid,
    x: width / 2,
    y: 64,
    label: "Master",
    sub: String(masterUid),
    online: true,
    master: true,
    radius: 38,
  });

  const levels = new Map();
  for (const device of onlineDevices) {
    const level = Math.max(1, Number(device.chain_distance || 1));
    if (!levels.has(level)) levels.set(level, []);
    levels.get(level).push(device);
  }

  const maxLevel = Math.max(...levels.keys(), 1);
  const deadBoxHeight = offlineDevices.length > 0 ? Math.min(150, 54 + offlineDevices.length * 24) : 0;
  const topMargin = 58;
  const bottomMargin = 58 + deadBoxHeight;
  const usableHeight = Math.max(180, height - topMargin - bottomMargin);
  const gap = usableHeight / Math.max(maxLevel, 1);
  const radius = Math.max(22, Math.min(33, gap * 0.33));
  const masterRadius = Math.min(38, radius + 5);
  nodes.get(masterUid).radius = masterRadius;

  for (const [level, levelDevices] of levels.entries()) {
    levelDevices.sort((a, b) => Number(a.uid) - Number(b.uid));
    const count = levelDevices.length;
    levelDevices.forEach((device, index) => {
      nodes.set(device.uid, {
        uid: device.uid,
        x: (width * (index + 1)) / (count + 1),
        y: topMargin + level * gap,
        label: String(device.uid),
        sub: device.last_received_ip || "no IP",
        online: device.hmi_online,
        master: false,
        radius,
      });
    });
  }

  for (const device of onlineDevices) {
    const child = nodes.get(device.uid);
    const parent = nodes.get(device.parent_uid);
    if (child && parent) {
      addLine(parent.x, parent.y + parent.radius, child.x, child.y - child.radius, "edge");
    } else if (child && Number(device.parent_uid) === 0) {
      addLine(child.x, child.y - 58, child.x, child.y - child.radius, "edge unresolved");
      addText(child.x, child.y - 70, "parent unresolved", "node-sub");
    }
  }

  for (const node of nodes.values()) {
    addNode(node);
  }

  if (offlineDevices.length > 0) {
    addDeadBox(offlineDevices, width, height);
  }
}

function renderTable(devices) {
  table.replaceChildren();
  const sorted = [...devices].sort((a, b) => {
    const chainDelta = Number(a.chain_distance || 0) - Number(b.chain_distance || 0);
    if (chainDelta !== 0) return chainDelta;
    return Number(a.uid) - Number(b.uid);
  });

  for (const device of sorted) {
    const isOnline = device.hmi_online ?? device.online;
    const row = document.createElement("tr");
    row.innerHTML = `
      <td><button class="remove-device" data-uid="${escapeHtml(device.uid)}" title="Hide this stale device">x</button>${escapeHtml(device.uid)}</td>
      <td><span class="pill ${isOnline ? "online" : "offline"}">${isOnline ? "online" : "offline"}</span></td>
      <td>${device.parent_uid || "unknown"}</td>
      <td>${escapeHtml(device.last_received_ip || "")}</td>
      <td>${formatDuration(device.age_s)}</td>
      <td>${formatDuration(device.last_seen_s)}</td>
      <td>${device.chain_distance}</td>
      <td>${device.packets_received}</td>
    `;
    const removeButton = row.querySelector(".remove-device");
    removeButton.disabled = isOnline;
    removeButton.title = isOnline ? "Live devices reappear automatically" : "Hide this stale device";
    removeButton.addEventListener("click", () => {
      hiddenDevices.add(String(device.uid));
      saveHiddenDevices();
      refresh();
    });
    table.appendChild(row);
  }
}

function addNode(node) {
  const group = document.createElementNS("http://www.w3.org/2000/svg", "g");
  const circle = document.createElementNS("http://www.w3.org/2000/svg", "circle");
  const radius = node.radius || (node.master ? 38 : 33);
  circle.setAttribute("cx", node.x);
  circle.setAttribute("cy", node.y);
  circle.setAttribute("r", radius);
  circle.setAttribute("fill", node.master ? "#9dc9ff" : node.online ? "#b8efc9" : "#d6dbe3");
  circle.setAttribute("stroke", node.master ? "#5f9fe8" : node.online ? "#75c98f" : "#aab4c3");
  circle.setAttribute("stroke-width", "3");
  group.appendChild(circle);
  group.appendChild(svgText(node.x, node.y - 4, node.label, "node-label"));
  group.appendChild(svgText(node.x, node.y + 14, node.sub, "node-sub"));
  svg.appendChild(group);
}

function addDeadBox(devices, width, height) {
  const boxWidth = Math.min(310, Math.max(190, width * 0.24));
  const circleRadius = 16;
  const gap = 12;
  const titleHeight = 38;
  const columns = Math.max(3, Math.floor((boxWidth - 28) / (circleRadius * 2 + gap)));
  const visibleCount = Math.max(0, columns * 3 - 1);
  const visible = devices.slice(0, visibleCount);
  const overflowCount = Math.max(0, devices.length - visible.length);
  const nodeCount = visible.length + (overflowCount > 0 ? 1 : 0);
  const rows = Math.max(1, Math.ceil(nodeCount / columns));
  const boxHeight = titleHeight + rows * (circleRadius * 2 + gap) + 12;
  const x = 18;
  const y = height - boxHeight - 18;

  const group = document.createElementNS("http://www.w3.org/2000/svg", "g");
  const rect = document.createElementNS("http://www.w3.org/2000/svg", "rect");
  rect.setAttribute("x", x);
  rect.setAttribute("y", y);
  rect.setAttribute("width", boxWidth);
  rect.setAttribute("height", boxHeight);
  rect.setAttribute("rx", 16);
  rect.setAttribute("class", "dead-box");
  group.appendChild(rect);
  group.appendChild(svgText(x + 16, y + 26, "Previously Seen", "dead-title"));

  visible.forEach((device, index) => {
    addDeadNode(group, x, y, columns, circleRadius, gap, titleHeight, index, String(device.uid));
  });

  if (overflowCount > 0) {
    addDeadNode(group, x, y, columns, circleRadius, gap, titleHeight, visible.length, `+${overflowCount}`);
  }

  svg.appendChild(group);
}

function addDeadNode(group, x, y, columns, radius, gap, titleHeight, index, label) {
  const column = index % columns;
  const row = Math.floor(index / columns);
  const cx = x + 18 + radius + column * (radius * 2 + gap);
  const cy = y + titleHeight + radius + row * (radius * 2 + gap);
  const circle = document.createElementNS("http://www.w3.org/2000/svg", "circle");
  circle.setAttribute("cx", cx);
  circle.setAttribute("cy", cy);
  circle.setAttribute("r", radius);
  circle.setAttribute("class", "dead-node");
  group.appendChild(circle);
  group.appendChild(svgText(cx, cy + 4, label, "dead-node-label"));
}

function addLine(x1, y1, x2, y2, className) {
  const line = document.createElementNS("http://www.w3.org/2000/svg", "line");
  line.setAttribute("x1", x1);
  line.setAttribute("y1", y1);
  line.setAttribute("x2", x2);
  line.setAttribute("y2", y2);
  line.setAttribute("class", className);
  svg.appendChild(line);
}

function addText(x, y, value, className) {
  svg.appendChild(svgText(x, y, value, className));
}

function svgText(x, y, value, className) {
  const text = document.createElementNS("http://www.w3.org/2000/svg", "text");
  text.setAttribute("x", x);
  text.setAttribute("y", y);
  text.setAttribute("class", className);
  text.textContent = value;
  return text;
}

function formatDuration(value) {
  const seconds = Number(value || 0);
  if (seconds < 1) return `${seconds.toFixed(1)}s`;
  if (seconds < 60) return `${seconds.toFixed(0)}s`;
  const minutes = seconds / 60;
  if (minutes < 60) return `${minutes.toFixed(1)}m`;
  return `${(minutes / 60).toFixed(1)}h`;
}

function escapeHtml(value) {
  return String(value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#039;");
}

function loadHiddenDevices() {
  try {
    const values = JSON.parse(localStorage.getItem(HIDDEN_DEVICE_KEY) || "[]");
    return new Set(Array.isArray(values) ? values.map(String) : []);
  } catch {
    return new Set();
  }
}

function saveHiddenDevices() {
  localStorage.setItem(HIDDEN_DEVICE_KEY, JSON.stringify([...hiddenDevices]));
}

refresh();
setInterval(refresh, 1000);
window.addEventListener("resize", refresh);
