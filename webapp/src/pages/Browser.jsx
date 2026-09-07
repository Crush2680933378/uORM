import React, { useEffect, useState } from 'react'
import { Button, Card, Descriptions, Drawer, message, Space, Table, Typography } from 'antd'
import { ColumnWidthOutlined } from '@ant-design/icons'
import { api } from '../api.js'

const { Text } = Typography

function renderCell(v) {
  if (v === null || v === undefined) return <Text type="secondary" italic>NULL</Text>
  if (typeof v === 'boolean') return String(v)
  return String(v)
}

export default function Browser({ connId, table }) {
  const [data, setData] = useState(null)
  const [total, setTotal] = useState(0)
  const [limit, setLimit] = useState(50)
  const [page, setPage] = useState(1)
  const [sorter, setSorter] = useState(null) // {field, desc}
  const [loading, setLoading] = useState(false)
  const [schema, setSchema] = useState(null)
  const [schemaOpen, setSchemaOpen] = useState(false)

  useEffect(() => { setPage(1); setSorter(null) }, [connId, table])

  useEffect(() => {
    if (!connId || !table) return
    let alive = true
    setLoading(true)
    api.rows(connId, table, { limit, offset: (page - 1) * limit, orderBy: sorter?.field || '', desc: sorter?.desc ? '1' : '' })
      .then((res) => {
        if (!alive) return
        setData(res.data)
        setTotal(res.total || 0)
      })
      .catch((e) => alive && message.error(e.message))
      .finally(() => alive && setLoading(false))
    return () => { alive = false }
  }, [connId, table, limit, page, sorter])

  if (!connId || !table) return null

  const cols = (data?.columns || []).map((c) => ({
    title: (
      <Space size={4}>
        <span onClick={() => setSorter(sorter?.field === c ? { field: c, desc: !sorter.desc } : { field: c, desc: false })}
          style={{ cursor: 'pointer', userSelect: 'none' }}>
          {c}{sorter?.field === c ? (sorter.desc ? ' ↓' : ' ↑') : ''}
        </span>
      </Space>
    ),
    dataIndex: c,
    key: c,
    ellipsis: true,
    render: renderCell,
  }))

  return (
    <Card
      title={`表：${table}`}
      extra={
        <Button icon={<ColumnWidthOutlined />} onClick={async () => {
          try {
            const res = await api.columns(connId, table)
            setSchema(res.columns || [])
            setSchemaOpen(true)
          } catch (e) { message.error(e.message) }
        }}>表结构</Button>
      }
    >
      <Table
        className="result-table"
        rowKey={(_, i) => i}
        size="small"
        loading={loading}
        columns={cols}
        dataSource={data?.rows?.map((r, i) => {
          const obj = {}
          data.columns.forEach((c, j) => { obj[c] = r[j] })
          obj.__idx = i
          return obj
        }) || []}
        pagination={{
          current: page,
          pageSize: limit,
          total,
          showSizeChanger: true,
          pageSizeOptions: [10, 20, 50, 100, 200],
          onChange: (p, ps) => { setPage(p); setLimit(ps) },
          showTotal: (t) => `共 ${t} 行`,
        }}
      />

      <Drawer title={`表结构：${table}`} open={schemaOpen} onClose={() => setSchemaOpen(false)} width={620}>
        <Descriptions column={1} size="small" bordered>
          {(schema || []).map((col, i) => (
            <Descriptions.Item key={i} label={col[0]}>
              {col[1]} {col[2] === 'NO' || col[5] ? <Text type="warning">NOT NULL</Text> : null}
              {col[4] !== null && col[4] !== undefined ? <Text type="secondary"> 默认 {String(col[4])}</Text> : null}
            </Descriptions.Item>
          ))}
        </Descriptions>
      </Drawer>
    </Card>
  )
}
