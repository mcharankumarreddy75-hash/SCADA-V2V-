// Node-RED settings for the V2V SCADA (userDir = scada/node-red).
// Start with scada/start_scada.bat or scada/start_scada.sh, which set V2V_SOURCE and cd here.
const path = require('path');

// Environment-driven configuration (visible to function nodes through env.get(...)).
process.env.V2V_SOURCE = (process.env.V2V_SOURCE || 'SIM').toUpperCase(); // SIM | GATEWAY
process.env.V2V_PORT = process.env.V2V_PORT || 'COM3';                  // serial port for GATEWAY
process.env.V2V_DB = process.env.V2V_DB || path.join(__dirname, '..', 'historian', 'v2v.db');

module.exports = {
    userDir: __dirname,
    flowFile: 'flows.json',
    uiPort: process.env.PORT || 1880,
    credentialSecret: false,
    functionGlobalContext: {
        ModbusRTU: require('modbus-serial'),
        V2V: require(path.join(__dirname, 'lib', 'v2v_modbus.js'))
    },
    functionExternalModules: true,
    logging: {
        console: { level: 'info', metrics: false, audit: false }
    }
};
