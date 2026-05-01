from __future__ import annotations

import argparse
import sys
import tkinter as tk
from tkinter import ttk

from ads_snapshot import AdsSnapshotReader, DemoSnapshotReader, MeshSnapshot, format_duration


class MeshVisualisation(tk.Tk):
    def __init__(self, reader, refresh_ms: int) -> None:
        super().__init__()
        self.reader = reader
        self.refresh_ms = refresh_ms
        self.title("ESP Mesh Visualisation")
        self.geometry("1180x760")
        self.configure(bg="#0f172a")

        self.status = tk.StringVar(value="Starting")
        self._build_ui()
        self.protocol("WM_DELETE_WINDOW", self._on_close)
        self.after(100, self._refresh)

    def _build_ui(self) -> None:
        style = ttk.Style(self)
        style.theme_use("clam")
        style.configure("Treeview", background="#111827", foreground="#e5e7eb", fieldbackground="#111827", rowheight=28)
        style.configure("Treeview.Heading", background="#1f2937", foreground="#f8fafc")

        header = tk.Frame(self, bg="#0f172a")
        header.pack(fill=tk.X, padx=18, pady=(14, 8))
        tk.Label(header, text="ESP Mesh", bg="#0f172a", fg="#f8fafc", font=("Segoe UI Semibold", 22)).pack(side=tk.LEFT)
        tk.Label(header, textvariable=self.status, bg="#0f172a", fg="#93c5fd", font=("Segoe UI", 11)).pack(side=tk.RIGHT)

        body = tk.PanedWindow(self, orient=tk.HORIZONTAL, sashwidth=6, bg="#0f172a")
        body.pack(fill=tk.BOTH, expand=True, padx=18, pady=10)

        left = tk.Frame(body, bg="#111827")
        right = tk.Frame(body, bg="#111827")
        body.add(left, minsize=650)
        body.add(right, minsize=420)

        self.canvas = tk.Canvas(left, bg="#111827", highlightthickness=0)
        self.canvas.pack(fill=tk.BOTH, expand=True)

        columns = ("uid", "online", "parent", "ip", "age", "last_seen", "chain", "packets", "uptime")
        self.tree = ttk.Treeview(right, columns=columns, show="headings")
        headings = {
            "uid": "UID",
            "online": "Online",
            "parent": "Parent",
            "ip": "Last-hop IP",
            "age": "Age",
            "last_seen": "Last Seen",
            "chain": "Chain",
            "packets": "Packets",
            "uptime": "Uptime",
        }
        widths = {
            "uid": 95,
            "online": 60,
            "parent": 95,
            "ip": 115,
            "age": 70,
            "last_seen": 75,
            "chain": 55,
            "packets": 75,
            "uptime": 75,
        }
        for column in columns:
            self.tree.heading(column, text=headings[column])
            self.tree.column(column, width=widths[column], anchor=tk.CENTER)
        self.tree.pack(fill=tk.BOTH, expand=True, padx=8, pady=8)

    def _refresh(self) -> None:
        try:
            snapshot = self.reader.read()
        except Exception as exc:
            snapshot = MeshSnapshot(0, 0, 0, [], error=str(exc))

        self._draw(snapshot)
        self._populate_table(snapshot)

        if snapshot.error:
            self.status.set(f"ADS read failed: {snapshot.error}")
        else:
            online = sum(1 for device in snapshot.devices if device.online)
            self.status.set(f"Devices: {len(snapshot.devices)} | Online: {online} | Snapshot: {snapshot.update_counter}")

        self.after(self.refresh_ms, self._refresh)

    def _draw(self, snapshot: MeshSnapshot) -> None:
        self.canvas.delete("all")
        width = max(self.canvas.winfo_width(), 600)
        height = max(self.canvas.winfo_height(), 420)

        master_uid = snapshot.master_uid or 999999999
        nodes: dict[int, tuple[float, float, str, bool]] = {
            master_uid: (width / 2, 70, f"Master\n{master_uid}", True)
        }

        levels: dict[int, list[DeviceSnapshot]] = {}
        for device in snapshot.devices:
            level = max(1, int(device.chain_distance or 1))
            levels.setdefault(level, []).append(device)

        max_level = max(levels.keys(), default=1)
        level_gap = max(95, (height - 150) / max(max_level, 1))

        for level, devices in levels.items():
            devices.sort(key=lambda item: item.uid)
            count = len(devices)
            for idx, device in enumerate(devices):
                x = width * (idx + 1) / (count + 1)
                y = 70 + level * level_gap
                nodes[device.uid] = (x, y, f"{device.uid}\n{device.last_received_ip}", device.online)

        for device in snapshot.devices:
            if device.parent_uid in nodes:
                x1, y1, _, _ = nodes[device.parent_uid]
                x2, y2, _, online = nodes[device.uid]
                colour = "#38bdf8" if online else "#64748b"
                self.canvas.create_line(x1, y1 + 28, x2, y2 - 28, fill=colour, width=2)
            elif device.parent_uid == 0:
                x2, y2, _, _ = nodes[device.uid]
                self.canvas.create_text(x2, y2 - 58, text="parent unresolved", fill="#f59e0b", font=("Segoe UI", 9))
                self.canvas.create_line(x2, y2 - 44, x2, y2 - 28, fill="#f59e0b", dash=(4, 4), width=2)

        for uid, (x, y, label, online) in nodes.items():
            is_master = uid == master_uid
            radius = 34 if is_master else 30
            fill = "#1d4ed8" if is_master else ("#059669" if online else "#475569")
            outline = "#bfdbfe" if is_master else ("#bbf7d0" if online else "#94a3b8")
            self.canvas.create_oval(x - radius, y - radius, x + radius, y + radius, fill=fill, outline=outline, width=3)
            self.canvas.create_text(x, y, text=label, fill="#f8fafc", font=("Segoe UI Semibold", 9), justify=tk.CENTER)

        if not snapshot.devices:
            text = snapshot.error or "No ESP devices in TwinCAT snapshot yet"
            self.canvas.create_text(width / 2, height / 2, text=text, fill="#f8fafc", font=("Segoe UI", 14), width=520)

    def _populate_table(self, snapshot: MeshSnapshot) -> None:
        for item in self.tree.get_children():
            self.tree.delete(item)

        for device in sorted(snapshot.devices, key=lambda item: (item.chain_distance, item.uid)):
            self.tree.insert(
                "",
                tk.END,
                values=(
                    device.uid,
                    "yes" if device.online else "no",
                    device.parent_uid if device.parent_uid else "unknown",
                    device.last_received_ip,
                    format_duration(device.age_s),
                    format_duration(device.last_seen_s),
                    device.chain_distance,
                    device.packets_received,
                    format_duration(device.uptime_s),
                ),
            )

    def _on_close(self) -> None:
        self.reader.close()
        self.destroy()


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="TwinCAT ADS ESP mesh visualisation")
    parser.add_argument("--netid", help="TwinCAT runtime AMS Net ID, for example 192.168.137.1.1.1")
    parser.add_argument("--port", type=int, default=851, help="TwinCAT PLC runtime ADS port")
    parser.add_argument("--refresh-ms", type=int, default=1000, help="ADS refresh interval in milliseconds")
    parser.add_argument("--demo", action="store_true", help="Run without TwinCAT using generated demo data")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.demo:
        reader = DemoSnapshotReader()
    else:
        if not args.netid:
            print("Missing --netid. Use --demo to run without TwinCAT.", file=sys.stderr)
            return 2
        reader = AdsSnapshotReader(args.netid, args.port)

    app = MeshVisualisation(reader, args.refresh_ms)
    app.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
