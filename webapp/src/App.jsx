import React, { useEffect, useState } from 'react'
import { Button, Card, Form, Input, Layout, message, Space, Tabs, Tag, Tree, Typography } from 'antd'
import {
  DatabaseOutlined, TableOutlined, ConsoleSqlOutlined,
  SettingOutlined, LogoutOutlined, ReloadOutlined, PlusOutlined,
} from '@ant-design/icons'
import { api, getToken, clearToken } from './api.js'
import Connections from './pages/Connections.jsx'
import Users from './components/Users.jsx'
import TableGrid from './components/TableGrid.jsx'
import SqlConsole from './components/SqlConsole.jsx'
import Logs from './components/Logs.jsx'

const { Sider, Content, Header, Footer } = Layout
const { Text } = Typography

// 空态占位
function EmptyHint({ text }) {
  return <Card><Text type="secondary">{text}</Text></Card>
}

export default function App() {
  const [authed, setAuthed] = useState(!!getToken())
  const [me, setMe] = useState({ username: '', role: '' })   // 当前登录者
  const [connections, setConnections] = useState([])
  const [drivers, setDrivers] = useState([])
  const [tablesMap, setTablesMap] = useState({})    // connId -> [{name}]
  const [columnsMap, setColumnsMap] = useState({})  // `connId:table` -> [[name,type,...],...]
  const [activeConnId, setActiveConnId] = useState(null)
  const [tabs, setTabs] = useState([])              // {key,type,connId,table,title}
  const [activeKey, setActiveKey] = useState(null)

  const refreshConnections = async () => {
    try {
      const data = await api.listConn()
      const conns = data.connections || []
      setConnections(conns)
      setActiveConnId((prev) => (prev && conns.some((c) => c.id === prev)) ? prev : (conns[0]?.id ?? null))
      // 预取每个连接的表列表（树形展示）
      conns.forEach(async (c) => {
        try {
          const res = await api.tables(c.id)
          setTablesMap((m) => ({ ...m, [c.id]: {
            tables: (res.tables || []).map((name) => ({ name })),
            views: (res.views || []).map((name) => ({ name })),
          } }))
        } catch {
          setTablesMap((m) => ({ ...m, [c.id]: { tables: [], views: [] } }))
        }
      })
      return conns
    } catch (e) {
      if (e.message !== 'Failed to fetch') message.error(e.message)
      return []
    }
  }

  const loadTables = (connId) => {
    api.tables(connId)
      .then((data) => setTablesMap((m) => ({ ...m, [connId]: {
        tables: (data.tables || []).map((name) => ({ name })),
        views: (data.views || []).map((name) => ({ name })),
      } })))
      .catch(() => setTablesMap((m) => ({ ...m, [connId]: { tables: [], views: [] } })))
  }

  const isSuper = me.role === 'superadmin'
  const isAdmin = me.role === 'admin' || isSuper
  const canWrite = isAdmin  // 普通用户只读

  const refreshUsers = () => {
    if (!isSuper) return
    api.users().then((d) => setUsersList(d.users || [])).catch(() => {})
  }

  useEffect(() => {
    if (!authed) return
    api.drivers().then((d) => setDrivers(d.drivers || [])).catch(() => {})
    api.me().then((m) => setMe({ username: m.username, role: m.role })).catch(() => {})
    refreshConnections()
    refreshUsers()
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [authed])

  if (!authed) {
    return (
      <div style={{ display: 'flex', justifyContent: 'center', alignItems: 'center', height: '100vh', background: '#f1f5f9' }}>
        <Card title="uORM 数据库管理台" style={{ width: 380 }}>
          <Form onFinish={async ({ username, password }) => {
            try { await api.login(username, password); setAuthed(true) } catch (e) { message.error(e.message) }
          }}>
            <Form.Item name="username" rules={[{ required: true, message: '请输入用户名' }]}>
              <Input placeholder="用户名" autoFocus />
            </Form.Item>
            <Form.Item name="password" rules={[{ required: true, message: '请输入密码' }]}>
              <Input.Password placeholder="密码" />
            </Form.Item>
            <Button type="primary" htmlType="submit" block>登 录</Button>
          </Form>
        </Card>
      </div>
    )
  }

  // ---------------- 标签页 ----------------
  const openTab = (tab) => {
    setTabs((ts) => (ts.some((t) => t.key === tab.key) ? ts : [...ts, tab]))
    setActiveKey(tab.key)
  }
  const removeTab = (key) => {
    setTabs((ts) => {
      const idx = ts.findIndex((t) => t.key === key)
      const next = ts.filter((t) => t.key !== key)
      setActiveKey((ak) => (ak === key && next.length) ? next[Math.max(0, idx - 1)].key : (next[0]?.key ?? null))
      return next
    })
  }

  const openTableTab = (connId, table, readOnly = false) => {
    const conn = connections.find((c) => c.id === connId)
    const prefix = readOnly ? 'vt:' : 't:'
    openTab({
      key: `${prefix}${connId}:${table}`, type: 'table', connId, table, readOnly,
      title: <span><TableOutlined /> {table}{readOnly ? '（视图）' : ''}</span>,
    })
  }
  const openSqlTab = (connId) => {
    const conn = connections.find((c) => c.id === connId)
    openTab({
      key: `s:${connId}`, type: 'sql', connId,
      title: <span><ConsoleSqlOutlined /> 查询</span>,
    })
  }
  const openConnTab = () => openTab({
    key: 'conn', type: 'conn', title: <span><SettingOutlined /> 连接管理</span>,
  })

  const active = tabs.find((t) => t.key === activeKey)
  const activeConn = active ? connections.find((c) => c.id === active.connId) : null

  // ---------------- 对象树 ----------------
  const treeData = connections.map((c) => ({
    key: c.id,
    icon: <DatabaseOutlined style={{ color: '#2563eb' }} />,
    title: <span>{c.name} <Tag style={{ marginLeft: 4 }}>{c.driver}</Tag></span>,
    children: [
      ...(tablesMap[c.id]?.tables || []).map((t) => ({
        key: `t:${c.id}:${t.name}`,
        icon: <TableOutlined />,
        title: t.name,
        isLeaf: false,
        children: (columnsMap[`${c.id}:${t.name}`] || []).map((col) => ({
          key: `c:${c.id}:${t.name}:${col[0]}`,
          title: <span style={{ fontSize: 12, color: '#64748b' }}>{col[0]} <span style={{ color: '#94a3b8' }}>{String(col[1]).split('(')[0]}</span></span>,
          selectable: false,
        })),
      })),
      ...((tablesMap[c.id]?.views || []).length ? [{
        key: `vgrp:${c.id}`,
        title: <span style={{ color: '#94a3b8' }}>视图</span>,
        selectable: false,
        children: (tablesMap[c.id].views).map((v) => ({
          key: `v:${c.id}:${v.name}`,
          icon: <TableOutlined />,
          title: v.name,
          isLeaf: true,
        })),
      }] : []),
    ],
  }))

  const onLoadTreeData = (treeNode) =>
    new Promise(async (resolve) => {
      const key = treeNode.key || ''
      if (key.startsWith('t:')) {
        const [, connId, ...rest] = key.split(':')
        const table = rest.join(':')
        try {
          const res = await api.columns(connId, table)
          setColumnsMap((m) => ({ ...m, [`${connId}:${table}`]: res.columns || [] }))
        } catch { /* 列加载失败忽略 */ }
      }
      resolve()
    })

  const onTreeSelect = (keys, info) => {
    const node = info?.node
    if (!node) return
    const key = node.key
    if (typeof key === 'string' && key.startsWith('t:')) {
      const [, connId, ...rest] = key.split(':')
      setActiveConnId(connId)
      openTableTab(connId, rest.join(':'))
    } else if (typeof key === 'string' && key.startsWith('v:')) {
      const [, connId, ...rest] = key.split(':')
      setActiveConnId(connId)
      openTableTab(connId, rest.join(':'), true)
    } else {
      setActiveConnId(key)
      loadTables(key)
    }
  }

  const refreshAll = () => {
    refreshConnections().then((cs) => cs.forEach((c) => loadTables(c.id)))
  }

  return (
    <Layout style={{ minHeight: '100vh' }}>
      <Sider width={280} theme="light" style={{ borderRight: '1px solid #e2e8f0', overflow: 'hidden' }}>
        <div style={{ padding: '14px 16px', fontWeight: 700, fontSize: 15, display: 'flex', gap: 8, alignItems: 'center', borderBottom: '1px solid #f1f5f9' }}>
          <DatabaseOutlined style={{ color: '#2563eb' }} /> uORM 管理台
        </div>
        <div style={{ fontSize: 12, color: '#64748b', padding: '10px 16px 4px', fontWeight: 600 }}>对象资源管理器</div>
        <div style={{ height: 'calc(100vh - 130px)', overflow: 'auto' }}>
          <Tree
            showIcon
            blockNode
            treeData={treeData}
            loadData={onLoadTreeData}
            onSelect={onTreeSelect}
          />
        </div>
      </Sider>
      <Layout>
        <Header style={{ background: '#0f172a', display: 'flex', alignItems: 'center', padding: '0 16px', height: 48 }}>
          <Space size={8}>
            <Button type="primary" size="small" icon={<PlusOutlined />}
              disabled={!activeConnId}
              onClick={() => openSqlTab(activeConnId)}>新建查询</Button>
            <Button size="small" icon={<ReloadOutlined />} onClick={() => { refreshAll(); refreshUsers() }} />
            {isSuper && (
              <Button size="small" icon={<SettingOutlined />}
                onClick={() => openTab({ key: 'users', type: 'users', title: <span><SettingOutlined /> 用户管理</span> })}>
                用户管理
              </Button>
            )}
            {isAdmin && (
              <Button size="small" icon={<TableOutlined />}
                onClick={() => openTab({ key: 'logs', type: 'logs', title: <span>操作日志</span> })}>
                操作日志
              </Button>
            )}
          </Space>
          <div style={{ flex: 1 }} />
          <Text style={{ color: '#94a3b8', fontSize: 12, marginRight: 12 }}>
            {connections.length} 个连接 · {me.username}（{me.role}）
          </Text>
          <Button size="small" icon={<LogoutOutlined />}
            onClick={() => { clearToken(); setAuthed(false); setTabs([]) }} />
        </Header>
        <Content style={{ padding: 12, background: '#f1f5f9' }}>
          {tabs.length === 0 ? (
            <EmptyHint text="从左侧对象资源管理器点击表名打开数据浏览，或点击工具栏“新建查询”打开 SQL 控制台；连接管理在左侧连接节点上右键/工具栏进入。" />
          ) : (
            <Tabs
              type="editable-card"
              hideAdd
              activeKey={activeKey}
              onChange={setActiveKey}
              onEdit={(targetKey, action) => { if (action === 'remove') removeTab(targetKey) }}
              items={tabs.map((t) => ({
                key: t.key,
                label: t.title,
                closable: true,
                children:
                  t.type === 'table' ? <TableGrid connId={t.connId} table={t.table} readOnly={t.readOnly} canEdit={canWrite} /> :
                  t.type === 'sql' ? <SqlConsole connId={t.connId} canWrite={canWrite} /> :
                  t.type === 'conn' ? <Connections connections={connections} drivers={drivers} onChange={refreshConnections} /> :
                  t.type === 'users' ? <Users users={usersList} onChange={() => { refreshUsers() }} /> :
                  t.type === 'logs' ? <Logs /> :
                  <EmptyHint text="在左侧展开表树进行浏览" />,
              }))}
            />
          )}
        </Content>
        <Footer style={{ background: '#e2e8f0', padding: '4px 16px', fontSize: 12, color: '#475569', display: 'flex', gap: 16 }}>
          <span>uORM v0.8.0</span>
          {activeConn && <span>当前连接: {activeConn.name} ({activeConn.driver} · {activeConn.database})</span>}
          <span>就绪</span>
          <div style={{ flex: 1 }} />
          <Button size="small" type="text" icon={<SettingOutlined />}
            onClick={() => openConnTab()}>连接管理</Button>
        </Footer>
      </Layout>
    </Layout>
  )
}
