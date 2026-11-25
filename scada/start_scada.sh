#!/usr/bin/env bash
# Start the V2V SCADA (Node-RED). Usage: V2V_SOURCE=SIM|GATEWAY V2V_PORT=COMx ./start_scada.sh
here="$(cd "$(dirname "$0")" && pwd)"
export V2V_SOURCE="${V2V_SOURCE:-SIM}"
export V2V_PORT="${V2V_PORT:-COM3}"
port="${PORT:-1880}"
cd "$here/node-red" || exit 1
echo "V2V SCADA: source=$V2V_SOURCE port=$V2V_PORT"
echo "  Dashboard:   http://localhost:$port/dashboard"
echo "  Worldmap:    http://localhost:$port/worldmap"
echo "  Status JSON: http://localhost:$port/v2v/status"
echo "  Editor:      http://localhost:$port/"
exec node node_modules/node-red/red.js --userDir . --settings settings.js
