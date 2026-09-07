import React, { useState } from 'react'
import { Alert, Button, Card, message, Table, Typography } from 'antd'
import { CaretRightOutlined } from '@ant-design/icons'
import { api } from '../api.js'

const { Text } = Typography
const SAMPLES = ['SELECT * FROM products LIMIT 10', 'SHOW TABLES', 'SELECT version()']

export default function Console({ connId }) {
  const [sql, setSql] = useState('SELECT * FROM products LIMIT 10')
  const [result, setResult] = useState(null)
  const [loading, setLoading] = useState(false)

  const run = async () => {
    if (!sql.trim()) return
    setLoading(true)
    try {
      const trimmed = sql.trim().toLowerCase()
      if (trimmed.startsWith('select') || trimmed.startsWith('with') || trimmed.startsWith('pragma') ||
          trimmed.startsWith('show') || trimmed.startsWith('explain')) {
        const res = await api.query(connId, sql)
        setResult({ kind: 'query', ...res })
      } else {
        const res = await api.execute(connId, sql)
        setResult({ kind: 'execute', affected: res.affected })
      }
    } catch (e) {
      message.error(e.message)
      setResult({ kind: 'error', message: e.message })
    }
    setLoading(false)
  }

  if (!connId) return null

  const cols = (result?.result?.columns || []).map((c) => ({
    title: c, dataIndex: c, key: c, ellipsis: true,
    render: (v) => (v === null || v === undefined ? <Text type="secondary" italic>NULL</Text> : String(v)),
  }))

  return (
    <Card title="SQL 控制台">
      <textarea
        className="sql-editor"
        value={sql}
        onChange={(e) => setSql(e.target.value)}
        onKeyDown={(e) => { if (e.ctrlKey && e.key === 'Enter') run() }}
        placeholder="输入 SQL，Ctrl+Enter 执行"
      />
      <div style={{ margin: '8px 0', display: 'flex', justifyContent: 'space-between' }}>
        <div>
          {SAMPLES.map((s) => (
            <a key={s} style={{ marginRight: 12 }} onClick={() => setSql(s)}>{s}</a>
          ))}
        </div>
        <Button type="primary" icon={<CaretRightOutlined />} loading={loading} onClick={run}>
          执行 (Ctrl+Enter)
        </Button>
      </div>

      {result?.kind === 'execute' && (
        <Alert type="success" message={`执行成功，受影响 ${result.affected} 行`} />
      )}
      {result?.kind === 'error' && (
        <Alert type="error" message={result.message} />
      )}
      {result?.kind === 'query' && (
        <>
          {result.truncated && <Alert type="warning" message={`结果已截断（最多返回行数）`} style={{ marginBottom: 8 }} />}
          <Table
            className="result-table"
            rowKey={(_, i) => i}
            size="small"
            columns={cols}
            dataSource={result.result.rows.map((r, i) => {
              const obj = {}
              result.result.columns.forEach((c, j) => { obj[c] = r[j] })
              obj.__idx = i
              return obj
            })}
            pagination={{ pageSize: 50, showTotal: (t) => `${t} 行` }}
          />
        </>
      )}
    </Card>
  )
}
