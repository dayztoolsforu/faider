// Faider for Stream Deck: keys and dials on the Faider desk, through its local control API
// (src/engine/ControlServer.h, http://127.0.0.1:9123). Plain Node, no packages: the Stream Deck
// app starts it with -port -pluginUUID -registerEvent -info and talks over a WebSocket, for
// which a small client is below.
'use strict';
const net = require('net');
const crypto = require('crypto');

const API = 'http://127.0.0.1:9123/api';
const args = {};
for (let i = 2; i + 1 < process.argv.length; i += 2) args[process.argv[i].replace(/^-/, '')] = process.argv[i + 1];

// ---- A WebSocket client (RFC 6455): text frames, masked as a client must. ----
function connect(port, onOpen, onMessage) {
  const key = crypto.randomBytes(16).toString('base64');
  const socket = net.connect(port, '127.0.0.1', () => {
    socket.write(`GET / HTTP/1.1\r\nHost: 127.0.0.1:${port}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n` +
                 `Sec-WebSocket-Key: ${key}\r\nSec-WebSocket-Version: 13\r\n\r\n`);
  });
  let buffer = Buffer.alloc(0), open = false;
  const frame = (opcode, data) => {
    const first = 0x80 | opcode, mask = crypto.randomBytes(4);
    const head = data.length < 126 ? Buffer.from([first, 0x80 | data.length])
               : data.length < 65536 ? Buffer.from([first, 0xfe, data.length >> 8, data.length & 255])
               : (() => { const h = Buffer.alloc(10); h[0] = first; h[1] = 0xff; h.writeBigUInt64BE(BigInt(data.length), 2); return h; })();
    for (let i = 0; i < data.length; i++) data[i] ^= mask[i % 4];
    socket.write(Buffer.concat([head, mask, data]));
  };
  const send = (text) => frame(1, Buffer.from(text));
  socket.on('data', (chunk) => {
    buffer = Buffer.concat([buffer, chunk]);
    if (!open) {
      const end = buffer.indexOf('\r\n\r\n');
      if (end < 0) return;
      // Anything but "101 Switching Protocols" isn't a WebSocket: give up, and the Stream Deck app
      // starts the plugin again.
      const status = buffer.subarray(0, buffer.indexOf('\r\n')).toString();
      if (!status.includes(' 101 ')) { socket.destroy(); process.exit(1); }
      buffer = buffer.subarray(end + 4);
      open = true;
      onOpen(send);
    }
    while (buffer.length >= 2) {
      let length = buffer[1] & 127, offset = 2;
      if (length === 126) { if (buffer.length < 4) return; length = buffer.readUInt16BE(2); offset = 4; }
      else if (length === 127) { if (buffer.length < 10) return; length = Number(buffer.readBigUInt64BE(2)); offset = 10; }
      if (buffer.length < offset + length) return;
      const opcode = buffer[0] & 15, payload = buffer.subarray(offset, offset + length);
      buffer = buffer.subarray(offset + length);
      if (opcode === 1) onMessage(payload.toString());
      else if (opcode === 8) process.exit(0);   // the Stream Deck app closed: so do we
      else if (opcode === 9) frame(10, Buffer.from(payload));   // ping: pong it back
      // continuation and pong frames: nothing to do (the app sends its JSON whole)
    }
  });
  socket.on('close', () => process.exit(0));
  socket.on('error', () => process.exit(1));
}

// ---- Faider ----
let state = null;   // the last /api/state, or null while Faider isn't answering
const keys = new Map();   // context -> { action, settings }

const path = (s) => `${s.kind || 'input'}/${encodeURIComponent(s.name || '')}`;
async function command(route, value) {
  try {
    await fetch(`${API}/${route}${value === undefined ? '' : `?value=${value}`}`,
                { method: 'POST', headers: { 'X-Faider': '1' }, signal: AbortSignal.timeout(1500) });
  } catch { /* Faider isn't running (or is stuck): the keys show it on the next poll */ }
}
function strip(s) {
  if (!state) return null;
  const list = { input: state.inputs, bus: state.buses, output: state.outputs }[s.kind || 'input'] || [];
  return list.find((x) => x.name.toUpperCase() === String(s.name || '').toUpperCase()) || null;
}

connect(Number(args.port), (send) => {
  const out = (event, context, payload) => send(JSON.stringify({ event, context, payload }));
  send(JSON.stringify({ event: args.registerEvent, uuid: args.pluginUUID }));

  // Keys follow the desk: lit while muted / soloed / the active scene; faders show their level.
  const shown = new Map();
  const show = (context, title, lit) => {
    const was = shown.get(context) || {};
    if (was.title !== title) out('setTitle', context, { title });
    if (lit !== undefined && was.lit !== lit) out('setState', context, { state: lit ? 1 : 0 });
    shown.set(context, { title, lit });
  };
  const refresh = () => { try {   // (a surprise in the state mustn't take the plugin down)
    for (const [context, { action, settings }] of keys) {
      const kind = action.split('.').pop(), s = strip(settings);
      const name = (settings.name || '').toUpperCase();
      if (!state) show(context, 'FAIDER\nOFF', false);
      else if (kind === 'mute') show(context, `${name}\nMUTE`, !!(s && s.mute));
      else if (kind === 'solo') show(context, `${name}\nSOLO`, !!(s && s.solo));
      else if (kind === 'scene') show(context, name, state.activeScene.toUpperCase() === name);
      else if (kind === 'macro') show(context, name);
      else if (kind === 'fader') {
        const text = s ? (s.fader <= -60 ? 'OFF' : `${s.fader.toFixed(1)} dB`) : '--';
        if ((shown.get(context) || {}).title !== `${name}\n${text}`)   // the dial's screen too (Stream Deck +)
          out('setFeedback', context, { title: name, value: text, indicator: s ? Math.max(0, Math.min(100, (s.fader + 60) / 72 * 100)) : 0 });
        show(context, `${name}\n${text}`);
      }
    }
  } catch { /* the next poll tries again */ } };
  // One request at a time, each given up after 1.5 s: a slow Faider doesn't pile up fetches, and
  // a late answer can't land on top of a newer one (a second caller shares the one in flight).
  let polling = null;
  const poll = () => polling || (polling = (async () => {
    try {
      // Only a real answer counts: an error page (403, 404, 500) is "not answering".
      const r = await fetch(`${API}/state`, { signal: AbortSignal.timeout(1500) });
      const body = r.ok ? await r.json() : null;
      state = body && Array.isArray(body.inputs) ? body : null;
    }
    catch { state = null; }
    finally { polling = null; }
  })());
  setInterval(async () => { if (polling) return; await poll(); refresh(); }, 400);

  const level = (settings) => Number(settings.level ?? 0);
  const handlers = {
    keyDown: (kind, settings) => {
      if (kind === 'mute' || kind === 'solo') command(`${path(settings)}/${kind}`);
      else if (kind === 'scene') command(`scene/${encodeURIComponent(settings.name || '')}`);
      else if (kind === 'macro') command(`macro/${encodeURIComponent(settings.name || '')}`, 1);
      else if (kind === 'fader') command(`${path(settings)}/fader`, level(settings));
    },
    keyUp: (kind, settings) => {
      if (kind === 'macro') command(`macro/${encodeURIComponent(settings.name || '')}`, 0);
    },
    dialRotate: (kind, settings, payload) => {
      const s = strip(settings);
      if (s) command(`${path(settings)}/fader`, Math.max(-60, Math.min(12, Math.max(s.fader, -61) + payload.ticks * Number(settings.step ?? 1))));
    },
    dialDown: (kind, settings) => command(`${path(settings)}/fader`, level(settings)),
  };

  connect.onMessage = (text) => {
    try {
      const m = JSON.parse(text), kind = (m.action || '').split('.').pop();
      const settings = (m.payload && m.payload.settings) || (keys.get(m.context) || {}).settings || {};
      if (m.event === 'willAppear' || m.event === 'didReceiveSettings') keys.set(m.context, { action: m.action, settings });
      else if (m.event === 'willDisappear') { keys.delete(m.context); shown.delete(m.context); }
      else if (m.event === 'propertyInspectorDidAppear' || (m.event === 'sendToPlugin' && m.payload && m.payload.refresh))
        poll().then(() => out('sendToPropertyInspector', m.context, { state }));   // the desk's names, for the menus
      else if (handlers[m.event]) handlers[m.event](kind, settings, m.payload || {});
      if (m.event === 'didReceiveSettings') { shown.delete(m.context); refresh(); }
    } catch { /* a frame that isn't the JSON we expect: skip it rather than take the plugin down */ }
  };
}, (text) => connect.onMessage && connect.onMessage(text));
