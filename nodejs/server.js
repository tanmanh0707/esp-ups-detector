// app.js - Node.js thuần
require('dotenv').config();
const dgram = require('dgram');
const net = require('net');
const https = require('https');
const { URL } = require('url');
const querystring = require('querystring');

// ================= CONFIG =================
const CLOUD_URL = process.env.CLOUD_URL || 'https://myapi';
const CLOUD_USERNAME = process.env.CLOUD_USERNAME || 'admin';
const CLOUD_PASSWORD = process.env.CLOUD_PASSWORD || 'admin';

const LOGIN_URL = `${CLOUD_URL}/api/v1/login`;
const TEMPLATE_URL = `${CLOUD_URL}/api/v1/email-templates/power-cut-template`;
const SMS_URL = `${CLOUD_URL}/api/v1/sms-notifications`;

const UDP_PORT = 7792;
const UDP_BROADCAST_ADDR = '255.255.255.255';
const UDP_BROADCAST_PAYLOAD = 'Where are you?';
const UDP_BROADCAST_INTERVAL_MS = 1000;

const TCP_RECONNECT_BASE_MS = 1000;
const TCP_RECONNECT_MAX_MS = 15000;
// ==========================================

// ========== STATE ==========
let accessToken = null;
let tokenExpireAt = 0;

let udpSocket = null;
let udpBroadcastTimer = null;
let discoveredPeer = { ip: null, port: null };

let tcpSocket = null;
let tcpReconnectTimer = null;
let tcpReconnectDelay = TCP_RECONNECT_BASE_MS;

let lastStatus = 'on';
let notifyInterval = null;
let lastTemplate = null;
// ===========================

// ====== HTTP REQUEST (Node thuần) ======
function httpRequest(options, body = null) {
  return new Promise((resolve, reject) => {
    const req = https.request(options, (res) => {
      let chunks = [];
      res.on('data', (c) => chunks.push(c));
      res.on('end', () => {
        const buf = Buffer.concat(chunks);
        resolve({ statusCode: res.statusCode, body: buf.toString('utf8') });
      });
    });
    req.on('error', (err) => reject(err));
    if (body) req.write(body);
    req.end();
  });
}

function parseUrl(u) {
  const parsed = new URL(u);
  return {
    host: parsed.hostname,
    port: parsed.port || (parsed.protocol === 'https:' ? 443 : 80),
    path: parsed.pathname + (parsed.search || ''),
  };
}

async function doLogin() {
  const bodyObj = { username: CLOUD_USERNAME, password: CLOUD_PASSWORD };
  const bodyStr = querystring.stringify(bodyObj);
  const p = parseUrl(LOGIN_URL);
  const opts = {
    hostname: p.host,
    port: p.port,
    path: p.path,
    method: 'POST',
    headers: {
      'Content-Type': 'application/x-www-form-urlencoded',
      'Content-Length': Buffer.byteLength(bodyStr),
    },
    timeout: 10000,
  };
  const resp = await httpRequest(opts, bodyStr);
  if (resp.statusCode !== 200) throw new Error(`Login failed ${resp.statusCode}`);
  const data = JSON.parse(resp.body);
  accessToken = data.access_token;
  tokenExpireAt = Date.now() + data.expires_in * 1000 - 30000;
  console.log(`[AUTH] New token, expires in ${data.expires_in}s`);
}

async function ensureLogin() {
  if (accessToken && tokenExpireAt > Date.now()) return;
  await doLogin();
}

async function authRequest(method, url, body = null) {
  await ensureLogin();
  const p = parseUrl(url);
  const opts = {
    hostname: p.host,
    port: p.port,
    path: p.path,
    method,
    headers: { Authorization: `Bearer ${accessToken}` },
    timeout: 10000,
  };
  let bodyStr = null;
  if (body) {
    bodyStr = JSON.stringify(body);
    opts.headers['Content-Type'] = 'application/json';
    opts.headers['Content-Length'] = Buffer.byteLength(bodyStr);
  }

  let resp = await httpRequest(opts, bodyStr);
  if (resp.statusCode === 401) {
    console.warn('[AUTH] 401 detected -> refreshing token...');
    accessToken = null;
    await ensureLogin();
    opts.headers.Authorization = `Bearer ${accessToken}`;
    resp = await httpRequest(opts, bodyStr);
  }
  if (resp.statusCode < 200 || resp.statusCode >= 300) {
    throw new Error(`${method} ${url} failed ${resp.statusCode}: ${resp.body}`);
  }
  return resp.body ? JSON.parse(resp.body) : {};
}
// =======================================

// ===== UDP DISCOVERY =====
function startUdp() {
  udpSocket = dgram.createSocket('udp4');
  udpSocket.on('message', (msg, rinfo) => {
    if (msg.toString().trim() === 'Here I am') {
      if (!discoveredPeer.ip) {
        discoveredPeer = { ip: rinfo.address, port: rinfo.port };
        console.log(`[UDP] Found peer ${discoveredPeer.ip}:${discoveredPeer.port}`);
        stopUdpBroadcast();
        connectTcp();
      }
    }
  });
  udpSocket.bind(UDP_PORT, () => {
    udpSocket.setBroadcast(true);
    console.log('[UDP] Broadcasting...');
    udpBroadcastTimer = setInterval(() => {
      udpSocket.send(UDP_BROADCAST_PAYLOAD, UDP_PORT, UDP_BROADCAST_ADDR);
    }, UDP_BROADCAST_INTERVAL_MS);
  });
}
function stopUdpBroadcast() {
  if (udpBroadcastTimer) {
    clearInterval(udpBroadcastTimer);
    udpBroadcastTimer = null;
  }
}
// ==========================

// ===== TCP CLIENT =====
function sendTcp(obj) {
  try {
    if (tcpSocket && !tcpSocket.destroyed) {
      const payload = JSON.stringify(obj);
      tcpSocket.write(payload + "\n");
      console.log("Sent TCP:", payload);
    } else {
      console.warn("TCP socket is not connected");
    }
  } catch (err) {
    console.error("Failed to send TCP:", err.message);
  }
}

function connectTcp() {
  if (!discoveredPeer.ip) return;
  if (tcpSocket) { tcpSocket.destroy(); tcpSocket = null; }
  tcpSocket = new net.Socket();
  let buf = '';

  tcpSocket.on('connect', () => {
    console.log('[TCP] Connected');
    tcpReconnectDelay = TCP_RECONNECT_BASE_MS;
  });

  tcpSocket.on('data', (chunk) => {
    buf += chunk.toString();
    try {
      const obj = JSON.parse(buf.trim());
      if (obj.status) handleStatus(obj.status);
      buf = '';
    } catch { /* wait more */ }
  });

  tcpSocket.on('close', () => {
    console.log('[TCP] Closed, reconnecting...');
    scheduleReconnect();
  });


  tcpSocket.on("error", (err) => {
    console.log("TCP error:", err.message);
    tcpSocket.destroy();
    scheduleReconnect();
  });

  tcpSocket.setKeepAlive(true, 3000);
  tcpSocket.connect(discoveredPeer.port, discoveredPeer.ip);
}
function scheduleReconnect() {
  if (tcpReconnectTimer) return;
  tcpReconnectTimer = setTimeout(() => {
    tcpReconnectTimer = null;
    tcpReconnectDelay = Math.min(tcpReconnectDelay * 2, TCP_RECONNECT_MAX_MS);
    connectTcp();
  }, tcpReconnectDelay);
}
// ========================

// ===== STATUS HANDLER =====
async function handleStatus(status) {
  status = status.toLowerCase();
  if (status === lastStatus) {
    sendTcp({status: lastStatus});
    return;
  }
  console.log(`[STATUS] ${lastStatus} -> ${status}`);
  if (lastStatus === 'on' && status === 'off') {
    sendTcp({status: status});
    await onPowerCut();
  }
  if (lastStatus === 'off' && status === 'on') {
    sendTcp({status: status});
    onPowerRestore();
  }
  lastStatus = status;
}

async function onPowerCut() {
  console.log('[ALERT] Fetching template...');
  const resp = await authRequest('GET', TEMPLATE_URL);
  const tpl = resp.data;
  lastTemplate = {
    alert_time: tpl.alert_time || 15,
    rendered_template: tpl.rendered_template,
    receiver_users: tpl.receiver_users || [],
  };
  startNotify();
}

function onPowerRestore() {
  stopNotify();
}

function startNotify() {
  stopNotify();
  if (!lastTemplate) return;
  const ms = (lastTemplate.alert_time || 15) * 60000;
  sendSms();
  notifyInterval = setInterval(sendSms, ms);
  console.log(`[SMS] Started every ${lastTemplate.alert_time} min`);
}
function stopNotify() {
  if (notifyInterval) {
    clearInterval(notifyInterval);
    notifyInterval = null;
    console.log('[SMS] Stopped');
  }
}

async function sendSms() {
  if (!lastTemplate) return;
  for (const u of lastTemplate.receiver_users) {
    if (!u.phone || !u.id) continue;
    await authRequest('POST', SMS_URL, {
      phone: u.phone,
      sms: lastTemplate.rendered_template,
      user_id: u.id,
    });
    console.log(`[SMS] Sent to ${u.phone}`);
  }
}
// ===========================

(async () => {
  await ensureLogin();
  startUdp();
})();
