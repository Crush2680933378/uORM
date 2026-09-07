const token = () => localStorage.getItem('uorm_token') || ''

async function req(method, path, body) {
  const res = await fetch(path, {
    method,
    headers: { 'Content-Type': 'application/json', 'X-Auth-Token': token() },
    body: body === undefined ? undefined : JSON.stringify(body),
  })
  const data = await res.json().catch(() => ({}))
  if (!res.ok) {
    if (res.status === 401) {
      localStorage.removeItem('uorm_token')
      window.location.reload()
    }
    throw new Error(data.error || res.statusText)
  }
  return data
}

export const api = {
  async login(t) {
    const res = await fetch('/api/login', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ token: t }),
    })
    if (!res.ok) throw new Error('令牌无效')
    localStorage.setItem('uorm_token', t)
  },
  logout: () => localStorage.removeItem('uorm_token'),
  drivers: () => req('GET', '/api/drivers'),
  listConn: () => req('GET', '/api/connections'),
  addConn: (p) => req('POST', '/api/connections', p),
  delConn: (id) => req('DELETE', `/api/connections/${id}`),
  testConn: (id) => req('POST', `/api/connections/${id}/test`),
  status: (id) => req('GET', `/api/connections/${id}/status`),
  tables: (id) => req('GET', `/api/connections/${id}/tables`),
  columns: (id, t) => req('GET', `/api/connections/${id}/tables/${encodeURIComponent(t)}/columns`),
  rows: (id, t, params) => req('GET', `/api/connections/${id}/tables/${encodeURIComponent(t)}/rows?` + new URLSearchParams(params)),
  query: (id, sql, maxRows = 500) => req('POST', `/api/connections/${id}/query`, { sql, maxRows }),
  execute: (id, sql, params) => req('POST', `/api/connections/${id}/execute`, { sql, params }),
}
