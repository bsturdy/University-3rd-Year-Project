const svg = document.getElementById("topology");
const table = document.getElementById("device-table");
const totalCount = document.getElementById("total-count");
const onlineCount = document.getElementById("online-count");
const DISPLAY_ONLINE_TIMEOUT_S = 3;
const REFRESH_INTERVAL_MS = 500;
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
    hmi_online: Number(device.last_seen_s || 0) <= DISPLAY_ONLINE_TIMEOUT_S,
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

  const layout = buildTreeLayout(masterUid, onlineDevices, width, height, offlineDevices.length);

  for (const edge of layout.edges) {
    const child = layout.nodes.get(edge.childUid);
    const parent = layout.nodes.get(edge.parentUid);
    if (child && parent) {
      addLine(parent.x, parent.y + parent.radius, child.x, child.y - child.radius, "edge");
    }
  }

  for (const uid of layout.unresolvedUids) {
    const child = layout.nodes.get(uid);
    if (child) {
      addLine(child.x, child.y - 58, child.x, child.y - child.radius, "edge unresolved");
      addText(child.x, child.y - 70, "parent unresolved", "node-sub");
    }
  }

  for (const node of layout.nodes.values()) {
    addNode(node);
  }

  if (offlineDevices.length > 0) {
    addDeadBox(offlineDevices, width, height);
  }
}

function buildTreeLayout(masterUid, onlineDevices, width, height, offlineCount) {
  const masterKey = normaliseUid(masterUid) || 999999999;
  const devicesByUid = new Map();
  const childrenByParent = new Map();
  const unresolvedUids = new Set();

  for (const device of onlineDevices) {
    devicesByUid.set(normaliseUid(device.uid), device);
  }

  for (const device of onlineDevices) {
    const uid = normaliseUid(device.uid);
    const parentUid = normaliseUid(device.parent_uid);

    if (parentUid === masterKey || devicesByUid.has(parentUid)) {
      if (!childrenByParent.has(parentUid)) childrenByParent.set(parentUid, []);
      childrenByParent.get(parentUid).push(device);
    } else {
      unresolvedUids.add(uid);
    }
  }

  for (const children of childrenByParent.values()) {
    children.sort(compareTopologyDevices);
  }

  let roots = [
    ...(childrenByParent.get(masterKey) || []),
    ...onlineDevices.filter((device) => unresolvedUids.has(normaliseUid(device.uid))),
  ].sort(compareTopologyDevices);

  if (roots.length === 0 && onlineDevices.length > 0) {
    roots = [...onlineDevices].sort(compareTopologyDevices);
    for (const device of roots) {
      unresolvedUids.add(normaliseUid(device.uid));
    }
  }

  const depthByUid = new Map();
  const weightByUid = new Map();

  const measure = (uid, path = new Set()) => {
    if (path.has(uid)) return 1;
    if (weightByUid.has(uid)) return weightByUid.get(uid);

    path.add(uid);
    const children = childrenByParent.get(uid) || [];
    let weight = 0;

    for (const child of children) {
      weight += measure(normaliseUid(child.uid), path);
    }

    path.delete(uid);
    weight = Math.max(1, weight);
    weightByUid.set(uid, weight);
    return weight;
  };

  const measureDepth = (uid, depth, path = new Set()) => {
    if (path.has(uid)) return depth;
    path.add(uid);
    depthByUid.set(uid, Math.max(depthByUid.get(uid) || 0, depth));

    let maxDepth = depth;
    for (const child of childrenByParent.get(uid) || []) {
      maxDepth = Math.max(maxDepth, measureDepth(normaliseUid(child.uid), depth + 1, path));
    }

    path.delete(uid);
    return maxDepth;
  };

  let totalWeight = 0;
  let maxDepth = 1;
  for (const root of roots) {
    const uid = normaliseUid(root.uid);
    totalWeight += measure(uid);
    maxDepth = Math.max(maxDepth, measureDepth(uid, 1));
  }

  const deadBoxHeight = offlineCount > 0 ? Math.min(150, 54 + offlineCount * 24) : 0;
  const masterY = 64;
  const bottomLimit = height - deadBoxHeight - 48;
  const availableHeight = Math.max(180, bottomLimit - masterY);
  const verticalGap = Math.max(70, Math.min(96, availableHeight / Math.max(maxDepth, 1)));
  const nodeRadius = Math.max(23, Math.min(33, verticalGap * 0.34));
  const masterRadius = Math.min(38, nodeRadius + 5);
  const sidePadding = Math.max(80, Math.min(width * 0.18, 170));
  const availableWidth = Math.max(1, width - sidePadding * 2);
  const preferredWidth = Math.max(220, totalWeight * 150);
  const totalSpan = Math.min(availableWidth, preferredWidth);
  const left = (width - totalSpan) / 2;
  const unitSpan = totalSpan / Math.max(totalWeight, 1);

  const nodes = new Map();
  const edges = [];
  nodes.set(masterKey, {
    uid: masterKey,
    x: width / 2,
    y: masterY,
    label: "Master",
    sub: String(masterKey),
    online: true,
    master: true,
    radius: masterRadius,
  });

  const placeSubtree = (device, startX, endX, path = new Set()) => {
    const uid = normaliseUid(device.uid);
    if (path.has(uid)) return (startX + endX) / 2;

    path.add(uid);
    const children = childrenByParent.get(uid) || [];
    let childStartX = startX;
    const childCenters = [];

    for (const child of children) {
      const childUid = normaliseUid(child.uid);
      const childWidth = unitSpan * measure(childUid);
      const childEndX = childStartX + childWidth;
      childCenters.push(placeSubtree(child, childStartX, childEndX, path));
      edges.push({ parentUid: uid, childUid });
      childStartX = childEndX;
    }

    const x = childCenters.length > 0
      ? (childCenters[0] + childCenters[childCenters.length - 1]) / 2
      : (startX + endX) / 2;
    const depth = depthByUid.get(uid) || 1;

    nodes.set(uid, {
      uid,
      x,
      y: masterY + depth * verticalGap,
      label: String(device.uid),
      sub: device.last_received_ip || "no IP",
      online: device.hmi_online,
      master: false,
      radius: nodeRadius,
    });

    path.delete(uid);
    return x;
  };

  let cursor = left;
  for (const root of roots) {
    const uid = normaliseUid(root.uid);
    const rootWidth = unitSpan * measure(uid);
    placeSubtree(root, cursor, cursor + rootWidth);
    if (!unresolvedUids.has(uid)) {
      edges.push({ parentUid: masterKey, childUid: uid });
    }
    cursor += rootWidth;
  }

  return {
    nodes,
    edges,
    unresolvedUids,
  };
}

function normaliseUid(value) {
  const uid = Number(value);
  return Number.isFinite(uid) ? uid : 0;
}

function compareTopologyDevices(a, b) {
  const depthDelta = Number(a.chain_distance || 0) - Number(b.chain_distance || 0);
  if (depthDelta !== 0) return depthDelta;
  return normaliseUid(a.uid) - normaliseUid(b.uid);
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
setInterval(refresh, REFRESH_INTERVAL_MS);
window.addEventListener("resize", refresh);
