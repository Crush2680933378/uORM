// 操作日志（管理员及以上）：过滤查询 + 表格展示
import React, { useEffect, useState } from 'react'
import { Button, Card, Input, Select, Space, Table, Tag, Tooltip } from 'antd'
import { ReloadOutlined } from '@ant-design/icons'
import { api } from '../api.js'

const STATUS_COLOR = { ok: 'green', denied: 'orange', error: 'red' }
const ACTIONS = ['login', 'http', 'sql.query', 'sql.execute', 'conn.create', 'conn.delete', 'user.create', 'user.delete']

export default function Logs() {
  const [logs, setLogs] = useState([])
  const [loading, setLoading] = useState(false)
  const [user, setUser] = useState('')
  const [action, setAction] = useState('')
  const [status, setStatus] = useState('')
  const [limit, setLimit] = useState(300)
  const [offset, setOffset] = useState(0)

  const load = (p = { user, action, status, limit, offset }) => {
    setLoading(true)
    api.logs(p).then((d) => setLogs(d.logs || [])).catch((e) => message.error(e.message)).finally(() => setLoading(false))
  }

  useEffect(() => { load({ user, action, status, limit: 300, offset: 0 }) }, []) // eslint-disable-line

  const columns = [
    { title: '时间', dataIndex: 'time', width: 170 },
    {
      title: '用户', dataIndex: 'user', width: 110,
      render: (v, r) => <span>{v} <Tag style={{ marginLeft: 4 }}>{r.role}</Tag></span>,
    },
    { title: '动作', dataIndex: 'action', width: 130 },
    { title: '目标', dataIndex: 'target', ellipsis: true },
    {
      title: '结果', dataIndex: 'status', width: 90,
      render: (v) => <Tag color={STATUS_COLOR[v] || 'default'}>{v}</Tag>,
    },
    { title: '详情', dataIndex: 'detail', ellipsis: true },
    { title: 'IP', dataIndex: 'ip', width: 120 },
  ]

  return (
    <Card
      title="操作日志"
      extra={
        <Space size={4} wrap>
          <Input size="small" style={{ width: 130 }} placeholder="按用户过滤"
            value={user} onChange={(e) => setUser(e.target.value)} />
          <Input size="small" style={{ width: 130 }} placeholder="动作前缀 login/…"
            value={action} onChange={(e) => setAction(e.target.value)} />
          <Select size="small" style={{ width: 90 }} allowClear placeholder="状态"
            value={status || undefined}
            onChange={(v) => setStatus(v || '')}
            options={[
              { value: 'ok', label: 'ok' },
              { value: 'denied', label: 'denied' },
              { value: 'error', label: 'error' },
            ]} />
          <Button size="small" icon={<ReloadOutlined />} onClick={() =>
            load({ user, action, status, limit, offset })}>查询</Button>
        </Space>
      }
    >
      <Table
        className="result-table"
        rowKey={(r) => r.time + r.action + r.ip + Math.random()}
        size="small"
        loading={loading}
        columns={columns}
        dataSource={logs}
        pagination={{ pageSize: 50, showTotal: (t) => `${t} 条` }}
      />
    </Card>
  )
}
