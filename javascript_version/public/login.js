(() => {
  const form = document.querySelector('#loginForm');
  const password = document.querySelector('#password');
  const error = document.querySelector('#loginError');
  const params = new URLSearchParams(window.location.search);
  const candidate = params.get('return') || '/';
  const returnTo = candidate.startsWith('/') && !candidate.startsWith('//') ? candidate : '/';

  fetch('/api/session', { credentials: 'same-origin' })
    .then(response => response.json())
    .then(session => { if (session.authenticated) window.location.replace(returnTo); })
    .catch(() => {});

  form.addEventListener('submit', async event => {
    event.preventDefault();
    error.textContent = '';
    const button = form.querySelector('button');
    button.disabled = true;
    try {
      const response = await fetch('/api/session', {
        method: 'POST',
        credentials: 'same-origin',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ password: password.value })
      });
      const result = await response.json();
      if (!response.ok) throw new Error(result.error || `Sign in failed (${response.status})`);
      window.location.replace(returnTo);
    } catch (cause) {
      error.textContent = cause.message;
      password.select();
    } finally {
      button.disabled = false;
    }
  });
})();
