// Thin fetch wrapper for the ECO-NEXUS backend (Phase 3 API). Same-origin; the session lives in an HttpOnly cookie
// the browser cannot read. State-changing requests must echo the CSRF token the server returned at login/me.
let csrf = '';

async function api(path, { method = 'GET', body } = {}) {
  const headers = { 'Content-Type': 'application/json' };
  if (method !== 'GET' && csrf) headers['X-CSRF-Token'] = csrf;
  let res;
  try {
    res = await fetch('/api/v1' + path, { method, credentials: 'same-origin', headers, body: body ? JSON.stringify(body) : undefined });
  } catch {
    throw new Error('Cannot reach the ECO-NEXUS server. Start carbon_server (see docs/local-dev.md).');
  }
  const data = res.status === 204 ? {} : await res.json().catch(() => ({}));
  // Backend error shape: {"error":{"code","message","request_id","details"}}
  if (!res.ok) {
    const e = new Error((data.error && data.error.message) || `Request failed (${res.status})`);
    e.status = res.status;
    e.details = data.error && data.error.details;
    throw e;
  }
  return data;
}

// Session response: {user:{id,email,display_name,is_platform_admin}, memberships:[{organization_id, role}], csrf_token}
const remember = (s) => { csrf = s.csrf_token || ''; return s; };

export const auth = {
  register: (b) => api('/auth/register', { method: 'POST', body: b }).then(remember),
  login: (b) => api('/auth/login', { method: 'POST', body: b }).then(remember),
  logout: () => api('/auth/logout', { method: 'POST' }).finally(() => { csrf = ''; }),
  me: () => api('/auth/me').then(remember),
};

// Company data (PostgreSQL `organizations` table, tenant-checked on the server).
export const orgs = {
  get: (id) => api('/organizations/' + encodeURIComponent(id)),
  update: (id, patch) => api('/organizations/' + encodeURIComponent(id), { method: 'PATCH', body: patch }),
};
