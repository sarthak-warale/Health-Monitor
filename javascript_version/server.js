'use strict';

// Health Monitor API and static frontend server. Uses only Node's built-in modules.
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { URL } = require('node:url');

const PORT = Number(process.env.PORT) || 3000;
const MAX_PATIENTS = 20;
const RING_SIZE = 50;
const UNDO_LIMIT = 30;
const NAME_LIMIT = 63;
const WEB_DIR = path.join(__dirname, 'public');
const DATA_DIR = process.env.DATA_DIR ? path.resolve(process.env.DATA_DIR) : path.join(__dirname, 'data');
const STORE_FILE = path.join(DATA_DIR, 'store.json');
const DEVICE_API_KEY = process.env.DEVICE_API_KEY || '';

class ReadingNode {
  constructor(reading, next = null) { this.reading = reading; this.next = next; }
}

class ReadingList {
  constructor() { this.head = null; this.length = 0; }
  insert(reading) { this.head = new ReadingNode(reading, this.head); this.length += 1; }
  removeHead() {
    if (!this.head) return null;
    const reading = this.head.reading;
    this.head = this.head.next;
    this.length -= 1;
    return reading;
  }
  toArray(limit = Infinity) {
    const result = [];
    for (let node = this.head; node && result.length < limit; node = node.next) result.push(node.reading);
    return result;
  }
  static fromNewestFirst(readings) {
    const list = new ReadingList();
    for (let i = readings.length - 1; i >= 0; i -= 1) list.insert(readings[i]);
    return list;
  }
}

class CircularQueue {
  constructor(capacity = RING_SIZE) {
    this.values = new Array(capacity);
    this.head = 0;
    this.tail = 0;
    this.count = 0;
  }
  enqueue(value) {
    this.values[this.tail] = value;
    this.tail = (this.tail + 1) % this.values.length;
    if (this.count < this.values.length) this.count += 1;
    else this.head = (this.head + 1) % this.values.length;
  }
  latest() { return this.count ? this.values[(this.tail - 1 + this.values.length) % this.values.length] : null; }
  static from(readings) {
    const queue = new CircularQueue();
    readings.slice(0, RING_SIZE).reverse().forEach(reading => queue.enqueue(reading));
    return queue;
  }
}

class UndoStack {
  constructor(limit = UNDO_LIMIT) { this.limit = limit; this.items = []; }
  push(reading) {
    if (this.items.length === this.limit) this.items.shift();
    this.items.push(reading);
  }
  pop() { return this.items.pop() || null; }
}

class PatientBST {
  constructor() { this.root = null; }
  insert(patient) {
    const key = patient.name.toLocaleLowerCase();
    const add = node => {
      if (!node) return { key, name: patient.name, ids: [patient.id], left: null, right: null };
      if (key < node.key) node.left = add(node.left);
      else if (key > node.key) node.right = add(node.right);
      else node.ids.push(patient.id);
      return node;
    };
    this.root = add(this.root);
  }
  find(name) {
    const key = String(name).toLocaleLowerCase();
    let node = this.root;
    while (node) {
      if (key === node.key) return [...node.ids];
      node = key < node.key ? node.left : node.right;
    }
    return [];
  }
  remove(name, id) {
    const key = String(name).toLocaleLowerCase();
    let node = this.root;
    while (node && node.key !== key) node = key < node.key ? node.left : node.right;
    if (!node) return;
    node.ids = node.ids.filter(patientId => patientId !== id);
    if (node.ids.length) return;
    const removeKey = (current, target) => {
      if (!current) return null;
      if (target < current.key) current.left = removeKey(current.left, target);
      else if (target > current.key) current.right = removeKey(current.right, target);
      else {
        if (!current.left) return current.right;
        if (!current.right) return current.left;
        let successor = current.right;
        while (successor.left) successor = successor.left;
        current.key = successor.key;
        current.name = successor.name;
        current.ids = successor.ids;
        current.right = removeKey(current.right, successor.key);
      }
      return current;
    };
    this.root = removeKey(this.root, key);
  }
}

function bubbleSort(readings, field) {
  const result = [...readings];
  for (let end = result.length - 1; end > 0; end -= 1) {
    let swapped = false;
    for (let i = 0; i < end; i += 1) {
      if (Number(result[i][field]) > Number(result[i + 1][field])) {
        [result[i], result[i + 1]] = [result[i + 1], result[i]];
        swapped = true;
      }
    }
    if (!swapped) break;
  }
  return result;
}

let patients = [];
let nextPatientId = 1;
let patientIndex = new PatientBST();
const histories = new Map();
const liveBuffers = new Map();
const undoStacks = new Map();

function rebuildIndexes() {
  patientIndex = new PatientBST();
  histories.clear(); liveBuffers.clear(); undoStacks.clear();
  for (const patient of patients) {
    patientIndex.insert(patient);
    const readings = ReadingList.fromNewestFirst(patient.readings || []);
    histories.set(patient.id, readings);
    liveBuffers.set(patient.id, CircularQueue.from(readings.toArray(RING_SIZE)));
    undoStacks.set(patient.id, new UndoStack());
    patient.readings = readings.toArray();
  }
}

function loadStore() {
  try {
    const saved = JSON.parse(fs.readFileSync(STORE_FILE, 'utf8'));
    patients = Array.isArray(saved.patients) ? saved.patients : [];
    nextPatientId = Number(saved.nextPatientId) || (patients.reduce((max, patient) => Math.max(max, Number(patient.id) || 0), 0) + 1);
    rebuildIndexes();
  } catch (error) {
    if (error.code !== 'ENOENT') console.error(`Could not load saved data: ${error.message}`);
    patients = [];
    nextPatientId = 1;
    rebuildIndexes();
  }
}

function saveStore() {
  fs.mkdirSync(DATA_DIR, { recursive: true });
  const snapshot = {
    nextPatientId,
    patients: patients.map(patient => ({ ...patient, readings: histories.get(patient.id)?.toArray() || [] }))
  };
  const temporaryFile = `${STORE_FILE}.tmp`;
  fs.writeFileSync(temporaryFile, JSON.stringify(snapshot, null, 2), 'utf8');
  fs.renameSync(temporaryFile, STORE_FILE);
}

function sendJson(response, status, body) {
  const data = JSON.stringify(body);
  response.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8', 'Content-Length': Buffer.byteLength(data), 'Cache-Control': 'no-store' });
  response.end(data);
}
function sendError(response, status, message) { sendJson(response, status, { error: message }); }
function patientDto(patient) {
  return { id: patient.id, name: patient.name, age: patient.age, reading_count: histories.get(patient.id)?.length || 0 };
}
function findPatient(id) { return patients.find(patient => patient.id === Number(id)); }
function readBody(request) {
  return new Promise((resolve, reject) => {
    let body = '';
    request.setEncoding('utf8');
    request.on('data', chunk => {
      body += chunk;
      if (body.length > 64 * 1024) {
        reject(Object.assign(new Error('Request body is too large'), { status: 413 }));
        request.destroy();
      }
    });
    request.on('end', () => {
      if (!body) return resolve({});
      try { resolve(JSON.parse(body)); }
      catch (_) { reject(Object.assign(new Error('Request body must be valid JSON'), { status: 400 })); }
    });
    request.on('error', reject);
  });
}

async function handleApi(request, response, url) {
  const parts = url.pathname.split('/').filter(Boolean);
  const method = request.method;
  if (method === 'GET' && url.pathname === '/api/patients') return sendJson(response, 200, patients.map(patientDto));
  if (method === 'POST' && url.pathname === '/api/patients') {
    const body = await readBody(request);
    const name = typeof body.name === 'string' ? body.name.trim() : '';
    const age = Number(body.age);
    if (!name) return sendError(response, 400, 'name is required');
    if (name.length > NAME_LIMIT) return sendError(response, 400, `name must be ${NAME_LIMIT} characters or fewer`);
    if (!Number.isInteger(age) || age < 0 || age > 120) return sendError(response, 400, 'age must be a whole number from 0 to 120');
    if (patients.length >= MAX_PATIENTS) return sendError(response, 400, `patient limit reached (${MAX_PATIENTS})`);
    const patient = { id: nextPatientId++, name, age, readings: [] };
    patients.push(patient);
    histories.set(patient.id, new ReadingList());
    liveBuffers.set(patient.id, new CircularQueue());
    undoStacks.set(patient.id, new UndoStack());
    patientIndex.insert(patient);
    saveStore();
    return sendJson(response, 201, patientDto(patient));
  }
  if (method === 'GET' && url.pathname === '/api/patients/search') {
    const ids = patientIndex.find(url.searchParams.get('name') || '');
    return sendJson(response, 200, ids.map(findPatient).filter(Boolean).map(patientDto));
  }
  if (method === 'GET' && url.pathname === '/api/live') {
    const result = patients.map(patient => {
      const reading = liveBuffers.get(patient.id)?.latest();
      return { patient_id: patient.id, name: patient.name, hr: reading?.hr || 0, spo2: reading?.spo2 || 0, timestamp: reading?.timestamp || 0 };
    });
    return sendJson(response, 200, result);
  }
  if (method === 'POST' && parts[0] === 'api' && parts[1] === 'simulate' && parts.length === 3) {
    const patient = findPatient(parts[2]);
    if (!patient) return sendError(response, 404, 'patient not found');
    const reading = { hr: 58 + Math.floor(Math.random() * 45), spo2: 94 + Math.floor(Math.random() * 7), timestamp: Math.floor(Date.now() / 1000) };
    recordReading(patient, reading);
    return sendJson(response, 201, { ok: true, patient_id: patient.id, ...reading });
  }
  if (method === 'POST' && url.pathname === '/api/readings') {
    if (process.env.NODE_ENV === 'production' && !DEVICE_API_KEY) return sendError(response, 503, 'device ingestion is not configured');
    if (DEVICE_API_KEY) {
      const suppliedKey = Buffer.from(String(request.headers['x-device-key'] || ''));
      const expectedKey = Buffer.from(DEVICE_API_KEY);
      if (suppliedKey.length !== expectedKey.length || !crypto.timingSafeEqual(suppliedKey, expectedKey)) return sendError(response, 401, 'invalid device key');
    }
    const body = await readBody(request);
    const patient = findPatient(body.patient_id);
    if (!patient) return sendError(response, 404, 'patient not found');
    const hr = Number(body.hr), spo2 = Number(body.spo2);
    if (!Number.isInteger(hr) || hr < 1 || hr > 250 || !Number.isInteger(spo2) || spo2 < 1 || spo2 > 100) return sendError(response, 400, 'hr and spo2 must be valid positive whole-number readings');
    const reading = { hr, spo2, timestamp: Number(body.timestamp) || Math.floor(Date.now() / 1000) };
    recordReading(patient, reading);
    return sendJson(response, 201, { ok: true, patient_id: patient.id, ...reading });
  }
  if (parts[0] === 'api' && parts[1] === 'patients' && parts.length >= 3) {
    const id = Number(parts[2]);
    const patient = findPatient(id);
    const subresource = parts[3] || '';
    if (!subresource && method === 'GET') return patient ? sendJson(response, 200, patientDto(patient)) : sendError(response, 404, 'not found');
    if (!subresource && method === 'DELETE') {
      if (!patient) return sendError(response, 404, 'not found');
      patientIndex.remove(patient.name, patient.id);
      patients = patients.filter(item => item.id !== patient.id);
      histories.delete(patient.id); liveBuffers.delete(patient.id); undoStacks.delete(patient.id);
      saveStore();
      return sendJson(response, 200, {});
    }
    if (subresource === 'readings' && method === 'GET') {
      if (!patient) return sendError(response, 404, 'patient not found');
      const readings = histories.get(patient.id)?.toArray() || [];
      const sort = url.searchParams.get('sort');
      return sendJson(response, 200, sort === 'hr' || sort === 'spo2' ? bubbleSort(readings, sort) : readings);
    }
    if (subresource === 'undo' && method === 'POST') {
      if (!patient) return sendError(response, 404, 'patient not found');
      const removed = undoStacks.get(patient.id)?.pop();
      if (!removed) return sendError(response, 400, 'nothing to undo');
      const current = histories.get(patient.id)?.head?.reading;
      if (current && current.timestamp === removed.timestamp && current.hr === removed.hr && current.spo2 === removed.spo2) {
        histories.get(patient.id).removeHead();
        liveBuffers.set(patient.id, CircularQueue.from(histories.get(patient.id).toArray(RING_SIZE)));
      }
      saveStore();
      return sendJson(response, 200, { ok: true, removed: { hr: removed.hr, spo2: removed.spo2 } });
    }
  }
  return sendError(response, 404, 'unknown API endpoint');
}

function recordReading(patient, reading) {
  histories.get(patient.id).insert(reading);
  liveBuffers.get(patient.id).enqueue(reading);
  undoStacks.get(patient.id).push(reading);
  saveStore();
}

function serveStatic(request, response, url) {
  let relative = decodeURIComponent(url.pathname);
  if (relative === '/' || relative === '/next' || relative === '/next/') relative = '/index.html';
  else if (relative.startsWith('/next/')) relative = relative.slice('/next'.length);
  const target = path.resolve(WEB_DIR, `.${relative}`);
  if (!target.startsWith(`${WEB_DIR}${path.sep}`) && target !== WEB_DIR) return sendError(response, 403, 'forbidden');
  fs.stat(target, (statError, stat) => {
    if (statError || !stat.isFile()) {
      response.writeHead(404, { 'Content-Type': 'text/plain; charset=utf-8' });
      response.end('Not found');
      return;
    }
    const extension = path.extname(target).toLowerCase();
    const types = { '.html': 'text/html; charset=utf-8', '.css': 'text/css; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.json': 'application/json; charset=utf-8', '.svg': 'image/svg+xml' };
    response.writeHead(200, { 'Content-Type': types[extension] || 'application/octet-stream', 'Content-Length': stat.size, 'Cache-Control': extension === '.html' ? 'no-cache' : 'public, max-age=3600' });
    fs.createReadStream(target).pipe(response);
  });
}

const server = http.createServer(async (request, response) => {
  const url = new URL(request.url, `http://${request.headers.host || 'localhost'}`);
  if (request.method === 'OPTIONS') {
    response.writeHead(204, { 'Access-Control-Allow-Origin': '*', 'Access-Control-Allow-Methods': 'GET,POST,DELETE,OPTIONS', 'Access-Control-Allow-Headers': 'Content-Type' });
    return response.end();
  }
  try {
    if (url.pathname.startsWith('/api/')) return await handleApi(request, response, url);
    if (request.method !== 'GET' && request.method !== 'HEAD') return sendError(response, 405, 'method not allowed');
    return serveStatic(request, response, url);
  } catch (error) {
    if (!response.headersSent) return sendError(response, error.status || 500, error.status ? error.message : 'server error');
    response.destroy(error);
  }
});

loadStore();
server.listen(PORT, () => {
  console.log(`Health Monitor JavaScript server ready at http://localhost:${PORT}`);
  console.log(`Patients loaded: ${patients.length}`);
  console.log(`Persistent data: ${STORE_FILE}`);
});
