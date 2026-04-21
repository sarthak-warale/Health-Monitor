/* app.js — Shared utilities for Health Monitor web UI */

/* ── API helper ───────────────────────────────────────────────── */
async function apiFetch(endpoint, options = {}) {
    const res = await fetch(endpoint, {
        headers: { 'Content-Type': 'application/json' },
        ...options
    });
    if (!res.ok) {
        const err = await res.json().catch(() => ({ error: res.statusText }));
        throw new Error(err.error || `HTTP ${res.status}`);
    }
    return res.json();
}

/* ── Formatting helpers ───────────────────────────────────────── */
function formatTime(ts) {
    if (!ts) return '—';
    return new Date(ts * 1000).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
}

function formatDateTime(ts) {
    if (!ts) return '—';
    return new Date(ts * 1000).toLocaleString([], {
        month: 'short', day: 'numeric',
        hour: '2-digit', minute: '2-digit', second: '2-digit'
    });
}

/* ── Vital status helpers ─────────────────────────────────────── */
function hrStatus(hr) {
    if (!hr || hr === 0) return { text: 'No data', color: 'var(--text-muted)' };
    if (hr < 60)          return { text: '↓ Low',    color: 'var(--warn-color)'  };
    if (hr > 100)         return { text: '↑ High',   color: 'var(--hr-color)'    };
    return                       { text: '✓ Normal', color: 'var(--good-color)'  };
}

function spo2Status(spo2) {
    if (!spo2 || spo2 === 0) return { text: 'No data', color: 'var(--text-muted)' };
    if (spo2 < 94)            return { text: '↓ Low',   color: 'var(--hr-color)'   };
    if (spo2 < 97)            return { text: '✓ Fair',  color: 'var(--warn-color)' };
    return                           { text: '✓ Normal',color: 'var(--good-color)' };
}

/* ── Toast notifications ──────────────────────────────────────── */
function showToast(msg, type = 'info') {
    let container = document.getElementById('toast-container');
    if (!container) {
        container = document.createElement('div');
        container.id = 'toast-container';
        container.className = 'toast-container';
        document.body.appendChild(container);
    }
    const icons = { success: '✓', error: '✗', info: 'ℹ' };
    const t = document.createElement('div');
    t.className = `toast ${type}`;
    t.innerHTML = `<span>${icons[type] || 'ℹ'}</span><span>${msg}</span>`;
    container.appendChild(t);
    setTimeout(() => { t.style.opacity = '0'; t.style.transition = 'opacity 0.3s'; setTimeout(() => t.remove(), 300); }, 3000);
}

/* ── Navigation highlight ─────────────────────────────────────── */
document.addEventListener('DOMContentLoaded', () => {
    const links = document.querySelectorAll('.nav-link');
    const curr  = window.location.pathname;
    links.forEach(l => {
        const href = l.getAttribute('href');
        if (href === curr || (curr === '/' && href === '/') ||
            (curr.startsWith('/patient_detail') && href === '/patients.html') ||
            (href !== '/' && curr.endsWith(href)))
            l.classList.add('active');
        else
            l.classList.remove('active');
    });
});
