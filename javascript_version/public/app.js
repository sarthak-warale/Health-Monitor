(() => {
  'use strict';

  const API = '';
  const REFRESH_MS = 5000;
  const state = { patients: [], live: new Map(), selectedId: null, readings: [], chartReadings: [], sort: 'latest', view: 'overview', busy: false };
  const $ = (selector, root = document) => root.querySelector(selector);
  const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];

  async function api(path, options = {}) {
    const response = await fetch(`${API}${path}`, {
      ...options,
      headers: { 'Content-Type': 'application/json', ...(options.headers || {}) }
    });
    if (response.status === 401 && path !== '/api/session') {
      const returnTo = `${window.location.pathname}${window.location.search}${window.location.hash}`;
      window.location.replace(`/login.html?return=${encodeURIComponent(returnTo)}`);
      throw new Error('Sign in required');
    }
    if (!response.ok) {
      let error = `Request failed (${response.status})`;
      try { error = (await response.json()).error || error; } catch (_) { /* Keep status message. */ }
      throw new Error(error);
    }
    if (response.status === 204) return null;
    return response.json();
  }

  function escapeHtml(value) {
    return String(value ?? '').replace(/[&<>"']/g, char => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[char]);
  }
  function initials(name) { return String(name || '?').trim().split(/\s+/).slice(0, 2).map(part => part[0] || '').join('').toUpperCase(); }
  function timeOf(timestamp, date = false) {
    if (!timestamp) return '—';
    const options = date ? { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit' } : { hour: '2-digit', minute: '2-digit', second: '2-digit' };
    return new Date(Number(timestamp) * 1000).toLocaleString([], options);
  }
  function statusFor(reading) {
    if (!reading || (!reading.hr && !reading.spo2)) return { label: 'No readings', className: 'neutral' };
    if (reading.spo2 < 94 || reading.hr < 50 || reading.hr > 110) return { label: 'Review', className: 'alert' };
    if (reading.spo2 < 97 || reading.hr < 60 || reading.hr > 100) return { label: 'Observe', className: 'attention' };
    return { label: 'In range', className: 'stable' };
  }
  function hrStatus(hr) {
    if (!hr) return { text: 'Awaiting a reading', cls: '' };
    if (hr < 60) return { text: 'Below typical range', cls: 'warn' };
    if (hr > 100) return { text: 'Above typical range', cls: 'alert' };
    return { text: 'Within typical range', cls: 'good' };
  }
  function spo2Status(spo2) {
    if (!spo2) return { text: 'Awaiting a reading', cls: '' };
    if (spo2 < 94) return { text: 'Below typical range', cls: 'alert' };
    if (spo2 < 97) return { text: 'Slightly below typical range', cls: 'warn' };
    return { text: 'Within typical range', cls: 'good' };
  }
  function selectedPatient() { return state.patients.find(patient => String(patient.id) === String(state.selectedId)); }
  function latestFor(patient) { return state.live.get(Number(patient.id)) || state.chartReadings[0] || null; }

  function toast(message, type = 'success') {
    const item = document.createElement('div');
    item.className = `toast ${type}`;
    item.textContent = message;
    $('#toastRegion').append(item);
    setTimeout(() => item.remove(), 3400);
  }
  function setConnection(connected) {
    $('#connectionDot').className = `connection-dot ${connected ? 'connected' : 'offline'}`;
    $('#connectionText').textContent = connected ? 'Server connected' : 'Server unavailable';
  }

  function renderSidePatients() {
    $('#navPatientCount').textContent = state.patients.length;
    if (!state.patients.length) {
      $('#sidePatients').innerHTML = '<div class="side-loading">No patients yet</div>';
      return;
    }
    $('#sidePatients').innerHTML = state.patients.map(patient => {
      const reading = latestFor(patient);
      const status = statusFor(reading);
      return `<button class="side-patient ${String(patient.id) === String(state.selectedId) ? 'selected' : ''}" data-patient-id="${Number(patient.id)}" title="Open ${escapeHtml(patient.name)}"><span class="mini-avatar">${escapeHtml(initials(patient.name))}</span><span class="side-patient-name">${escapeHtml(patient.name)}</span><i class="side-patient-dot ${status.className === 'stable' ? 'good' : ''}"></i></button>`;
    }).join('');
  }

  function patientCard(patient, directory = false) {
    const latest = latestFor(patient);
    const status = statusFor(latest);
    const selected = String(patient.id) === String(state.selectedId);
    const common = `<div class="patient-card-top" data-patient-id="${Number(patient.id)}"><span class="directory-avatar">${escapeHtml(initials(patient.name))}</span><div style="min-width:0"><span class="patient-name">${escapeHtml(patient.name)}</span><span class="patient-age">${Number(patient.age) || 0} years · ${Number(patient.reading_count) || 0} readings</span></div><span class="patient-state ${status.className === 'stable' ? 'stable' : status.className === 'alert' ? 'attention' : ''}">${escapeHtml(status.label)}</span></div>
      <div class="patient-card-vitals"><div class="card-vital"><span>HEART RATE</span><strong class="heart-text">${latest?.hr ? Number(latest.hr) : '—'}</strong> <em>BPM</em></div><div class="card-vital"><span>BLOOD OXYGEN</span><strong class="oxygen-text">${latest?.spo2 ? Number(latest.spo2) : '—'}</strong> <em>%</em></div></div>`;
    if (!directory) return `<article class="patient-card ${selected ? 'selected' : ''}" data-select-patient="${Number(patient.id)}">${common}</article>`;
    return `<article class="directory-card"><div class="patient-card-top" data-patient-id="${Number(patient.id)}"><span class="directory-avatar">${escapeHtml(initials(patient.name))}</span><div style="min-width:0"><span class="patient-name">${escapeHtml(patient.name)}</span><span class="patient-age">${Number(patient.age) || 0} years · ${Number(patient.reading_count) || 0} readings</span></div><span class="patient-state ${status.className === 'stable' ? 'stable' : status.className === 'alert' ? 'attention' : ''}">${escapeHtml(status.label)}</span></div><div class="patient-card-vitals"><div class="card-vital"><span>HEART RATE</span><strong class="heart-text">${latest?.hr ? Number(latest.hr) : '—'}</strong> <em>BPM</em></div><div class="card-vital"><span>BLOOD OXYGEN</span><strong class="oxygen-text">${latest?.spo2 ? Number(latest.spo2) : '—'}</strong> <em>%</em></div></div><div class="directory-actions"><button data-open-patient="${Number(patient.id)}">Open monitor</button><button data-delete-patient="${Number(patient.id)}">Remove</button></div></article>`;
  }

  function renderPatients() {
    $('#sidePatients').innerHTML = '';
    renderSidePatients();
    $('#summaryPatients').textContent = String(state.patients.length).padStart(2, '0');
    const cards = state.patients.map(patient => patientCard(patient)).join('');
    $('#patientGrid').innerHTML = cards || '<div class="directory-empty">No patient records yet. Add a patient to begin monitoring.</div>';
    renderDirectory();
    const selectButton = $('#simulateButton');
    selectButton.disabled = !state.selectedId || state.busy;
  }

  function renderDirectory() {
    const term = $('#searchInput').value.trim().toLocaleLowerCase();
    const matching = state.patients.filter(patient => String(patient.name).toLocaleLowerCase().includes(term));
    $('#directoryCount').textContent = `${matching.length} ${matching.length === 1 ? 'patient' : 'patients'}`;
    $('#directoryGrid').innerHTML = matching.map(patient => patientCard(patient, true)).join('') || `<div class="directory-empty">${term ? 'No patients match that search.' : 'No patients yet. Add a patient to start your directory.'}</div>`;
  }

  function drawChart(readings) {
    const svg = $('#vitalsChart');
    const sorted = [...readings].slice(0, 20).reverse();
    if (!sorted.length) {
      svg.innerHTML = '<text x="380" y="122" text-anchor="middle" fill="#a0a3b2" font-size="12">No readings yet — simulate one to start the chart</text>';
      $('#chartRange').textContent = 'Waiting for readings';
      return;
    }
    const width = 760, height = 250, left = 42, right = 18, top = 12, bottom = 29;
    const plotW = width - left - right, plotH = height - top - bottom;
    const x = index => left + (sorted.length === 1 ? plotW / 2 : index * plotW / (sorted.length - 1));
    const yHr = value => top + plotH - (Math.min(130, Math.max(40, Number(value) || 40) - 40) / 90) * plotH;
    const yO2 = value => top + plotH - (Math.min(100, Math.max(85, Number(value) || 85) - 85) / 15) * plotH;
    let content = '';
    [40, 70, 100, 130].forEach(value => {
      const y = yHr(value);
      content += `<line x1="${left}" y1="${y}" x2="${width-right}" y2="${y}" stroke="#f0f0f5" stroke-width="1"/><text x="${left-9}" y="${y+3}" text-anchor="end" fill="#a3a6b4" font-size="9">${value}</text>`;
    });
    [85, 90, 95, 100].forEach(value => {
      const y = yO2(value);
      content += `<text x="${width-right+8}" y="${y+3}" text-anchor="start" fill="#a3a6b4" font-size="9">${value}</text>`;
    });
    const hrPoints = sorted.map((reading, index) => `${x(index)},${yHr(reading.hr)}`).join(' ');
    const o2Points = sorted.map((reading, index) => `${x(index)},${yO2(reading.spo2)}`).join(' ');
    if (sorted.length > 1) {
      const firstX = x(0), lastX = x(sorted.length - 1), baseY = top + plotH;
      content += `<polygon points="${firstX},${baseY} ${hrPoints} ${lastX},${baseY}" fill="rgba(222,126,137,.08)"/>`;
      content += `<polyline points="${hrPoints}" fill="none" stroke="#dc7d88" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round"/>`;
      content += `<polyline points="${o2Points}" fill="none" stroke="#6c9dd0" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round"/>`;
    }
    sorted.forEach((reading, index) => {
      content += `<circle cx="${x(index)}" cy="${yHr(reading.hr)}" r="3.2" fill="#fff" stroke="#dc7d88" stroke-width="2"/><circle cx="${x(index)}" cy="${yO2(reading.spo2)}" r="3.2" fill="#fff" stroke="#6c9dd0" stroke-width="2"/>`;
      if (index === 0 || index === sorted.length - 1 || index === Math.floor((sorted.length - 1) / 2)) content += `<text x="${x(index)}" y="${height-5}" text-anchor="middle" fill="#a3a6b4" font-size="8">${escapeHtml(timeOf(reading.timestamp))}</text>`;
    });
    svg.innerHTML = content;
    $('#chartRange').textContent = `${sorted.length} most recent ${sorted.length === 1 ? 'reading' : 'readings'}`;
  }

  function renderReadings(readings) {
    state.readings = readings || [];
    if (state.sort === 'latest') state.chartReadings = state.readings.slice(0, 20);
    $('#historyCaption').textContent = `${state.readings.length} ${state.readings.length === 1 ? 'reading' : 'readings'} recorded · newest first`;
    drawChart(state.chartReadings);
    if (!state.readings.length) {
      $('#readingRows').innerHTML = '<tr><td colspan="4" class="empty-row">No readings yet. Simulate a reading to begin.</td></tr>';
      return;
    }
    $('#readingRows').innerHTML = state.readings.slice(0, 50).map(reading => {
      const h = hrStatus(reading.hr), s = spo2Status(reading.spo2);
      const label = h.cls === 'alert' || s.cls === 'alert' ? 'Review' : h.cls === 'warn' || s.cls === 'warn' ? 'Observe' : 'In range';
      const cls = label === 'Review' ? 'alert' : label === 'Observe' ? 'warn' : 'good';
      return `<tr><td class="reading-time">${escapeHtml(timeOf(reading.timestamp, true))}</td><td><span class="value-pill"><i class="reading-dot ${h.cls === 'alert' ? 'high' : h.cls === 'warn' ? 'low' : ''}"></i>${Number(reading.hr)} <span style="font-weight:400;color:#999cad">BPM</span></span></td><td><span class="value-pill"><i class="reading-dot ${s.cls === 'alert' ? 'high' : s.cls === 'warn' ? 'low' : ''}"></i>${Number(reading.spo2)}%</span></td><td><span class="vital-status ${cls}">${label}</span></td></tr>`;
    }).join('');
  }

  function renderDetail() {
    const patient = selectedPatient();
    const hasPatient = Boolean(patient);
    $('#detailPanel').hidden = !hasPatient;
    $('#emptyDetail').hidden = hasPatient;
    if (!patient) {
      $('#summaryHr').textContent = '—'; $('#summarySpo2').textContent = '—';
      $('#detailHr').textContent = '—'; $('#detailSpo2').textContent = '—';
      drawChart([]); renderReadings([]); renderSidePatients(); return;
    }
    const latest = latestFor(patient);
    const status = statusFor(latest);
    $('#selectedPatientBadge').textContent = patient.name;
    $('#patientName').textContent = patient.name;
    $('#patientMeta').textContent = `${Number(patient.age) || 0} years old`;
    $('#profileAvatar').textContent = initials(patient.name);
    $('#profileId').textContent = `#${patient.id}`;
    $('#profileReadings').textContent = patient.reading_count || 0;
    $('#patientStatus').textContent = status.label;
    $('#patientStatus').className = `status-chip ${status.className}`;
    $('#detailHr').textContent = latest?.hr || '—';
    $('#detailSpo2').textContent = latest?.spo2 || '—';
    $('#summaryHr').textContent = latest?.hr || '—';
    $('#summarySpo2').textContent = latest?.spo2 || '—';
    const hs = hrStatus(latest?.hr), ss = spo2Status(latest?.spo2);
    $('#detailHrStatus').textContent = hs.text; $('#detailHrStatus').className = `vital-status ${hs.cls}`;
    $('#detailSpo2Status').textContent = ss.text; $('#detailSpo2Status').className = `vital-status ${ss.cls}`;
    $('#profileDelete').dataset.deletePatient = patient.id;
    $('#undoButton').disabled = state.busy || state.readings.length === 0;
    renderSidePatients();
  }

  async function loadPatients({ keepSelection = true } = {}) {
    try {
      const patients = await api('/api/patients');
      state.patients = Array.isArray(patients) ? patients : [];
      if (!keepSelection || !state.patients.some(patient => String(patient.id) === String(state.selectedId))) {
        state.selectedId = state.patients.length ? state.patients[0].id : null;
      }
      renderPatients();
      if (state.selectedId) await loadReadings(); else renderDetail();
      setConnection(true);
    } catch (error) {
      setConnection(false);
      $('#patientGrid').innerHTML = `<div class="directory-empty">Could not connect to the Health Monitor API. Check the deployment and refresh. <small>${escapeHtml(error.message)}</small></div>`;
      toast(`Could not load patients: ${error.message}`, 'error');
    }
  }

  async function loadLive({ quiet = false } = {}) {
    try {
      const live = await api('/api/live');
      state.live = new Map((Array.isArray(live) ? live : []).map(entry => [Number(entry.patient_id), entry.timestamp ? entry : null]));
      setConnection(true);
      renderPatients();
      if (state.selectedId) {
        const patient = selectedPatient();
        const fresh = state.live.get(Number(state.selectedId));
        if (patient && fresh && (!state.chartReadings[0] || Number(state.chartReadings[0].timestamp) !== Number(fresh.timestamp))) {
          state.chartReadings = [fresh, ...state.chartReadings.filter(item => Number(item.timestamp) !== Number(fresh.timestamp))].slice(0, 20);
          const priorCount = Number(patient.reading_count) || 0;
          if (!state.readings.some(item => Number(item.timestamp) === Number(fresh.timestamp))) {
            patient.reading_count = priorCount + 1;
            state.readings = [fresh, ...state.readings];
            if (state.sort === 'hr' || state.sort === 'spo2') state.readings.sort((a, b) => Number(a[state.sort]) - Number(b[state.sort]));
            renderReadings(state.readings);
          }
          renderDetail();
        }
      }
      $('#lastUpdated').textContent = new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
      if (!quiet) toast('Dashboard refreshed');
    } catch (_) {
      setConnection(false);
    }
  }

  async function loadReadings() {
    const patient = selectedPatient();
    if (!patient) { renderDetail(); return; }
    const suffix = state.sort === 'latest' ? '' : `?sort=${encodeURIComponent(state.sort)}`;
    try {
      const readings = await api(`/api/patients/${Number(patient.id)}/readings${suffix}`);
      renderReadings(Array.isArray(readings) ? readings : []);
      if (readings.length) {
        const latest = readings.reduce((current, reading) => Number(reading.timestamp) > Number(current.timestamp) ? reading : current, readings[0]);
        state.live.set(Number(patient.id), { patient_id: patient.id, name: patient.name, ...latest });
        state.chartReadings = state.sort === 'latest' ? readings.slice(0, 20) : (state.chartReadings.length ? state.chartReadings : readings.slice(0, 20));
      } else state.chartReadings = [];
      renderDetail();
    } catch (error) {
      toast(`Could not load readings: ${error.message}`, 'error');
    }
  }

  function selectPatient(id) {
    state.selectedId = Number(id);
    state.sort = 'latest';
    state.readings = [];
    state.chartReadings = [];
    $('#sortSelect').value = 'latest';
    renderPatients(); renderDetail();
    loadReadings();
    if (window.innerWidth <= 800) $('#sidebar').classList.remove('open');
  }

  function setView(view) {
    state.view = view;
    $$('.view').forEach(section => section.classList.toggle('active', section.id === `${view}View`));
    $$('.nav-item').forEach(button => button.classList.toggle('active', button.dataset.view === view));
    $('#crumbCurrent').textContent = view === 'overview' ? (selectedPatient()?.name || 'Overview') : 'Patients';
    if (view === 'patients') renderDirectory();
    if (window.innerWidth <= 800) $('#sidebar').classList.remove('open');
  }

  function openModal() {
    $('#patientForm').reset(); $('#formError').textContent = '';
    $('#patientModal').hidden = false;
    setTimeout(() => $('#patientNameInput').focus(), 0);
  }
  function closeModal() { $('#patientModal').hidden = true; }

  async function simulateReading() {
    if (!state.selectedId || state.busy) return;
    state.busy = true; renderPatients();
    try {
      const result = await api(`/api/simulate/${Number(state.selectedId)}`, { method: 'POST', body: '' });
      toast(`Reading added · ${result.hr} BPM · ${result.spo2}% SpO₂`);
      await loadPatients();
      await loadLive({ quiet: true });
    } catch (error) { toast(`Could not add reading: ${error.message}`, 'error'); }
    finally { state.busy = false; renderPatients(); }
  }

  async function undoReading() {
    if (!state.selectedId || state.busy) return;
    state.busy = true; renderDetail();
    try {
      const result = await api(`/api/patients/${Number(state.selectedId)}/undo`, { method: 'POST', body: '' });
      toast(`Removed latest reading · ${result.removed.hr} BPM · ${result.removed.spo2}% SpO₂`);
      await loadPatients(); await loadLive({ quiet: true });
    } catch (error) { toast(error.message, 'error'); }
    finally { state.busy = false; renderDetail(); }
  }

  async function deletePatient(id) {
    const patient = state.patients.find(item => Number(item.id) === Number(id));
    if (!patient || !window.confirm(`Remove ${patient.name} and their saved reading history?`)) return;
    try {
      await api(`/api/patients/${Number(id)}`, { method: 'DELETE' });
      state.live.delete(Number(id));
      if (Number(state.selectedId) === Number(id)) state.selectedId = null;
      toast(`${patient.name} was removed`);
      await loadPatients({ keepSelection: false });
    } catch (error) { toast(`Could not remove patient: ${error.message}`, 'error'); }
  }

  async function createPatient(event) {
    event.preventDefault();
    const name = $('#patientNameInput').value.trim();
    const age = Number($('#patientAgeInput').value);
    if (!name || !Number.isInteger(age) || age < 0 || age > 120) {
      $('#formError').textContent = 'Enter a name and an age from 0 to 120.'; return;
    }
    $('#formError').textContent = '';
    $('#savePatient').disabled = true;
    try {
      const patient = await api('/api/patients', { method: 'POST', body: JSON.stringify({ name, age }) });
      closeModal();
      await loadPatients({ keepSelection: false });
      selectPatient(patient.id);
      toast(`${patient.name} added to the patient list`);
    } catch (error) { $('#formError').textContent = error.message; }
    finally { $('#savePatient').disabled = false; }
  }

  function bindEvents() {
    $$('.nav-item').forEach(button => button.addEventListener('click', () => setView(button.dataset.view)));
    $$('[data-open-view="patients"]').forEach(button => button.addEventListener('click', () => setView('patients')));
    $('#patientGrid').addEventListener('click', event => {
      const card = event.target.closest('[data-select-patient]');
      if (card) selectPatient(card.dataset.selectPatient);
    });
    $('#sidePatients').addEventListener('click', event => {
      const patient = event.target.closest('[data-patient-id]');
      if (patient) { selectPatient(patient.dataset.patientId); setView('overview'); }
    });
    $('#directoryGrid').addEventListener('click', event => {
      const open = event.target.closest('[data-open-patient]');
      const card = event.target.closest('.directory-card');
      if (open) { selectPatient(open.dataset.openPatient); setView('overview'); return; }
      const remove = event.target.closest('[data-delete-patient]');
      if (remove) { deletePatient(remove.dataset.deletePatient); return; }
      const patient = event.target.closest('.patient-card-top[data-patient-id]');
      if (patient) { selectPatient(patient.dataset.patientId); setView('overview'); }
    });
    $('#profileDelete').addEventListener('click', event => deletePatient(event.currentTarget.dataset.deletePatient));
    $('#addPatientButton').addEventListener('click', openModal);
    $('#sideAddPatient').addEventListener('click', openModal);
    $('#closeModal').addEventListener('click', closeModal);
    $('#cancelModal').addEventListener('click', closeModal);
    $('#patientModal').addEventListener('click', event => { if (event.target === $('#patientModal')) closeModal(); });
    $('#patientForm').addEventListener('submit', createPatient);
    $('#simulateButton').addEventListener('click', simulateReading);
    $('#undoButton').addEventListener('click', undoReading);
    $('#sortSelect').addEventListener('change', event => { state.sort = event.target.value; loadReadings(); });
    $('#searchInput').addEventListener('input', renderDirectory);
    $('#refreshButton').addEventListener('click', async () => { await loadPatients(); await loadLive({ quiet: true }); toast('Dashboard refreshed'); });
    $('#menuButton').addEventListener('click', () => $('#sidebar').classList.toggle('open'));
    $('#logoutButton').addEventListener('click', async () => {
      try { await fetch('/api/session', { method: 'DELETE', credentials: 'same-origin' }); } finally { window.location.replace('/login.html'); }
    });
    document.addEventListener('keydown', event => { if (event.key === 'Escape') closeModal(); });
  }

  async function init() {
    $('#todayLabel').textContent = new Date().toLocaleDateString([], { weekday: 'short', month: 'short', day: 'numeric' });
    $('#greeting').textContent = new Date().getHours() < 12 ? 'GOOD MORNING' : new Date().getHours() < 18 ? 'GOOD AFTERNOON' : 'GOOD EVENING';
    fetch('/api/session', { credentials: 'same-origin' }).then(response => response.ok ? response.json() : null)
      .then(session => { if (session?.authenticated) $('#logoutButton').hidden = false; }).catch(() => {});
    bindEvents();
    await loadPatients({ keepSelection: false });
    await loadLive({ quiet: true });
    window.setInterval(() => loadLive({ quiet: true }), REFRESH_MS);
  }
  init();
})();
