const TOKEN_KEY = 'uorm_token'

export const getToken = () => localStorage.getItem(TOKEN_KEY) || ''
export const setToken = (t) => localStorage.setItem(TOKEN_KEY, t)
export const clearToken = () => localStorage.removeItem(TOKEN_KEY)

async function req(method, path, body) {
  const res = await fetch(path, {
    method,
    headers: { 'Content-Type': 'application/json', 'X-Auth-Token': getToken() },
    body: body === undefined ? undefined : JSON.stringify(body),
  })
  const data = await res.json().catch(() => ({}))
  if (!res.ok) {
    if (res.status === 401) {
      clearToken()
      window.location.reload()
    }
    throw new Error(data.error || res.statusText)
  }
  return data
}

export const api = {
  async login(username, password) {
    const res = await fetch('/api/login', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ username, password }),
    })
    const data = await res.json().catch(() => ({}))
    if (!res.ok) throw new Error(data.error || '登录失败')
    setToken(data.token)
    return { username: data.username, role: data.role }
  },
  async me() {
    return req('GET', '/api/me')
  },
  logout: () => localStorage.removeItem(TOKEN_KEY),
  drivers: () => req('GET', '/api/drivers'),
  users: () => req('GET', '/api/users'),
  logs: (params) => req('GET', '/api/logs?' + new URLSearchParams(params)),
  addUser: (p) => req('POST', '/api/users', p),
  delUser: (name) => req('DELETE', `/api/users/${encodeURIComponent(name)}`),
  setUserRole: (name, role) => req('PUT', `/api/users/${encodeURIComponent(name)}/role`, { role }),
  setUserPassword: (name, pw) => req('PUT', `/api/users/${encodeURIComponent(name)}/password`, { password: pw }),
  listConn: () => req('GET', '/api/connections'),
  addConn: (p) => req('POST', '/api/connections', p),
  delConn: (id) => req('DELETE', `/api/connections/${id}`),
  testConn: (id) => req('POST', `/api/connections/${id}/test`),
  status: (id) => req('GET', `/api/connections/${id}/status`),
  tables: (id) => req('GET', `/api/connections/${id}/tables`),
  columns: (id, t) => req('GET', `/api/connections/${id}/tables/${encodeURIComponent(t)}/columns`),
  rows: (id, t, params) =>
    req('GET', `/api/connections/${id}/tables/${encodeURIComponent(t)}/rows?` + new URLSearchParams(params)),
  query: (id, sql, maxRows = 500) => req('POST', `/api/connections/${id}/query`, { sql, maxRows }),
  execute: (id, sql, params) => req('POST', `/api/connections/${id}/execute`, { sql, params }),
}
