// SQL 控制台标签页：编辑执行 / 历史 / 耗时 / 错误与结果面板
import React, { useEffect, useRef, useState } from 'react'
import { Alert, Button, Card, message, Space, Table, Tag, Typography } from 'antd'
import { CaretRightOutlined, HistoryOutlined } from '@ant-design/icons'
import { api } from '../api.js'

const { Text } = Typography
const HIST_KEY = 'uorm_sql_history'

const isQuerySql = (sql) => {
  const head = (sql || '').replace(/^[\s(;]+/, '').slice(0, 7).toLowerCase()
  return /^(select|with|pragma|show|explain|desc)/.test(head)
}

export default function Console({ connId }) {
  const [sql, setSql] = useState('SELECT * FROM products LIMIT 10')
  const [result, setResult] = useState(null)
  const [history, setHistory] = useState(() => {
    try { return JSON.parse(localStorage.getItem(HIST_KEY) || '[]') } catch { return [] }
  })
  const [running, setRunning] = useState(false)
  const [elapsed, setElapsed] = useState(null)
  const editorRef = useRef(null)

  const run = async () => {
    const text = sql.trim()
    if (!text || !connId) return
    setRunning(true)
    const t0 = performance.now()
    try {
      let res
      if (isQuerySql(text)) {
        res = { kind: 'query', ...(await api.query(connId, text)) }
      } else {
        const affected = await api.execute(connId, text)
        res = { kind: 'execute', affected }
      }
      res.elapsed = Math.round(performance.now() - t0)
      setResult(res)
      const hist = [text, ...history.filter((h) => h !== text)].slice(0, 20)
      setHistory(hist)
      localStorage.setItem(HIST_KEY, JSON.stringify(hist))
    } catch (e) {
      setResult({ kind: 'error', message: e.message, elapsed: Math.round(performance.now() - t0) })
    }
    setRunning(false)
  }

  if (!connId) return null

  const cols = (result?.result?.columns || []).map((c) => ({
    title: c, dataIndex: c, key: c, ellipsis: true,
    render: (v) => (v === null || v === undefined) ? <Text type="secondary" italic>NULL</Text> : String(v),
  }))

  return (
    <Card
      title="SQL 控制台"
      extra={
        <Space>
          <HistoryOutlined style={{ color: '#64748b' }} />
          {history.slice(0, 5).map((h, i) => (
            <a key={i} style={{ fontSize: 12 }} onClick={() => setSql(h)}>{h.slice(0, 24)}…</a>
          ))}
        </Space>
      }
    >
      <textarea
        ref={editorRef}
        className="sql-editor"
        value={sql}
        onChange={(e) => setSql(e.target.value)}
        onKeyDown={(e) => { if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') run() }}
        placeholder="输入 SQL，Ctrl+Enter 执行；选中文本可只执行选中部分"
        spellCheck={false}
      />
      <div style={{ margin: '8px 0', display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        <Space size={8}>
          {['SELECT * FROM users LIMIT 10', 'SHOW TABLES', 'SELECT version()'].map((s) => (
            <a key={s} style={{ fontSize: 12 }} onClick={() => setSql(s)}>{s}</a>
          ))}
        </Space>
        <Button type="primary" icon={<CaretRightOutlined />} loading={running} onClick={run}>
          执行 (Ctrl+Enter)
        </Button>
      </div>

      {result?.kind === 'execute' && (
        <Alert type="success" showIcon message={`执行成功，受影响 ${result.affected} 行`} />
      )}
      {result?.kind === 'error' && (
        <Alert type="error" showIcon message={result.message} />
      )}
      {result?.kind === 'query' && (
        <>
          {result.truncated && <Alert type="warning" showIcon style={{ marginBottom: 8 }} message="结果已截断" />}
          {result.elapsed != null && (
            <Tag style={{ marginBottom: 8 }}>耗时 {result.elapsed} ms</Tag>
          )}
          <Table
            className="result-table"
            rowKey={(_, i) => i}
            size="small"
            columns={cols}
            dataSource={result.result.rows.map((row, i) => {
              const obj = {}
              result.result.columns.forEach((c, j) => { obj[c] = row[j] })
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
