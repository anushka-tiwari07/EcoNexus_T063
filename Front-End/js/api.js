// Thin fetch wrapper for the ECO-NEXUS backend. Same-origin; the session lives in an HttpOnly cookie.
// State-changing requests must carry the CSRF token the backend returns from register/login/me (X-CSRF-Token).
let csrfToken = null;

async function api(path, { method = 'GET', body } = {}) {
  const headers = { 'Content-Type': 'application/json' };
  if (csrfToken && method !== 'GET' && method !== 'HEAD') headers['X-CSRF-Token'] = csrfToken;
  let res;
  try {
    res = await fetch('/api/v1' + path, {
      method, credentials: 'same-origin', headers,
      body: body ? JSON.stringify(body) : undefined,
    });
  } catch {
    throw new Error('Cannot reach the ECO-NEXUS server. Start carbon_server (see docs/local-dev.md).');
  }
  const data = res.status === 204 ? {} : await res.json().catch(() => ({}));
  // Backend error shape: {"error":{"code","message","request_id","details?"}}
  if (!res.ok) {
    const err = data.error || {};
    const detail = Array.isArray(err.details) && err.details.length ? ': ' + err.details.map((d) => d.message).filter(Boolean).join('; ') : '';
    throw new Error((err.message || `Request failed (${res.status})`) + detail);
  }
  if (data.csrf_token) csrfToken = data.csrf_token;
  return data;
}

// Session response: { user: {id, email, display_name, is_platform_admin}, memberships: [{organization_id, role}], csrf_token }
export const auth = {
  register: (b) => api('/auth/register', { method: 'POST', body: b }),
  login: (b) => api('/auth/login', { method: 'POST', body: b }),
  logout: async () => { await api('/auth/logout', { method: 'POST' }); csrfToken = null; },
  me: () => api('/auth/me'),
};
