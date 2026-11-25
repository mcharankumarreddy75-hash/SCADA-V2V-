'use strict';
// Generates scada/node-red/flows.json and scada/historian/schema.sql for the V2V SCADA.
// Run from anywhere:  node scada/node-red/build_flows.js
// Function-node bodies are written as normal JavaScript functions below and embedded as text
// (Node-RED runs each body with msg, node, flow, global, env available).
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const DIR = __dirname;
const TAB = 'v2v-tab';
const nodes = [];
let slot = 0;

const hid = (key) => crypto.createHash('md5').update('v2v:' + key).digest('hex').slice(0, 16);
const body = (fn) => { const s = fn.toString(); return s.slice(s.indexOf('{') + 1, s.lastIndexOf('}')); };
const place = (col) => { slot += 1; return { x: 60 + col * 230, y: 40 + slot * 45 }; };

// Flow node (lives on the V2V SCADA tab). outputs = number of wire arrays.
function add(key, type, props, outputs, col = 0) {
    const pos = place(col);
    const n = { id: hid(key), type, z: TAB, x: pos.x, y: pos.y, ...props };
    n.wires = Array.from({ length: outputs }, () => []);
    nodes.push(n);
    return n;
}
// Configuration node (no position, no wires).
function config(key, type, props) {
    const n = { id: hid(key), type, ...props };
    nodes.push(n);
    return n;
}
function wire(src, dst, out = 0) {
    const n = nodes.find((x) => x.id === src.id);
    if (!n || !n.wires || !n.wires[out]) throw new Error('bad wire from ' + src.id + ' output ' + out);
    if (!n.wires[out].includes(dst.id)) n.wires[out].push(dst.id);
}
function func(key, code, outputs, col) {
    return add(key, 'function', {
        name: key, func: code, outputs, libs: [], initialize: '', finalize: '', timeout: 0, noerr: 0
    }, outputs, col);
}

// ------------------------------------------------------------------ tab + dashboard config
nodes.push({ id: TAB, type: 'tab', label: 'V2V SCADA', disabled: false, info: 'Modbus poller, decoder, map, dashboard, alarms, historian, commands.' });
const base = config('ui-base', 'ui-base', { name: 'V2V SCADA', path: '/dashboard', showPathInSidebar: false, navigationStyle: 'default' });
const theme = config('ui-theme', 'ui-theme', { name: 'V2V theme', colors: { surface: '#fcfcfd', primary: '#0d74ce', bgPage: '#f9f9fb', groupBg: '#ffffff', groupOutline: '#d9d9e0' } });
const pages = {};
[['map', 'Map', '/map', 1], ['tel', 'Telemetry', '/telemetry', 2], ['link', 'V2V Link', '/link', 3],
    ['alarm', 'Alarms', '/alarms', 4], ['ctrl', 'Control', '/control', 5]].forEach(([k, name, p, order]) => {
    pages[k] = config('page-' + k, 'ui-page', { name, ui: base.id, path: p, icon: '', layout: 'grid', theme: theme.id, order, className: '' });
});
const group = (key, page, name, width, order) => config('group-' + key, 'ui-group', { name, page: pages[page].id, width, height: 1, order, className: '' });
// Widget on a group. outputs: 0 for display widgets, 1 for inputs/buttons.
const widget = (key, type, grp, props, outputs = 0, col = 1) => add(key, type, { group: grp.id, ...props }, outputs, col);

// ------------------------------------------------------------------ SQLite historian
const schemaSql = `CREATE TABLE IF NOT EXISTS telemetry (
  ts INTEGER, vehicle_id INTEGER, lat REAL, lon REAL, speed_kmh REAL, heading REAL,
  accel REAL, roll INTEGER, pitch INTEGER, flags INTEGER, battery INTEGER,
  rssi INTEGER, pdr REAL, distance REAL, ttc REAL, warn INTEGER);
CREATE TABLE IF NOT EXISTS alarms (
  id INTEGER PRIMARY KEY AUTOINCREMENT, ts INTEGER, vehicle_id INTEGER,
  type TEXT, severity TEXT, lat REAL, lon REAL,
  acked INTEGER DEFAULT 0, ack_ts INTEGER, cleared_ts INTEGER);
`;
fs.writeFileSync(path.join(DIR, '..', 'historian', 'schema.sql'), schemaSql);
const schemaStatements = schemaSql.split(';').map((s) => s.trim()).filter(Boolean).map((s) => s + ';');

const dbCfg = config('db-cfg', 'sqlitedb', { name: 'v2v.db', db: '../historian/v2v.db', mode: 'RWC' });
const dbNode = (key, sqlquery, sql, col) => add(key, 'sqlite', { name: key, mydb: dbCfg.id, sqlquery, sql }, 1, col);

const SQL_TEL = 'INSERT INTO telemetry (ts, vehicle_id, lat, lon, speed_kmh, heading, accel, roll, pitch, flags, battery, rssi, pdr, distance, ttc, warn) VALUES ($ts, $vehicle_id, $lat, $lon, $speed_kmh, $heading, $accel, $roll, $pitch, $flags, $battery, $rssi, $pdr, $distance, $ttc, $warn)';
const SQL_ALM_INS = 'INSERT INTO alarms (ts, vehicle_id, type, severity, lat, lon) VALUES ($ts, $vehicle_id, $type, $severity, $lat, $lon)';
const SQL_ALM_CLR = 'UPDATE alarms SET cleared_ts = $cleared_ts WHERE ts = $ts AND vehicle_id = $vehicle_id AND type = $type';
const SQL_ALM_ACK = 'UPDATE alarms SET acked = 1, ack_ts = $ack_ts WHERE ts = $ts AND vehicle_id = $vehicle_id AND type = $type';

// Schema: inject once -> split statements -> sqlite (msg.topic mode)
const injSchema = add('inject-schema', 'inject', { name: 'Create tables (once)', props: [{ p: 'payload' }, { p: 'topic', vt: 'str' }], repeat: '', crontab: '', once: true, onceDelay: 1, topic: '', payload: '', payloadType: 'date' }, 1, 0);
const fnSchema = func('Schema statements', body(function () {
    return [msg.statements.map((s) => ({ topic: s }))];
}).replace('msg.statements', JSON.stringify(schemaStatements)), 1, 1);
const dbSchema = dbNode('DB schema', 'msg.topic', '', 2);
const fnSchemaDone = func('Schema done', "global.set('schemaDone', true); return null;", 1, 3);
wire(dbSchema, fnSchemaDone);
wire(injSchema, fnSchema);
wire(fnSchema, dbSchema);

// ------------------------------------------------------------------ poller
const injPoll = add('inject-poll', 'inject', { name: 'Poll every 250 ms', props: [{ p: 'payload' }, { p: 'topic', vt: 'str' }], repeat: '0.25', crontab: '', once: false, onceDelay: 0.1, topic: '', payload: '', payloadType: 'date' }, 1, 0);
const fnPoll = func('Modbus poll', body(function () {
    const V2V = global.get('V2V');
    const st = global.get('gwState') || { online: false, errorSince: null, lastOkTs: null, lastErr: null };
    const status = () => ({ topic: 'status', payload: { online: st.online, errorSince: st.errorSince, lastOkTs: st.lastOkTs, ts: Date.now() } });
    if (!global.get('schemaDone')) { node.send([null, status()]); return null; }
    V2V.withBus(global, (client) => client.readHoldingRegisters(0, 110), 0)
        .then((res) => {
            st.online = true; st.errorSince = null; st.lastOkTs = Date.now(); st.lastErr = null;
            global.set('gwState', st);
            node.send([{ payload: res.data }, status()]);
        })
        .catch((err) => {
            if (err && err.message === 'bus busy') { node.send([null, status()]); return; }
            if (!st.errorSince) st.errorSince = Date.now();
            st.online = false;
            if (st.lastErr !== err.message) { node.warn('Modbus: ' + err.message); st.lastErr = err.message; }
            global.set('gwState', st);
            V2V.resetClient(global);
            node.send([null, status()]);
        });
    return null;
}), 2, 1);
wire(injPoll, fnPoll);

// Decoder: verbatim from spec S12.4
const fnDecode = func('Decode', `const r = msg.payload;                               // 110 unsigned 16-bit values
const s16 = v => (v > 32767 ? v - 65536 : v);
const s32 = (hi, lo) => (hi << 16) | lo;             // signed 32-bit in JS
const gateway = { fw: r[0], status: r[1], neighbors: r[2], rttMs: r[3], acks: r[4],
                  rx: r[5], tx: r[6], crcErr: r[7], gatewayId: r[8], uptime: r[9] };
const vehicles = [];
for (let slot = 0; slot < 4; slot++) {
    const b = 10 + 20 * slot;
    if (r[b] === 0) continue;
    const f = r[b + 10];
    vehicles.push({
        slot, id: r[b],
        lat: s32(r[b + 1], r[b + 2]) / 1e7, lon: s32(r[b + 3], r[b + 4]) / 1e7,
        speedKmh: r[b + 5] / 100 * 3.6, heading: r[b + 6] / 100,
        accel: s16(r[b + 7]) / 100, roll: s16(r[b + 8]), pitch: s16(r[b + 9]),
        flags: f, hardBrake: !!(f & 1), crash: !!(f & 2), rollover: !!(f & 4), hazard: !!(f & 8),
        gpsValid: !!(f & 64), lowBattery: !!(f & 128),
        battery: r[b + 11], rssi: s16(r[b + 12]), snr: s16(r[b + 13]) / 10, pdr: r[b + 14] / 10,
        distance: r[b + 15] === 65535 ? null : r[b + 15] / 10,
        ttc: r[b + 16] === 65535 ? null : r[b + 16] / 10,
        warn: r[b + 17], sats: r[b + 18] >> 4, hdop: r[b + 18] & 15, ageMs: r[b + 19]
    });
}
msg.payload = { ts: Date.now(), gateway, vehicles };
return msg;`, 1, 2);
wire(fnPoll, fnDecode, 0);

// ------------------------------------------------------------------ worldmap
const fnMap = func('Map markers', body(function () {
    const now = Date.now();
    const R = 6371000;
    const toR = (x) => x * Math.PI / 180;
    const dist = (la1, lo1, la2, lo2) => {
        const dLa = toR(la2 - la1), dLo = toR(lo2 - lo1);
        const a = Math.sin(dLa / 2) ** 2 + Math.cos(toR(la1)) * Math.cos(toR(la2)) * Math.sin(dLo / 2) ** 2;
        return 2 * R * Math.asin(Math.sqrt(a));
    };
    const p = msg.payload;
    const hazards = flow.get('hazards') || [];
    const lastMap = flow.get('lastMap') || {};
    const prevIds = flow.get('mapIds') || [];
    const prevHaz = flow.get('prevHaz') || {};
    const sos = flow.get('sosOn') || {};
    const out = [];
    const ids = [];
    let hazChanged = false;
    for (const v of p.vehicles) {
        ids.push(v.id);
        const key = String(v.id);
        if (!v.gpsValid || (v.lat === 0 && v.lon === 0)) { prevHaz[key] = !!v.hazard; continue; }
        // hazard report = rising edge of the hazard flag; reports within 10 m of a hazard merge into it
        if (v.hazard && !prevHaz[key]) {
            let h = hazards.find((x) => dist(x.lat, x.lon, v.lat, v.lon) <= 10);
            if (!h) {
                h = { name: 'HAZ-' + (hazards.length + 1), lat: v.lat, lon: v.lon, count: 0 };
                hazards.push(h);
            }
            h.count += 1;
            hazChanged = true;
            out.push({ payload: { name: h.name, lat: h.lat, lon: h.lon, layer: 'Hazards', icon: 'fa-exclamation-triangle', iconColor: 'yellow', label: 'Road hazard x' + h.count, popup: 'Road hazard: ' + h.count + ' report(s)' } });
        }
        prevHaz[key] = !!v.hazard;
        // SOS marker (red) while crash or rollover is set; removed when cleared
        const sosNow = !!(v.crash || v.rollover);
        if (sosNow) {
            out.push({ payload: { name: 'SOS-V' + v.id, lat: v.lat, lon: v.lon, layer: 'SOS', icon: 'fa-exclamation', iconColor: 'red', label: 'SOS V' + v.id, popup: (v.crash ? 'Crash' : 'Rollover') + ' SOS, vehicle ' + v.id } });
        } else if (sos[key]) {
            out.push({ payload: { name: 'SOS-V' + v.id, deleted: true } });
        }
        sos[key] = sosNow;
        // vehicle marker, at most 2 updates per second per vehicle
        if (!lastMap[key] || now - lastMap[key] >= 500) {
            lastMap[key] = now;
            out.push({ payload: { name: 'V' + v.id, lat: v.lat, lon: v.lon, bearing: v.heading, icon: 'fa-car', iconColor: ['green', 'orange', 'red'][v.warn] || 'green', label: Math.round(v.speedKmh) + ' km/h', layer: 'Vehicles', popup: 'Vehicle ' + v.id + ': ' + v.speedKmh.toFixed(1) + ' km/h, warn ' + v.warn } });
        }
    }
    for (const id of prevIds) {
        if (!ids.includes(id)) out.push({ payload: { name: 'V' + id, deleted: true } });
    }
    if (hazChanged) flow.set('hazards', hazards);
    flow.set('lastMap', lastMap);
    flow.set('mapIds', ids);
    flow.set('prevHaz', prevHaz);
    flow.set('sosOn', sos);
    return [out.length ? out : null];
}), 1, 3);
wire(fnDecode, fnMap);
const worldmap = add('worldmap', 'worldmap', {
    name: 'V2V map', path: '/worldmap', lat: '17.3850', lon: '78.4867', zoom: '16', layer: 'OSMG',
    cluster: '', maxage: '', usermenu: 'show', layers: 'show', panit: 'false', panlock: 'false',
    zoomlock: 'false', hiderightclick: 'false', coords: 'false', showgrid: 'false', showruler: 'false',
    allowFileDrop: 'false', overlist: 'DR,CO,RA,DN', maplist: 'OSMG,OSMC,EsriC,EsriS,UKOS', mapname: ''
}, 0, 4);
wire(fnMap, worldmap);

// ------------------------------------------------------------------ dashboard: Map page
const gMap = group('map', 'map', 'Worldmap', 12, 1);
widget('ui-map-iframe', 'ui-template', gMap, {
    name: 'Map', order: 1, width: 12, height: 12, templateScope: 'local', className: '',
    format: '<template><iframe src="/worldmap" style="width:100%;height:600px;border:0;"></iframe></template>'
}, 0, 5);

// ------------------------------------------------------------------ dashboard: Telemetry page (vehicles 1 and 2)
const telWidgets = [
    { kind: 'gauge', label: 'Speed (km/h)', expr: 'v.speedKmh' },
    { kind: 'text', label: 'Heading', expr: 'f(v.heading, 1) + " deg"' },
    { kind: 'chart', label: 'Acceleration (m/s2)', expr: 'v.accel', topic: 'accel' },
    { kind: 'text', label: 'Roll / pitch', expr: '"roll " + v.roll + " deg, pitch " + v.pitch + " deg"' },
    { kind: 'text', label: 'Battery', expr: 'v.battery + " %" + (v.lowBattery ? "  LOW" : "")' },
    { kind: 'text', label: 'Sats / HDOP', expr: '"sats " + v.sats + ", HDOP " + v.hdop' },
    { kind: 'text', label: 'RSSI / SNR', expr: 'v.rssi + " dBm / " + f(v.snr, 1) + " dB"' },
    { kind: 'text', label: 'PDR', expr: 'f(v.pdr, 1) + " %"' },
    { kind: 'text', label: 'Age', expr: 'v.ageMs + " ms"' },
    { kind: 'text', label: 'GPS / flags', expr: '(v.gpsValid ? "fix" : "NO FIX") + ", flags 0x" + v.flags.toString(16)' }
];
const fieldCode = (ws) => ws.map((w) => `{ payload: ${w.expr}${w.topic ? `, topic: ${JSON.stringify(w.topic)}` : ''} }`).join(',\n    ');
const vehicleSplitter = (vid, ws) => func('Telemetry V' + vid, `const f = (x, d) => (x === null || x === undefined) ? '--' : Number(x).toFixed(d);
const v = msg.payload.vehicles.find((x) => x.id === ${vid});
if (!v) return [${ws.map(() => 'null').join(', ')}];
return [
    ${fieldCode(ws).split('\n').join('\n    ')}
];`, ws.length, 2);
[1, 2].forEach((vid, idx) => {
    const g = group('tel' + vid, 'tel', 'Vehicle ' + vid, 6, 10 + idx);
    const split = vehicleSplitter(vid, telWidgets);
    telWidgets.forEach((w, i) => {
        const key = `tel-${vid}-${i}`;
        let target;
        if (w.kind === 'gauge') {
            target = widget(key, 'ui-gauge', g, { name: w.label, title: w.label, gtype: 'gauge-half', min: 0, max: 60, units: 'km/h', segments: [{ from: 0, color: '#55DBEC' }, { from: 40, color: '#FF9800' }, { from: 50, color: '#F44336' }], width: 6, height: 3, order: i }, 0, 4);
        } else if (w.kind === 'chart') {
            target = widget(key, 'ui-chart', g, { name: w.label, label: w.label, chartType: 'line', category: 'topic', categoryType: 'msg', removeOlder: 60, removeOlderUnit: '1', width: 6, height: 4, order: i, xAxisType: 'time', ymin: '', ymax: '' }, 0, 4);
        } else {
            target = widget(key, 'ui-text', g, { name: w.label, format: '{{msg.payload}}', layout: 'row-spread', fontSize: 14, width: 6, height: 1, order: i }, 0, 4);
        }
        wire(split, target, i);
    });
    wire(fnDecode, split);
});

// ------------------------------------------------------------------ dashboard: V2V Link page
const linkWidgets = [
    { key: 'dist', label: 'Distance to neighbour (m)', expr: 'link ? f(link.distance, 1) : "--"', kind: 'text' },
    { key: 'ttc', label: 'TTC (s)', expr: 'link && link.ttc !== null ? f(link.ttc, 1) : "--"', kind: 'big' },
    { key: 'warn', label: 'Warning', expr: '({ level: ["SAFE", "CAUTION", "CRITICAL"][link ? link.warn : 0] || "SAFE", color: ["#2e7d32", "#ef6c00", "#c62828"][link ? link.warn : 0] || "#2e7d32" })', kind: 'warn' },
    { key: 'lat', label: 'Latency (RTT/2, ms)', expr: 'f(gw.rttMs / 2, 1)', kind: 'text' },
    { key: 'cnt', label: 'RX / TX / CRC errors', expr: '"RX " + gw.rx + "   TX " + gw.tx + "   CRC " + gw.crcErr', kind: 'text' },
    { key: 'nb', label: 'Active neighbours', expr: 'String(gw.neighbors)', kind: 'text' },
    { key: 'gst', label: 'Gateway status', expr: '"radio " + ((gw.status & 1) ? "OK" : "FAIL") + " | GPS " + ((gw.status & 2) ? "fix" : "no fix") + " | IMU " + ((gw.status & 4) ? "OK" : "FAIL") + " | SIM " + ((gw.status & 8) ? "on" : "off")', kind: 'text' },
    { key: 'gid', label: 'Gateway ID / FW / uptime', expr: '"V" + gw.gatewayId + "  fw " + (gw.fw / 100).toFixed(2) + "  up " + gw.uptime + " s"', kind: 'text' }
];
const fnLink = func('Link widgets', `const f = (x, d) => (x === null || x === undefined || Number.isNaN(x)) ? '--' : Number(x).toFixed(d);
const gw = msg.payload.gateway;
const link = msg.payload.vehicles.filter((x) => x.slot > 0 && x.distance !== null)[0] || null;
return [
    ${linkWidgets.map((w) => `{ payload: ${w.expr} }`).join(',\n    ')}
];`, linkWidgets.length, 2);
wire(fnDecode, fnLink);
const gLink = group('link', 'link', 'Link quality', 6, 20);
linkWidgets.forEach((w, i) => {
    let t;
    if (w.kind === 'big') t = widget('link-' + w.key, 'ui-text', gLink, { name: w.label, format: '{{msg.payload}}', layout: 'row-spread', fontSize: 48, width: 6, height: 2, order: i }, 0, 4);
    else if (w.kind === 'warn') t = widget('link-' + w.key, 'ui-template', gLink, {
        name: w.label, width: 6, height: 2, order: i, templateScope: 'local', className: '',
        format: '<template><div :style="{background: color, color: \'#fff\', borderRadius: \'8px\', padding: \'12px\', textAlign: \'center\', fontSize: \'30px\', fontWeight: \'700\'}">{{ level }}</div></template>\n<script>\nexport default {\n  props: [\'msg\'],\n  data() { return { level: \'SAFE\', color: \'#2e7d32\' }; },\n  watch: { msg: { deep: true, handler(m) { if (m && m.payload) { this.level = m.payload.level; this.color = m.payload.color; } } } }\n}\n</script>'
    }, 0, 4);
    else t = widget('link-' + w.key, 'ui-text', gLink, { name: w.label, format: '{{msg.payload}}', layout: 'row-spread', fontSize: 14, width: 6, height: 1, order: i }, 0, 4);
    wire(fnLink, t, i);
});

// ------------------------------------------------------------------ alarms: logic, DB, UI
const fnAlarm = func('Alarm logic', body(function () {
    const now = Date.now();
    const t = msg.topic || '';
    const pl = msg.payload;
    let entries = flow.get('alarms') || [];
    let selected = flow.get('ackSel') || null;
    let latest = flow.get('latest') || null;
    let gw = flow.get('gwStatus') || { online: true, errorSince: null };
    const seen = flow.get('seenIds') || [];
    const gpsSince = flow.get('gpsSince') || {};
    const outIns = [], outClr = [], outAck = [], notes = [];

    const ackWhere = (pred) => {
        for (const e of entries) {
            if (!e.acked && pred(e)) {
                e.acked = true; e.ack_ts = now;
                outAck.push({ params: { $ack_ts: now, $ts: e.ts, $vehicle_id: e.vehicle_id, $type: e.type } });
            }
        }
    };
    if (t === 'status') { gw = pl; flow.set('gwStatus', gw); }
    else if (t === 'ackSelect') { selected = Number(pl) || null; }
    else if (t === 'ack_all') { ackWhere(() => true); }
    else if (t === 'ack_sel') { if (selected) ackWhere((e) => e.ts === selected); }
    else if (pl && pl.gateway && pl.vehicles) { latest = pl; flow.set('latest', pl); }

    // Conditions to evaluate: key -> {type, vid, sev, lat, lon, on}
    const want = {};
    const add = (type, vid, sev, lat, lon, on) => { want[vid + ':' + type] = { type, vid, sev, lat, lon, on: !!on }; };
    const online = gw.online !== false;
    add('Gateway offline', 0, 'CRITICAL', 0, 0, !online && !!gw.errorSince && now - gw.errorSince > 2000);
    if (online && latest) {
        const present = [];
        for (const v of latest.vehicles) {
            present.push(v.id);
            if (!seen.includes(v.id)) seen.push(v.id);
            const la = v.lat, lo = v.lon;
            if (!v.gpsValid) { if (!gpsSince[v.id]) gpsSince[v.id] = now; } else { delete gpsSince[v.id]; }
            add('Crash / SOS', v.id, 'CRITICAL', la, lo, v.crash);
            add('Rollover', v.id, 'CRITICAL', la, lo, v.rollover);
            add('Collision risk', v.id, 'HIGH', la, lo, v.warn === 2);
            add('Hard braking', v.id, 'MEDIUM', la, lo, v.hardBrake);
            add('Road hazard', v.id, 'MEDIUM', la, lo, v.hazard);
            add('V2V link lost', v.id, 'MEDIUM', la, lo, v.ageMs > 2000);
            add('GPS fix lost', v.id, 'LOW', la, lo, !v.gpsValid && !!gpsSince[v.id] && now - gpsSince[v.id] > 5000);
            add('Low battery', v.id, 'LOW', la, lo, v.lowBattery);
        }
        for (const id of seen) {
            if (!present.includes(id)) add('V2V link lost', id, 'MEDIUM', 0, 0, true);
        }
    }

    // Rising edge -> new active entry + INSERT; falling edge -> cleared_ts
    for (const key of Object.keys(want)) {
        const w = want[key];
        const k = w.vid + ':' + w.type;
        const idx = entries.findIndex((e) => e.key === k && !e.cleared);
        if (w.on && idx < 0) {
            entries.push({ key: k, type: w.type, vehicle_id: w.vid, severity: w.sev, ts: now, lat: w.lat, lon: w.lon, cleared: false, cleared_ts: null, acked: false, ack_ts: null, critical: w.sev === 'CRITICAL' });
            outIns.push({ params: { $ts: now, $vehicle_id: w.vid, $type: w.type, $severity: w.sev, $lat: w.lat, $lon: w.lon } });
            notes.push('ALARM ' + w.sev + ': ' + w.type + (w.vid ? ' (V' + w.vid + ')' : ' (gateway)'));
        } else if (!w.on && idx >= 0) {
            const e = entries[idx];
            e.cleared = true; e.cleared_ts = now;
            outClr.push({ params: { $cleared_ts: now, $ts: e.ts, $vehicle_id: e.vehicle_id, $type: e.type } });
        }
    }
    // Non-critical cleared alarms leave the list; CRITICAL ones stay until acknowledged
    entries = entries.filter((e) => !e.cleared || (e.critical && !e.acked));

    flow.set('alarms', entries);
    flow.set('ackSel', selected);
    flow.set('seenIds', seen);
    flow.set('gpsSince', gpsSince);

    const rows = entries.map((e) => ({
        time: new Date(e.ts).toLocaleTimeString(), vehicle: e.vehicle_id, type: e.type, severity: e.severity,
        state: (e.cleared ? 'CLEARED' : 'ACTIVE') + (e.acked ? ', acked' : ', not acked')
    }));
    flow.set('visible', rows);
    const rowsJson = JSON.stringify(rows);
    const tblOut = flow.get('tblJson') !== rowsJson ? [{ payload: rows }] : [];
    flow.set('tblJson', rowsJson);
    const opts = entries.map((e) => ({ label: new Date(e.ts).toLocaleTimeString() + ' V' + e.vehicle_id + ' ' + e.type + ' [' + (e.cleared ? 'cleared' : 'active') + ']', value: String(e.ts) }));
    const optsJson = JSON.stringify(opts);
    const dropOut = flow.get('dropJson') !== optsJson ? [{ options: opts.length ? opts : [{ label: '(none)', value: '' }], payload: selected ? String(selected) : '' }] : [];
    flow.set('dropJson', optsJson);

    return [outIns, outClr, outAck, notes.map((n) => ({ payload: n })), tblOut, dropOut];
}), 6, 5);
wire(fnDecode, fnAlarm);
wire(fnPoll, fnAlarm, 1);

const dbAlarmIns = dbNode('DB alarm insert', 'prepared', SQL_ALM_INS, 6);
const dbAlarmClr = dbNode('DB alarm clear', 'prepared', SQL_ALM_CLR, 6);
const dbAlarmAck = dbNode('DB alarm ack', 'prepared', SQL_ALM_ACK, 6);
wire(fnAlarm, dbAlarmIns, 0);
wire(fnAlarm, dbAlarmClr, 1);
wire(fnAlarm, dbAlarmAck, 2);

const gAlarm = group('alarm', 'alarm', 'Active alarms', 12, 30);
const tblActive = widget('tbl-active', 'ui-table', gAlarm, { name: 'Active alarms', label: 'Active alarms (CRITICAL stay until acknowledged)', autocols: true, showSearch: false, deselect: true, selectionType: 'none', width: 12, height: 6, order: 1, maxrows: 0, passthru: false, action: 'replace' }, 0, 5);
const dropAck = widget('drop-ack', 'ui-dropdown', gAlarm, { name: 'ackSelect', label: 'Alarm to acknowledge', topic: 'ackSelect', topicType: 'str', options: [{ label: '(none)', value: '' }], width: 6, height: 1, order: 2, passthru: false, multiple: false, clearable: false, payload: '', className: '' }, 1, 5);
const btnAckSel = widget('btn-ack-sel', 'ui-button', gAlarm, { name: 'ack_sel', label: 'Acknowledge selected', payload: 'click', payloadType: 'str', topic: 'ack_sel', topicType: 'str', width: 3, height: 1, order: 3, icon: '', className: '' }, 1, 5);
const btnAckAll = widget('btn-ack-all', 'ui-button', gAlarm, { name: 'ack_all', label: 'Acknowledge all', payload: 'click', payloadType: 'str', topic: 'ack_all', topicType: 'str', width: 3, height: 1, order: 4, icon: '', className: '' }, 1, 5);
wire(fnAlarm, tblActive, 4);
wire(fnAlarm, dropAck, 5);
wire(dropAck, fnAlarm);
wire(btnAckSel, fnAlarm);
wire(btnAckAll, fnAlarm);

const gHist = group('hist', 'alarm', 'Alarm history (last 100, from SQLite)', 12, 31);
const injHist = add('inject-hist', 'inject', { name: 'History every 10 s', props: [{ p: 'payload' }, { p: 'topic', vt: 'str' }], repeat: '10', crontab: '', once: true, onceDelay: 2, topic: 'SELECT id, ts, vehicle_id, type, severity, acked, ack_ts, cleared_ts FROM alarms ORDER BY id DESC LIMIT 100', payload: '', payloadType: 'date' }, 1, 0);
const dbHist = dbNode('DB alarm history', 'msg.topic', '', 6);
const tblHist = widget('tbl-hist', 'ui-table', gHist, { name: 'Alarm history', label: 'Alarm history', autocols: true, showSearch: true, deselect: true, selectionType: 'none', width: 12, height: 6, order: 1, maxrows: 0, passthru: false, action: 'replace' }, 0, 5);
wire(injHist, dbHist);
wire(dbHist, tblHist);

const gExport = group('export', 'alarm', 'Export (SQLite historian)', 12, 32);
widget('link-export', 'ui-template', gExport, {
    name: 'CSV export', width: 12, height: 1, order: 1, templateScope: 'local', className: '',
    format: '<template><div style="font-size:15px;"><a href="/v2v/export.csv?table=alarms" target="_blank">Download alarms CSV</a> &nbsp;|&nbsp; <a href="/v2v/export.csv?table=telemetry" target="_blank">Download telemetry CSV</a></div></template>'
}, 0, 5);

const notif = add('notify', 'ui-notification', { ui: base.id, name: 'Alarm notices', position: 'top right', colorDefault: true, color: null, displayTime: '5', showCountdown: true, outputs: 0, allowDismiss: true, dismissText: 'Dismiss', allowConfirm: false, confirmText: 'Confirm', showInput: false, inputType: 'text', inputLabel: '', raw: false, className: '' }, 0, 6);
wire(fnAlarm, notif, 3);

// ------------------------------------------------------------------ historian: 1 Hz per vehicle
const fnHist = func('Historian 1 Hz', body(function () {
    const now = Date.now();
    const last = flow.get('lastTel') || {};
    const out = [];
    for (const v of msg.payload.vehicles) {
        if (last[v.id] && now - last[v.id] < 1000) continue;
        last[v.id] = now;
        out.push({ params: { $ts: now, $vehicle_id: v.id, $lat: v.lat, $lon: v.lon, $speed_kmh: v.speedKmh, $heading: v.heading, $accel: v.accel, $roll: v.roll, $pitch: v.pitch, $flags: v.flags, $battery: v.battery, $rssi: v.rssi, $pdr: v.pdr, $distance: v.distance, $ttc: v.ttc, $warn: v.warn } });
    }
    flow.set('lastTel', last);
    return [out.length ? out : null];
}), 1, 3);
wire(fnDecode, fnHist);
const dbTel = dbNode('DB telemetry insert', 'prepared', SQL_TEL, 6);
wire(fnHist, dbTel);

// ------------------------------------------------------------------ commands
const gCtrl = group('ctrl', 'ctrl', 'Commands', 6, 40);
const btn = (key, label, payload, order) => widget('btn-' + key, 'ui-button', gCtrl, { name: key, label, payload, payloadType: 'str', topic: 'cmd', topicType: 'str', width: 3, height: 1, order, icon: '', className: '' }, 1, 5);
const btns = [btn('hazard', 'Hazard alert (1)', 'hazard', 1), btn('clearsos', 'Clear SOS (3)', 'clearsos', 2), btn('buzzer', 'Buzzer test (4)', 'buzzer', 3), btn('ping', 'Ping (5)', 'ping', 4)];
const gSpeed = group('speed', 'ctrl', 'Speed advisory and target', 6, 41);
const speedIn = widget('speed-in', 'ui-number-input', gSpeed, { name: 'speed', label: 'Speed limit (km/h)', topic: 'speed', topicType: 'str', min: 0, max: 200, width: 3, height: 1, order: 1, passthru: false, sendOnDelay: false, sendOnBlur: true, sendOnEnter: true, className: '', clearable: false, icon: '', iconPosition: 'left', iconInnerPosition: 'inside', spinner: 'default', tooltip: '' }, 1, 5);
const btnAdv = widget('btn-advisory', 'ui-button', gSpeed, { name: 'advisory', label: 'Speed advisory (2)', payload: 'advisory', payloadType: 'str', topic: 'cmd', topicType: 'str', width: 3, height: 1, order: 2, icon: '', className: '' }, 1, 5);
const dropTarget = widget('drop-target', 'ui-dropdown', gSpeed, { name: 'target', label: 'Target', topic: 'target', topicType: 'str', options: [{ label: 'All', value: '0' }, { label: 'Vehicle 1', value: '1' }, { label: 'Vehicle 2', value: '2' }], width: 6, height: 1, order: 3, passthru: false, multiple: false, clearable: false, payload: '', className: '' }, 1, 5);

const fnCmd = func('Command builder', body(function () {
    const codes = { hazard: 1, advisory: 2, clearsos: 3, buzzer: 4, ping: 5 };
    const fail = (text) => { msg.statusCode = 400; msg.headers = { 'Content-Type': 'application/json' }; msg.payload = { ok: false, error: text }; return [null, msg]; };
    if (msg.req) {
        const q = msg.req.query || {};
        const code = Number(q.code), param = Number(q.param || 0), target = Number(q.target || 0);
        if (!Number.isInteger(code) || code < 1 || code > 5) return fail('code must be 1..5');
        if (!Number.isInteger(param) || param < 0 || param > 65535) return fail('param must be 0..65535');
        if (!Number.isInteger(target) || target < 0 || target > 254) return fail('target must be 0 (all) or a vehicle id 1..254');
        msg.payload = [code, param, target, 1];
        return [msg, null];
    }
    if (msg.topic === 'target') { flow.set('target', Number(msg.payload) || 0); return [null, null]; }
    if (msg.topic === 'speed') { flow.set('speed', Number(msg.payload) || 0); return [null, null]; }
    if (msg.topic === 'cmd') {
        const name = String(msg.payload);
        if (!Object.prototype.hasOwnProperty.call(codes, name)) return [null, null];
        const code = codes[name];
        const param = code === 2 ? (flow.get('speed') || 50) : 0;
        const target = flow.get('target') || 0;
        msg.payload = [code, param, target, 1];
        return [msg, null];
    }
    return [null, null];
}), 2, 3);
wire(btns[0], fnCmd); btns.slice(1).forEach((b) => wire(b, fnCmd));
wire(btnAdv, fnCmd);
wire(speedIn, fnCmd);
wire(dropTarget, fnCmd);

const fnWrite = func('Modbus write', body(function () {
    const V2V = global.get('V2V');
    const regs = msg.payload;
    const isHttp = !!msg.res;
    V2V.withBus(global, (client) => client.writeRegisters(100, regs), 2000)
        .then(() => {
            if (isHttp) node.send([{ payload: { ok: true, sent: regs }, req: msg.req, res: msg.res }, null]);
            else node.send([null, { payload: 'Command sent: code ' + regs[0] + ', param ' + regs[1] + ', target ' + regs[2] }]);
        })
        .catch((err) => {
            if (isHttp) node.send([{ statusCode: 503, payload: { ok: false, error: err.message }, req: msg.req, res: msg.res }, null]);
            else node.send([null, { payload: 'Command failed: ' + err.message }]);
        });
    return null;
}), 2, 4);
wire(fnCmd, fnWrite, 0);
wire(fnWrite, notif, 1);

// ------------------------------------------------------------------ HTTP endpoints
const httpRes = add('http-res', 'http response', { name: 'HTTP reply', statusCode: '', headers: {} }, 0, 7);
const httpStatus = add('http-status', 'http in', { name: 'GET /v2v/status', url: '/v2v/status', method: 'get', upload: false, swaggerDoc: '' }, 1, 0);
const fnStatus = func('Build status', body(function () {
    msg.headers = { 'Content-Type': 'application/json' };
    msg.payload = {
        ts: Date.now(),
        source: env.get('V2V_SOURCE'),
        gateway: flow.get('gwStatus') || null,
        latest: flow.get('latest') || null,
        activeAlarms: flow.get('visible') || []
    };
    return msg;
}), 1, 1);
wire(httpStatus, fnStatus);
wire(fnStatus, httpRes);

const httpCmd = add('http-cmd', 'http in', { name: 'GET /v2v/command', url: '/v2v/command', method: 'get', upload: false, swaggerDoc: '' }, 1, 0);
wire(httpCmd, fnCmd);
wire(fnCmd, httpRes, 1);
wire(fnWrite, httpRes, 0);

const httpExp = add('http-export', 'http in', { name: 'GET /v2v/export.csv', url: '/v2v/export.csv', method: 'get', upload: false, swaggerDoc: '' }, 1, 0);
const fnExpReq = func('Export request', body(function () {
    const cols = {
        telemetry: ['ts', 'vehicle_id', 'lat', 'lon', 'speed_kmh', 'heading', 'accel', 'roll', 'pitch', 'flags', 'battery', 'rssi', 'pdr', 'distance', 'ttc', 'warn'],
        alarms: ['id', 'ts', 'vehicle_id', 'type', 'severity', 'lat', 'lon', 'acked', 'ack_ts', 'cleared_ts']
    };
    const table = (msg.req.query || {}).table;
    if (!Object.prototype.hasOwnProperty.call(cols, table)) {
        msg.statusCode = 400; msg.headers = { 'Content-Type': 'application/json' };
        msg.payload = { ok: false, error: 'table must be telemetry or alarms' };
        return [null, msg];
    }
    msg.table = table;
    msg.cols = cols[table];
    msg.topic = table === 'telemetry' ? 'SELECT * FROM telemetry ORDER BY ts' : 'SELECT * FROM alarms ORDER BY id';
    return [msg, null];
}), 2, 1);
const dbExport = dbNode('DB export query', 'msg.topic', '', 2);
const fnCsv = func('To CSV', body(function () {
    const rows = Array.isArray(msg.payload) ? msg.payload : [];
    const cols = msg.cols;
    const esc = (v) => {
        if (v === null || v === undefined) return '';
        const s = String(v);
        return /[",\r\n]/.test(s) ? '"' + s.replace(/"/g, '""') + '"' : s;
    };
    const lines = [cols.join(',')].concat(rows.map((r) => cols.map((c) => esc(r[c])).join(',')));
    msg.headers = { 'Content-Type': 'text/csv; charset=utf-8', 'Content-Disposition': 'attachment; filename="v2v_' + msg.table + '.csv"' };
    msg.payload = lines.join('\r\n') + '\r\n';
    return msg;
}), 1, 3);
wire(httpExp, fnExpReq);
wire(fnExpReq, dbExport, 0);
wire(fnExpReq, httpRes, 1);
wire(dbExport, fnCsv);
wire(fnCsv, httpRes);

// ------------------------------------------------------------------ write flows.json
const out = path.join(DIR, 'flows.json');
fs.writeFileSync(out, JSON.stringify(nodes, null, 4) + '\n');
console.log('wrote ' + out + ' (' + nodes.length + ' nodes)');
console.log('wrote ' + path.join(DIR, '..', 'historian', 'schema.sql'));
