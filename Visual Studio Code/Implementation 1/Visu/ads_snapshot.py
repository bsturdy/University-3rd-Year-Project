from __future__ import annotations

import random
import time
from dataclasses import dataclass


GVL_PREFIX = "GVL_EspVisualisation"
MAX_DEVICES = 64


@dataclass
class DeviceSnapshot:
    index: int
    in_use: bool
    online: bool
    uid: int
    parent_uid: int
    last_received_ip: str
    packets_received: int
    age_s: float
    last_seen_s: float
    uptime_s: float
    packet_type: int
    forwarding_mode: int
    chain_distance: int
    child_count: int
    hop_count: int
    status_flags: int


@dataclass
class MeshSnapshot:
    master_uid: int
    device_count: int
    update_counter: int
    devices: list[DeviceSnapshot]
    error: str = ""


class AdsSnapshotReader:
    def __init__(self, netid: str | None, port: int) -> None:
        try:
            import pyads
        except ImportError as exc:
            raise RuntimeError("pyads is not installed. Run: py -m pip install -r requirements.txt") from exc

        self.pyads = pyads
        if netid is None or netid.strip() == "" or netid.strip().lower() == "local":
            netid = self._get_local_netid(pyads)
        self.plc = pyads.Connection(netid, port)
        self.plc.open()

    def close(self) -> None:
        self.plc.close()

    def set_visualisation_ready(self, ready: bool) -> None:
        self.plc.write_by_name(f"{GVL_PREFIX}.VisualisationReady", ready, self.pyads.PLCTYPE_BOOL)
        if ready:
            counter = self._read(f"{GVL_PREFIX}.VisualisationHeartbeatCounter", self.pyads.PLCTYPE_ULINT)
            self.plc.write_by_name(
                f"{GVL_PREFIX}.VisualisationHeartbeatCounter",
                counter + 1,
                self.pyads.PLCTYPE_ULINT,
            )

    def read(self) -> MeshSnapshot:
        pyads = self.pyads
        master_uid = self._read(f"{GVL_PREFIX}.MasterUid", pyads.PLCTYPE_ULINT)
        device_count = self._read(f"{GVL_PREFIX}.DeviceCount", pyads.PLCTYPE_UINT)
        update_counter = self._read(f"{GVL_PREFIX}.SnapshotUpdateCounter", pyads.PLCTYPE_ULINT)

        devices: list[DeviceSnapshot] = []
        for index in range(MAX_DEVICES):
            base = f"{GVL_PREFIX}.Devices[{index}]"
            in_use = self._read(f"{base}.InUse", pyads.PLCTYPE_BOOL)
            if not in_use:
                continue

            age_100ns = self._read(f"{base}.Age100Ns", pyads.PLCTYPE_ULINT)
            last_seen_100ns = self._read(f"{base}.TimeSinceLastSeen100Ns", pyads.PLCTYPE_ULINT)
            uptime_us = self._read(f"{base}.UptimeUs", pyads.PLCTYPE_ULINT)

            devices.append(
                DeviceSnapshot(
                    index=index,
                    in_use=True,
                    online=self._read(f"{base}.Online", pyads.PLCTYPE_BOOL),
                    uid=self._read(f"{base}.Uid", pyads.PLCTYPE_ULINT),
                    parent_uid=self._read(f"{base}.ParentUid", pyads.PLCTYPE_ULINT),
                    last_received_ip=self._read_string(f"{base}.LastReceivedIp"),
                    packets_received=self._read(f"{base}.PacketsReceived", pyads.PLCTYPE_ULINT),
                    age_s=age_100ns / 10_000_000.0,
                    last_seen_s=last_seen_100ns / 10_000_000.0,
                    uptime_s=uptime_us / 1_000_000.0,
                    packet_type=self._read(f"{base}.PacketType", pyads.PLCTYPE_BYTE),
                    forwarding_mode=self._read(f"{base}.ForwardingMode", pyads.PLCTYPE_BYTE),
                    chain_distance=self._read(f"{base}.ChainDistance", pyads.PLCTYPE_BYTE),
                    child_count=self._read(f"{base}.ChildCount", pyads.PLCTYPE_BYTE),
                    hop_count=self._read(f"{base}.HopCount", pyads.PLCTYPE_BYTE),
                    status_flags=self._read(f"{base}.StatusFlags", pyads.PLCTYPE_BYTE),
                )
            )

        return MeshSnapshot(master_uid=master_uid, device_count=device_count, update_counter=update_counter, devices=devices)

    def _read(self, symbol: str, plc_type):
        return self.plc.read_by_name(symbol, plc_type)

    def _read_string(self, symbol: str) -> str:
        value = self.plc.read_by_name(symbol, self.pyads.PLCTYPE_STRING)
        if isinstance(value, bytes):
            return value.split(b"\x00", 1)[0].decode("ascii", errors="replace")
        return str(value).strip("\x00")

    @staticmethod
    def _get_local_netid(pyads) -> str:
        pyads.open_port()
        address = pyads.get_local_address()
        netid = getattr(address, "netId", None) or getattr(address, "netid", None)
        if isinstance(netid, bytes):
            return ".".join(str(part) for part in netid)
        if isinstance(netid, (list, tuple)):
            return ".".join(str(part) for part in netid)
        if netid:
            return str(netid)
        return str(address).split(":", 1)[0]


class LazyAdsSnapshotReader:
    def __init__(self, netid: str | None, port: int) -> None:
        self.netid = netid
        self.port = port
        self.reader: AdsSnapshotReader | None = None

    def close(self) -> None:
        if self.reader is not None:
            self.reader.close()
            self.reader = None

    def set_visualisation_ready(self, ready: bool) -> None:
        if self.reader is None:
            self.reader = AdsSnapshotReader(self.netid, self.port)
        self.reader.set_visualisation_ready(ready)

    def read(self) -> MeshSnapshot:
        if self.reader is None:
            self.reader = AdsSnapshotReader(self.netid, self.port)
        return self.reader.read()


class DemoSnapshotReader:
    def __init__(self) -> None:
        self.started = time.monotonic()
        self.counter = 0

    def close(self) -> None:
        return

    def set_visualisation_ready(self, ready: bool) -> None:
        return

    def read(self) -> MeshSnapshot:
        self.counter += 1
        now = time.monotonic()
        age = now - self.started
        devices = [
            DeviceSnapshot(0, True, True, 1001, 999999999, "192.168.137.41", 500 + self.counter, age, 0.2, age, 79, 0, 1, 2, 1, 7),
            DeviceSnapshot(1, True, True, 1002, 1001, "192.168.137.41", 300 + self.counter, age * 0.9, 0.5, age, 80, 2, 2, 1, 2, 7),
            DeviceSnapshot(2, True, self.counter % 10 != 0, 1003, 1001, "192.168.137.41", 120 + self.counter, age * 0.7, random.uniform(0.1, 2.5), age, 80, 2, 2, 0, 2, 3),
            DeviceSnapshot(3, True, True, 1004, 1002, "192.168.137.55", 80 + self.counter, age * 0.5, 1.0, age, 80, 2, 3, 0, 3, 3),
        ]
        return MeshSnapshot(999999999, len(devices), self.counter, devices)


def format_duration(seconds: float) -> str:
    if seconds < 1:
        return f"{seconds:.1f}s"
    if seconds < 60:
        return f"{seconds:.0f}s"
    minutes = seconds / 60
    if minutes < 60:
        return f"{minutes:.1f}m"
    hours = minutes / 60
    return f"{hours:.1f}h"
