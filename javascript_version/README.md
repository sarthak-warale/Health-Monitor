# Health Monitor — JavaScript version

This folder contains the complete JavaScript version of the Health Monitor: the frontend, Node.js API server, and local JSON persistence. It does not depend on the C server or the rest of the repository to serve the UI.

## Run

From this folder, run:

```powershell
node server.js
```

Then open [http://localhost:3000/](http://localhost:3000/). Stop the server with `Ctrl+C`.

Requirements: Node.js. The local server uses built-in modules only; no `npm install`
is needed for local mode. Vercel's API functions use the Neon driver listed in
`package.json`, which Vercel installs during deployment.

Patient and reading data is created on first write in `data/store.json`. That file is local runtime data and is ignored by Git.

## Features

- Patient creation, search, selection, and removal
- Simulated readings and `/api/readings` ingestion
- Live summary and recent-vitals chart
- Reading history sorted by heart rate or SpO₂
- Undo for up to 30 readings added in the current server session
- Linked list, circular queue, binary search tree, stack, and bubble sort implemented in JavaScript

The dashboard is a project demonstration using simulated vitals; it is not a medical device.

## ESP32 sensor firmware and USB installer

The starter firmware for an ESP32 DevKit/WROOM with a MAX30102 is in
[`firmware/README.md`](firmware/README.md). It posts valid sensor readings to
`POST /api/readings`. Set a patient ID, device key, Wi-Fi, and the deployed
server URL in the private `secrets.h` file before compiling.

The dashboard links to `/flash.html`, a browser USB installer. Compile the
configured firmware and copy its build parts into `public/firmware/` with
`firmware/build-web-firmware.ps1` before offering the installer on the deployed
site. Browser USB access requires HTTPS and a supported desktop browser.

## Deploy to Vercel

The Vercel deployment serves `public/` as static files and maps the routes in
`api/` to Node.js Functions. The cloud API stores patient and reading data in
Neon Postgres; it does not write to the local JSON file. The local `server.js`
remains available for development and continues to use `data/store.json`.

1. Create a Neon Postgres database, or add Neon through the Vercel Marketplace.
2. Run [`api/schema.sql`](api/schema.sql) once in the database's SQL editor.
3. In Vercel, import this repository and set the project root to
   `javascript_version`. Choose the **Other** framework preset and `public` as
   the output directory; there is no frontend build step.
4. Add these Vercel environment variables: `DATABASE_URL`, `DASHBOARD_PASSWORD`,
   `SESSION_SECRET`, and `DEVICE_API_KEY`. Use long random values for the two
   secrets (`node -e "console.log(require('node:crypto').randomBytes(32).toString('base64url'))"` generates a session secret). The dashboard uses a signed,
   HTTP-only session cookie; the ESP32 route accepts only the device key.
5. Deploy and sign in at `/login.html`. Add a patient through the dashboard,
   then configure the ESP32 with the Vercel URL ending in `/api/readings` and
   the matching device key.

The current local JSON store contains sample patient data. To carry it into a
new, empty database, first run the schema, set `DATABASE_URL` in your local
shell, run `npm install`, then run `npm run migrate:json`. The importer refuses
to run if the target database already contains patients or readings. No local
data is uploaded unless you run this import command.

Vercel Hobby includes function use within plan limits, while the database is a
separate Marketplace resource with its own provider and plan terms. Check the
selected database plan before creating it. The cloud API requires its schema
and environment variables before it will serve dashboard data. Never commit
database credentials, `secrets.h`, or a firmware binary containing the device
key.
