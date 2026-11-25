# V2V SCADA (Node-RED)

Operator station for the V2V system: live map, telemetry, V2V link quality, alarm console with
SQLite history, and a command panel. It polls the gateway over Modbus RTU (or the simulator over
Modbus TCP) every 250 ms.

## Files
- `start_scada.bat` / `start_scada.sh`: start Node-RED (defaults `V2V_SOURCE=SIM`, `V2V_PORT=COM3`).
- `node-red/settings.js`: userDir, env-driven config, `modbus-serial` exposed to function nodes.
- `node-red/lib/v2v_modbus.js`: shared Modbus client (one connection, bus lock for poll and commands).
- `node-red/build_flows.js`: generator for `node-red/flows.json`. Edit this, then run `node build_flows.js`.
- `node-red/flows.json`: generated flow (tab "V2V SCADA").
- `historian/schema.sql`: SQLite schema (also created automatically at start). Database: `historian/v2v.db`.

## Start the simulator (test mode)
From the project root:
```
tools/.venv/Scripts/python.exe tools/gateway_sim.py --speedup 10
```
It listens on 127.0.0.1:5020, unit 1.

## Start the SCADA
Windows: `scada\start_scada.bat` (set `V2V_SOURCE` first if not SIM).
Git Bash: `scada/start_scada.sh`.

## Switch to the real gateway (OBU #2 over USB)
Set the source to GATEWAY and the COM port of the gateway's USB CDC port:
```
set V2V_SOURCE=GATEWAY
set V2V_PORT=COM5
scada\start_scada.bat
```
Settings: RTU, 115200 8N1, slave ID 1, 500 ms timeout, holding registers 0-109 (poll) and 100-103 (commands).

## Pages and endpoints (port 1880 by default; set `PORT` to change)
| URL | Content |
|---|---|
| http://localhost:1880/dashboard/map | Map (worldmap iframe, markers V1/V2, red SOS, yellow hazards with report counts) |
| http://localhost:1880/dashboard/telemetry | Per-vehicle gauges, chart, text values |
| http://localhost:1880/dashboard/link | Distance, TTC, warning level, latency, counters, gateway status |
| http://localhost:1880/dashboard/alarms | Active alarms, acknowledge, history (last 100), CSV links |
| http://localhost:1880/dashboard/control | Hazard alert, clear SOS, buzzer test, ping, speed advisory, target |
| http://localhost:1880/worldmap | Stand-alone map |
| http://localhost:1880/v2v/status | JSON: source, gateway status, latest decoded frame, active alarms |
| http://localhost:1880/v2v/command?code=4&param=0&target=1 | Send a command (code 1-5, param 0-65535, target 0-65535; target 0 = all) |
| http://localhost:1880/v2v/export.csv?table=telemetry | CSV download (also `table=alarms`) |
| http://localhost:1880/ | Node-RED editor |

Commands are written with FC16 to address 100 (4 registers: code, param, target, trigger=1).

## Modbus library note
The spec (S12.1) asks for `node-red-contrib-modbus`. It could not be installed (its dependency is
only available as a third-party tarball, which npm blocked). The poller and command writer use the
`modbus-serial` npm library in function nodes instead, through `lib/v2v_modbus.js`. Same behaviour:
FC3 address 0 quantity 110 every 250 ms, FC16 to address 100 quantity 4, TCP 127.0.0.1:5020 for SIM,
serial 115200 8N1 for GATEWAY, 500 ms timeout.

## Alarms and historian
- Alarm rules: spec S12.6. Rising edge creates a row in `alarms` and a notification; falling edge sets
  `cleared_ts`. CRITICAL alarms stay in the active list until acknowledged. Acknowledge sets `acked` and `ack_ts`.
- Telemetry: one row per vehicle per second in `telemetry`.
- Gateway offline: raised when Modbus has failed for more than 2 s (vehicle_id 0).

## Rebuilding the flow
```
node scada/node-red/build_flows.js
```
Then restart Node-RED. Do not hand-edit `flows.json`; the generator overwrites it.
