import { neon } from '@neondatabase/serverless';
import { createHmac, timingSafeEqual } from 'node:crypto';

const MAX_PATIENTS = 20;
const NAME_LIMIT = 63;
let sqlClient;

function sql() {
  if (!process.env.DATABASE_URL) throw new Error('DATABASE_URL is not configured');
  if (!sqlClient) sqlClient = neon(process.env.DATABASE_URL);
  return sqlClient;
}

function json(data, status = 200, headers = {}) {
  return Response.json(data, { status, headers: { 'Cache-Control': 'no-store', ...headers } });
}

function error(message, status) {
  return json({ error: message }, status);
}

async function readJson(request) {
  const body = await request.text();
  if (body.length > 64 * 1024) throw Object.assign(new Error('Request body is too large'), { status: 413 });
  if (!body) return {};
  try { return JSON.parse(body); }
  catch { throw Object.assign(new Error('Request body must be valid JSON'), { status: 400 }); }
}

function equalSecret(supplied, expected) {
  const a = Buffer.from(String(supplied || ''));
  const b = Buffer.from(expected);
  return a.length === b.length && timingSafeEqual(a, b);
}

function signSession(expiry) {
  return createHmac('sha256', process.env.SESSION_SECRET).update(String(expiry)).digest('base64url');
}

function hasDashboardSession(request) {
  if (!process.env.SESSION_SECRET) return false;
  const cookieHeader = request.headers.get('cookie') || '';
  const cookie = cookieHeader.split(';').map(value => value.trim()).find(value => value.startsWith('hm_session='));
  if (!cookie) return false;
  const [expiryText, signature] = decodeURIComponent(cookie.slice('hm_session='.length)).split('.');
  const expiry = Number(expiryText);
  if (!Number.isSafeInteger(expiry) || expiry <= Math.floor(Date.now() / 1000) || !signature) return false;
  return equalSecret(signature, signSession(expiry));
}

function patientDto(row) {
  return { id: Number(row.id), name: row.name, age: Number(row.age), reading_count: Number(row.reading_count) };
}

function readingDto(row) {
  return { hr: Number(row.hr), spo2: Number(row.spo2), timestamp: Number(row.timestamp) };
}

async function getPatient(id) {
  const rows = await sql()`
    SELECT p.id, p.name, p.age, COUNT(r.id)::int AS reading_count
    FROM patients p LEFT JOIN readings r ON r.patient_id = p.id
    WHERE p.id = ${id} GROUP BY p.id
  `;
  return rows[0] || null;
}

async function addReading(patientId, reading) {
  const rows = await sql()`
    INSERT INTO readings (patient_id, hr, spo2, timestamp)
    VALUES (${patientId}, ${reading.hr}, ${reading.spo2}, ${reading.timestamp})
    RETURNING id
  `;
  return rows[0];
}

async function handler(request) {
  const url = new URL(request.url);
  const parts = url.pathname.split('/').filter(Boolean);
  if (parts[0] !== 'api') return error('unknown API endpoint', 404);
  const method = request.method;

  if (url.pathname === '/api/session') {
    if (method === 'GET') return json({ authenticated: hasDashboardSession(request) });
    if (method === 'DELETE') {
      return new Response(null, { status: 204, headers: { 'Set-Cookie': 'hm_session=; Path=/; Max-Age=0; HttpOnly; Secure; SameSite=Lax', 'Cache-Control': 'no-store' } });
    }
    if (method !== 'POST') return error('method not allowed', 405);
    if (!process.env.DASHBOARD_PASSWORD || !process.env.SESSION_SECRET) return error('dashboard sign-in is not configured', 503);
    const body = await readJson(request);
    if (!equalSecret(body.password, process.env.DASHBOARD_PASSWORD)) return error('incorrect password', 401);
    const expiry = Math.floor(Date.now() / 1000) + 7 * 24 * 60 * 60;
    const token = `${expiry}.${signSession(expiry)}`;
    const secure = url.protocol === 'https:' || request.headers.get('x-forwarded-proto') === 'https' || process.env.VERCEL ? '; Secure' : '';
    return json({ ok: true }, 200, {
      'Set-Cookie': `hm_session=${encodeURIComponent(token)}; Path=/; Max-Age=${7 * 24 * 60 * 60}; HttpOnly${secure}; SameSite=Lax`
    });
  }

  const publicDeviceRoute = method === 'POST' && url.pathname === '/api/readings';
  if (!publicDeviceRoute) {
    if (process.env.NODE_ENV === 'production' && (!process.env.DASHBOARD_PASSWORD || !process.env.SESSION_SECRET)) {
      return error('dashboard sign-in is not configured', 503);
    }
    if (process.env.NODE_ENV === 'production' && !hasDashboardSession(request)) return error('sign in required', 401);
  }

  if (method === 'GET' && url.pathname === '/api/patients') {
    const rows = await sql()`
      SELECT p.id, p.name, p.age, COUNT(r.id)::int AS reading_count
      FROM patients p LEFT JOIN readings r ON r.patient_id = p.id
      GROUP BY p.id ORDER BY p.id
    `;
    return json(rows.map(patientDto));
  }

  if (method === 'POST' && url.pathname === '/api/patients') {
    const body = await readJson(request);
    const name = typeof body.name === 'string' ? body.name.trim() : '';
    const age = Number(body.age);
    if (!name) return error('name is required', 400);
    if (name.length > NAME_LIMIT) return error(`name must be ${NAME_LIMIT} characters or fewer`, 400);
    if (!Number.isInteger(age) || age < 0 || age > 120) return error('age must be a whole number from 0 to 120', 400);
    const countRows = await sql()`SELECT COUNT(*)::int AS count FROM patients`;
    if (Number(countRows[0].count) >= MAX_PATIENTS) return error(`patient limit reached (${MAX_PATIENTS})`, 400);
    const rows = await sql()`
      INSERT INTO patients (name, age) VALUES (${name}, ${age})
      RETURNING id, name, age
    `;
    return json({ ...patientDto({ ...rows[0], reading_count: 0 }) }, 201);
  }

  if (method === 'GET' && url.pathname === '/api/patients/search') {
    const name = url.searchParams.get('name') || '';
    const rows = await sql()`
      SELECT p.id, p.name, p.age, COUNT(r.id)::int AS reading_count
      FROM patients p LEFT JOIN readings r ON r.patient_id = p.id
      WHERE LOWER(p.name) = LOWER(${name}) GROUP BY p.id ORDER BY p.id
    `;
    return json(rows.map(patientDto));
  }

  if (method === 'GET' && url.pathname === '/api/live') {
    const rows = await sql()`
      SELECT p.id AS patient_id, p.name,
        COALESCE(latest.hr, 0)::int AS hr,
        COALESCE(latest.spo2, 0)::int AS spo2,
        COALESCE(latest.timestamp, 0)::bigint AS timestamp
      FROM patients p
      LEFT JOIN LATERAL (
        SELECT hr, spo2, timestamp FROM readings WHERE patient_id = p.id ORDER BY id DESC LIMIT 1
      ) latest ON TRUE
      ORDER BY p.id
    `;
    return json(rows.map(row => ({ ...row, patient_id: Number(row.patient_id), hr: Number(row.hr), spo2: Number(row.spo2), timestamp: Number(row.timestamp) })));
  }

  if (method === 'POST' && url.pathname === '/api/readings') {
    const expected = process.env.DEVICE_API_KEY || '';
    if (process.env.NODE_ENV === 'production' && !expected) return error('device ingestion is not configured', 503);
    if (expected && !equalSecret(request.headers.get('x-device-key'), expected)) return error('invalid device key', 401);
    const body = await readJson(request);
    const patientId = Number(body.patient_id);
    const patient = Number.isInteger(patientId) ? await getPatient(patientId) : null;
    if (!patient) return error('patient not found', 404);
    const hr = Number(body.hr), spo2 = Number(body.spo2);
    if (!Number.isInteger(hr) || hr < 1 || hr > 250 || !Number.isInteger(spo2) || spo2 < 1 || spo2 > 100) {
      return error('hr and spo2 must be valid positive whole-number readings', 400);
    }
    const timestamp = Number(body.timestamp) || Math.floor(Date.now() / 1000);
    if (!Number.isSafeInteger(timestamp) || timestamp < 0) return error('timestamp must be a valid Unix timestamp', 400);
    const reading = { hr, spo2, timestamp };
    await addReading(patientId, reading);
    return json({ ok: true, patient_id: patientId, ...reading }, 201);
  }

  if (method === 'POST' && parts[1] === 'simulate' && parts.length === 3) {
    const patientId = Number(parts[2]);
    const patient = Number.isInteger(patientId) ? await getPatient(patientId) : null;
    if (!patient) return error('patient not found', 404);
    const reading = { hr: 58 + Math.floor(Math.random() * 45), spo2: 94 + Math.floor(Math.random() * 7), timestamp: Math.floor(Date.now() / 1000) };
    await addReading(patientId, reading);
    return json({ ok: true, patient_id: patientId, ...reading }, 201);
  }

  if (parts[1] === 'patients' && parts.length >= 3) {
    const patientId = Number(parts[2]);
    if (!Number.isInteger(patientId) || patientId < 1) return error('patient not found', 404);
    const subresource = parts[3] || '';

    if (!subresource && method === 'GET') {
      const patient = await getPatient(patientId);
      return patient ? json(patientDto(patient)) : error('not found', 404);
    }

    if (!subresource && method === 'DELETE') {
      const rows = await sql()`DELETE FROM patients WHERE id = ${patientId} RETURNING id`;
      return rows.length ? json({}) : error('not found', 404);
    }

    if (subresource === 'readings' && method === 'GET') {
      if (!(await getPatient(patientId))) return error('patient not found', 404);
      const sort = url.searchParams.get('sort');
      let rows;
      if (sort === 'hr' || sort === 'spo2') {
        rows = await sql.query(`SELECT hr, spo2, timestamp FROM readings WHERE patient_id = $1 ORDER BY ${sort} ASC, id DESC`, [patientId]);
      } else {
        rows = await sql()`SELECT hr, spo2, timestamp FROM readings WHERE patient_id = ${patientId} ORDER BY id DESC`;
      }
      return json(rows.map(readingDto));
    }

    if (subresource === 'undo' && method === 'POST') {
      const rows = await sql()`
        DELETE FROM readings WHERE id = (
          SELECT id FROM readings WHERE patient_id = ${patientId} ORDER BY id DESC LIMIT 1
        ) RETURNING hr, spo2
      `;
      return rows.length ? json({ ok: true, removed: { hr: Number(rows[0].hr), spo2: Number(rows[0].spo2) } }) : error('nothing to undo', 400);
    }
  }

  return error('unknown API endpoint', 404);
}

export default async function handleRequest(request) {
  try {
    return await handler(request);
  } catch (cause) {
    console.error('API request failed:', cause);
    return error(cause.status === 400 || cause.status === 413 ? cause.message : 'server error', cause.status || 500);
  }
}
