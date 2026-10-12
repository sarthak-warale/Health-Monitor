import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { neon } from '@neondatabase/serverless';

const projectDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const storePath = path.join(projectDir, 'data', 'store.json');
if (!process.env.DATABASE_URL) throw new Error('Set DATABASE_URL to the new Neon database connection string first.');
if (!fs.existsSync(storePath)) throw new Error(`No local store found at ${storePath}`);

const saved = JSON.parse(fs.readFileSync(storePath, 'utf8'));
const patients = Array.isArray(saved.patients) ? saved.patients : [];
const sql = neon(process.env.DATABASE_URL);
const counts = await sql`
  SELECT (SELECT COUNT(*)::int FROM patients) AS patients,
         (SELECT COUNT(*)::int FROM readings) AS readings
`;
if (Number(counts[0].patients) || Number(counts[0].readings)) {
  throw new Error('Target database is not empty. Import stopped to avoid duplicating or overwriting data.');
}

const statements = [];
for (const patient of patients) {
  statements.push(sql`
    INSERT INTO patients (id, name, age) VALUES (${patient.id}, ${patient.name}, ${patient.age})
  `);
  // The local JSON store is newest-first. Insert oldest-first so larger database IDs
  // continue to mean newer readings for the dashboard and undo action.
  for (const reading of [...(patient.readings || [])].reverse()) {
    statements.push(sql`
      INSERT INTO readings (patient_id, hr, spo2, timestamp)
      VALUES (${patient.id}, ${reading.hr}, ${reading.spo2}, ${reading.timestamp})
    `);
  }
}

if (statements.length) {
  const nextPatientId = Math.max(1, Number(saved.nextPatientId) || 1);
  statements.push(sql`
    SELECT setval(
      pg_get_serial_sequence('patients', 'id'),
      GREATEST(${nextPatientId - 1}, COALESCE((SELECT MAX(id) FROM patients), 1)),
      TRUE
    )
  `);
  await sql.transaction(statements);
}

const migratedReadings = patients.reduce((total, patient) => total + (patient.readings || []).length, 0);
console.log(`Imported ${patients.length} patient(s) and ${migratedReadings} reading(s).`);
