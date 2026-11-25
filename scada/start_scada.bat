@echo off
rem Start the V2V SCADA (Node-RED). Set V2V_SOURCE=GATEWAY and V2V_PORT=COMx for the real gateway.
if "%V2V_SOURCE%"=="" set V2V_SOURCE=SIM
if "%V2V_PORT%"=="" set V2V_PORT=COM3
if "%PORT%"=="" set PORT=1880
cd /d "%~dp0node-red" || exit /b 1
echo V2V SCADA: source=%V2V_SOURCE% port=%V2V_PORT%
echo   Dashboard:   http://localhost:%PORT%/dashboard
echo   Worldmap:    http://localhost:%PORT%/worldmap
echo   Status JSON: http://localhost:%PORT%/v2v/status
echo   Editor:      http://localhost:%PORT%/
node node_modules\node-red\red.js --userDir . --settings settings.js
