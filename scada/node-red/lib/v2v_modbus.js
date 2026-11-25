'use strict';
// Shared Modbus helper for the V2V SCADA flows (loaded via settings.js functionGlobalContext).
// Uses the modbus-serial library instead of node-red-contrib-modbus (see scada/README.md).
// One client is kept in the Node-RED global context ('mbClient'); 'mbBusy' serialises
// poller reads and command writes so they never overlap on the same line.

const ModbusRTU = require('modbus-serial');

const SIM_HOST = '127.0.0.1';
const SIM_PORT = 5020;
const UNIT_ID = 1;
const TIMEOUT_MS = 500;
const SERIAL_BAUD = 115200;

function source() {
    return (process.env.V2V_SOURCE || 'SIM').toUpperCase();
}

async function openClient(ctx) {
    const client = new ModbusRTU();
    client.setTimeout(TIMEOUT_MS);
    try {
        if (source() === 'GATEWAY') {
            const port = process.env.V2V_PORT || 'COM3';
            await client.connectRTUBuffered(port, {
                baudRate: SERIAL_BAUD, dataBits: 8, parity: 'none', stopBits: 1
            });
        } else {
            await client.connectTCP(SIM_HOST, { port: SIM_PORT });
        }
        await client.setID(UNIT_ID);
        client.setTimeout(TIMEOUT_MS);
    } catch (err) {
        try { client.close(() => {}); } catch (e) { /* ignore */ }
        throw err;
    }
    ctx.set('mbClient', client);
    return client;
}

async function getClient(ctx) {
    const c = ctx.get('mbClient');
    if (c && c.isOpen) return c;
    return openClient(ctx);
}

function resetClient(ctx) {
    const c = ctx.get('mbClient');
    ctx.set('mbClient', null);
    if (c) { try { c.close(() => {}); } catch (e) { /* ignore */ } }
}

// Runs fn(client) with the bus lock held. Waits (polling every 10 ms) if another request is in flight.
function withBus(ctx, fn, maxWaitMs) {
    const started = Date.now();
    return new Promise((resolve, reject) => {
        const attempt = async () => {
            if (ctx.get('mbBusy')) {
                if (maxWaitMs !== undefined && Date.now() - started > maxWaitMs) {
                    return reject(new Error('bus busy'));
                }
                return setTimeout(attempt, 10);
            }
            ctx.set('mbBusy', true);
            try {
                const client = await getClient(ctx);
                resolve(await fn(client));
            } catch (err) {
                reject(err);
            } finally {
                ctx.set('mbBusy', false);
            }
        };
        attempt();
    });
}

module.exports = { withBus, resetClient, source, UNIT_ID };
