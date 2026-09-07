import React, { useEffect, useState } from 'react'
import { Button, Card, Form, Input, Layout, Menu, message, Modal, Space, Tag, Typography } from 'antd'
import {
  DatabaseOutlined, TableOutlined, ConsoleSqlOutlined,
  SettingOutlined, LogoutOutlined, ReloadOutlined,
} from '@ant-design/icons'
import { api } from './api.js'
import Connections from './pages/Connections.jsx'
import Browser from './pages/Browser.jsx'
import Console from './pages/Console.jsx'

const { Sider, Content, Header } = Layout
const { Text } = Typography

export default function App() {
  const [authed, setAuthed] = useState(!!localStorage.getItem('uorm_token'))
  const [tokenInput, setTokenInput] = useState('')
  const [connections, setConnections] = useState([])
  const [drivers, setDrivers] = useState([])
  const [activeConn, setActiveConn] = useState(null)
  const [tables, setTables] = useState([])
  const [activeTable, setActiveTable] = useState(null)
  const [view, setView] = useState('browser')
  const [collapsed, setCollapsed] = useState(false)

  const refreshConnections = async () => {
    try {
      const data = await api.listConn()
      setConnections(data.connections || [])
      setActiveConn((prev) => {
        if (prev && (data.connections || []).some((c) => c.id === prev)) return prev
        return (data.connections || [])[0]?.id ?? null
      })
    } catch (e) {
      message.error(e.message)
    }
  }

  const refreshTables = async (connId) => {
    if (!connId) { setTables([]); return }
    try {
      const data = await api.tables(connId)
      setTables(data.tables || [])
      setActiveTable((prev) => (prev && (data.tables || []).includes(prev)) ? prev : null)
    } catch (e) {
      message.error(e.message)
      setTables([])
    }
  }

  useEffect(() => {
    if (!authed) return
    api.drivers().then((d) => setDrivers(d.drivers || []))
    refreshConnections()
  }, [authed])

  if (!authed) {
    return (
      <div style={{ display: 'flex', justifyContent: 'center', alignItems: 'center', height: '100vh' }}>
        <Card title="uORM 数据库管理台" style={{ width: 380 }}>
          <Form
            onFinish={async ({ token }) => {
              try {
                await api.login(token)
                setAuthed(true)
              } catch (e) {
                message.error(e.message)
              }
            }}
          >
            <Form.Item name="token" rules={[{ required: true, message: '请输入管理令牌' }]}>
              <Input.Password placeholder="管理令牌（服务启动时打印）" autoFocus />
            </Form.Item>
            <Button type="primary" htmlType="submit" block>登录</Button>
          </Form>
        </Card>
      </div>
    )
  }

  const active = connections.find((c) => c.id === activeConn)

  const sider = (
    <Sider collapsible collapsed={collapsed} onCollapse={setCollapsed} width={230} theme="light"
      style={{ borderRight: '1px solid #eee' }}>
      <div style={{ padding: 16, fontWeight: 700, textAlign: 'center' }}>
        <DatabaseOutlined /> uORM
      </div>
      <Text type="secondary" style={{ padding: '0 16px', fontSize: 12 }}>连接</Text>
      <Menu
        selectedKeys={[activeConn || '']}
        onClick={({ key }) => { setActiveConn(key); setActiveTable(null); refreshTables(key) }}
        items={connections.map((c) => ({
          key: c.id,
          icon: <DatabaseOutlined />,
          label: <Space size={4}><span>{c.name || c.database}</span><Tag>{c.driver}</Tag></Space>,
        }))}
      />
      <Text type="secondary" style={{ padding: '0 16px', fontSize: 12 }}>表</Text>
      <Menu
        selectedKeys={[activeTable || '']}
        onClick={({ key }) => setActiveTable(key)}
        items={tables.map((t) => ({ key: t, icon: <TableOutlined />, label: t }))}
      />
    </Sider>
  )

  return (
    <Layout style={{ minHeight: '100vh' }}>
      {sider}
      <Layout>
        <Header style={{ background: '#fff', display: 'flex', alignItems: 'center', gap: 8,
          borderBottom: '1px solid #eee', padding: '0 16px', height: 56 }}>
          <Space>
            {[
              { key: 'browser', icon: <TableOutlined />, label: '数据浏览' },
              { key: 'console', icon: <ConsoleSqlOutlined />, label: 'SQL 控制台' },
              { key: 'conn', icon: <SettingOutlined />, label: '连接管理' },
            ].map((it) => (
              <Button
                key={it.key}
                type={view === it.key ? 'primary' : 'text'}
                icon={it.icon}
                onClick={() => setView(it.key)}
              >
                {it.label}
              </Button>
            ))}
          </Space>
          <div style={{ flex: 1 }} />
          <Space>
            {active && <Tag color="blue">{active.driver} · {active.database}</Tag>}
            <Button icon={<ReloadOutlined />} onClick={() => { refreshConnections(); refreshTables(activeConn) }} />
            <Button icon={<LogoutOutlined />} onClick={() => { api.logout(); setAuthed(false) }} />
          </Space>
        </Header>
        <Content style={{ padding: 16 }}>
          {view === 'conn' && (
            <Connections connections={connections} drivers={drivers} onChange={refreshConnections} />
          )}
          {view === 'browser' && (
            <Browser connId={activeConn} table={activeTable} />
          )}
          {view === 'console' && (
            <Console connId={activeConn} />
          )}
          {!activeConn && view !== 'conn' && (
            <Card><Text type="secondary">请先在左侧选择一个连接（或在“连接管理”中添加）</Text></Card>
          )}
        </Content>
      </Layout>
    </Layout>
  )
}
