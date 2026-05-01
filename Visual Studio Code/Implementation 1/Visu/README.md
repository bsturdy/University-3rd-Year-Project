# ESP Mesh Visualisation

Python HMI for the TwinCAT `GVL_EspVisualisation` snapshot.

The recommended deployment is `web_hmi.py`: it runs on the Windows 10 Beckhoff controller, reads TwinCAT over ADS locally, and serves a LAN web page through the router.

## TwinCAT Symbols

The HMI reads these ADS symbols:

- `GVL_EspVisualisation.MasterUid`
- `GVL_EspVisualisation.DeviceCount`
- `GVL_EspVisualisation.SnapshotTimestamp`
- `GVL_EspVisualisation.SnapshotUpdateCounter`
- `GVL_EspVisualisation.Devices[0..63].*`

Build and activate the TwinCAT PLC after the new GVL/DUT changes so these symbols exist.

## Install

```powershell
cd "C:\University Work\3rd Year\3rd Year Project\University-3rd-Year-Project\Visual Studio Code\Implementation 1\Visu"
py -m pip install -r requirements.txt
```

Or use the Windows installer script:

```powershell
.\install_visu.ps1 -AddFirewallRule -AddStartup
```

If you double-click `install_visu.bat`, it runs the same default installation with firewall/startup enabled.

For setup/testing without TwinCAT hardware:

```powershell
.\install_visu.ps1 -Demo
```

If PowerShell script execution is blocked, run:

```powershell
.\install_visu.bat -Demo
```

This folder includes the Windows x64 Python installer and a local package cache, so the controller does not need internet access for the standard installation. Run:

```powershell
.\install_visu.bat -AddFirewallRule -AddStartup
```

## Configure

`config.json` is already set for a local TwinCAT runtime.

Use:

- `http_host: "0.0.0.0"` so phones/laptops on the router LAN can connect.
- `http_port: 8080` unless another service already uses it.
- `ams_net_id: "local"` for the TwinCAT runtime on the same Windows controller.

## Run As LAN Page

On the Beckhoff Windows controller:

```powershell
py .\web_hmi.py
```

From another device connected to the same WiFi router:

```text
http://the-controller-lan-address:8080
```

If Windows Firewall blocks it, add an inbound TCP rule for the selected HMI port, usually `8080`.

If the browser says the site cannot be reached, run:

```powershell
.\run_hmi_diagnostic.bat
```

The diagnostic run binds only to `127.0.0.1` and keeps the console open so startup errors are visible.

## Start On Power Cycle

Simple option:

1. Press `Win + R`.
2. Run `shell:startup`.
3. Add a shortcut to `Visu\start_web_hmi.bat`.

More robust option:

1. Open Windows Task Scheduler.
2. Create a task named `ESP Mesh HMI`.
3. Trigger: `At startup`.
4. Action: start `py`.
5. Arguments: `web_hmi.py`.
6. Start in: the full `Visu` folder path.
7. Enable `Run whether user is logged on or not` if the controller should serve the page before login.

## Desktop Tkinter View

```powershell
py .\ads_visualisation.py --netid local --port 851
```

For layout testing without TwinCAT:

```powershell
py .\ads_visualisation.py --demo
```

## Topology Limit

Parent links are inferred in the PLC snapshot from the last-hop IP of forwarded packets. A forwarded node can only be linked to its parent after the direct parent has also reported itself and therefore has a known UID/IP pair.
