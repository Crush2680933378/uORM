// 数据网格标签页：分页浏览 / 排序 / 行编辑 / 新增 / 删除（Navicat 风格）
import React, { useEffect, useState } from 'react'
import { Alert, Button, Card, Checkbox, Form, Input, message, Modal, Space, Table, Typography } from 'antd'
import { PlusOutlined, ReloadOutlined, DeleteOutlined, EditOutlined, ColumnWidthOutlined } from '@ant-design/icons'
import { api } from '../api.js'

const { Text } = Typography

const isNumericType = (t) => /int|decimal|numeric|real|float|double|bool/i.test(t || '')

// 按列类型把界面字符串转换为绑定参数（留空 = NULL）
function makeParam(colMeta, value) {
  if (value === '' || value === null || value === undefined) return { type: 0 }
  if (isNumericType(colMeta)) {
    const n = Number(value)
    if (!isNaN(n)) return Number.isInteger(n) ? { type: 1, i: n } : { type: 2, d: n }
  }
  return { type: 3, s: String(value) }
}

export default function TableGrid({ connId, table }) {
  const [data, setData] = useState(null)
  const [total, setTotal] = useState(0)
  const [page, setPage] = useState(1)
  const [pageSize, setPageSize] = useState(50)
  const [sorter, setSorter] = useState(null)
  const [loading, setLoading] = useState(false)
  const [columnsMeta, setColumnsMeta] = useState([]) // [name,type,nullable,default,isPK]
  const [editModal, setEditModal] = useState(null)   // {mode, values:{col:value|nullCheck}, old}
  const [form] = Form.useForm()

  const pkCol = columnsMeta.find((c) => String(c[4]) === '1' || c[4] === true)?.[0] || null

  const loadAll = (p = page, ps = pageSize, s = sorter) => {
    if (!connId || !table) return
    setLoading(true)
    Promise.all([
      api.columns(connId, table).catch(() => null),
      api.rows(connId, table, {
        limit: ps, offset: (p - 1) * ps,
        orderBy: s?.field || '', desc: s?.desc ? '1' : '',
      }),
    ]).then(([cols, rowsRes]) => {
      setColumnsMeta(cols?.columns || [])
      setData(rowsRes.data)
      setTotal(rowsRes.total || 0)
    }).catch((e) => message.error(e.message)).finally(() => setLoading(false))
  }

  useEffect(() => { setPage(1); setSorter(null) }, [connId, table])
  useEffect(() => { loadAll() }, [connId, table, page, pageSize, sorter])

  if (!connId || !table) return null

  const gridColumns = [
    {
      title: '操作', key: '__ops', width: 100,
      render: (_, record) => (
        <Space size={4}>
          <Button size="small" type="text" icon={<EditOutlined />}
            onClick={() => openEdit(record)} disabled={!pkCol} />
          <Button size="small" type="text" danger icon={<DeleteOutlined />}
            onClick={() => {
              Modal.confirm({
                title: '确认删除该行？',
                content: `表 ${table}${pkCol ? `，${pkCol} = ${record[pkCol]}` : ''}`,
                okText: '删除', okType: 'danger', cancelText: '取消',
                onOk: () => deleteRow(record),
              })
            }} disabled={!pkCol} />
        </Space>
      ),
    },
    ...(data?.columns || []).map((c) => ({
      title: (
        <span style={{ cursor: 'pointer', userSelect: 'none' }} onClick={() =>
          setSorter(sorter?.field === c ? { field: c, desc: !sorter.desc } : { field: c, desc: false })}>
          {c}{sorter?.field === c ? (sorter.desc ? ' ↓' : ' ↑') : ''}
        </span>
      ),
      dataIndex: c, key: c, ellipsis: true,
      render: (v) => (v === null || v === undefined) ? <Text type="secondary" italic>NULL</Text> : String(v),
    })),
  ]

  const gridData = (data?.rows || []).map((row, i) => {
    const obj = { __idx: i }
    ;(data?.columns || []).forEach((c, j) => { obj[c] = row[j] })
    return obj
  })

  const openEdit = (record) => {
    if (!pkCol) { message.warning('该表没有主键，无法定位行编辑'); return }
    setEditModal({
      mode: 'edit',
      values: Object.fromEntries((data?.columns || []).map((c) => [c, record[c] === null || record[c] === undefined ? '' : String(record[c])])),
      old: Object.fromEntries((data?.columns || []).map((c) => [c, record[c] === null || record[c] === undefined ? null : record[c]])),
    })
  }

  const openInsert = () => {
    setEditModal({
      mode: 'insert',
      values: Object.fromEntries((data?.columns || []).filter((c) => c !== pkCol).map((c) => [c, ''])),
      old: {},
    })
  }

  const saveEdit = async () => {
    const { mode, values, old } = editModal
    const metaOf = (c) => columnsMeta.find((m) => m[0] === c)?.[1]
    const paramFor = (c, v) => makeParam(metaOf(c), v)
    try {
      if (mode === 'insert') {
        const filled = (data?.columns || []).filter((c) => c !== pkCol && values[c] !== '' && values[c] !== undefined)
        if (!filled.length) { message.warning('没有填写任何值'); return }
        const sql = `INSERT INTO \`${table}\` (${filled.map((c) => `\`${c}\``).join(', ')}) VALUES (${filled.map(() => '?').join(', ')})`
        await api.execute(connId, sql, filled.map((c) => paramFor(c, values[c])))
      } else {
        const changed = (data?.columns || []).filter((c) => {
          if (c === pkCol) return false
          const nv = values[c], ov = old[c]
          return (nv === '' ? null : nv) !== (ov === '' ? null : ov) && String(nv) !== String(ov)
        })
        if (!changed.length) { message.info('没有修改'); setEditModal(null); return }
        const sql = `UPDATE \`${table}\` SET ${changed.map((c) => `\`${c}\` = ?`).join(', ')} WHERE \`${pkCol}\` = ?`
        const params = [...changed.map((c) => paramFor(c, values[c])), paramFor(pkCol, old[pkCol])]
        await api.execute(connId, sql, params)
      }
      setEditModal(null)
      message.success('已保存')
      loadAll()
    } catch (e) { message.error(e.message) }
  }

  const deleteRow = (record) => {
    if (!pkCol) { message.warning('该表没有主键，无法删除'); return }
    const sql = `DELETE FROM \`${table}\` WHERE \`${pkCol}\` = ?`
    const pkMeta = columnsMeta.find((m) => m[0] === pkCol)?.[1]
    const params = [makeParam(pkMeta, String(record[pkCol]))]
    api.execute(connId, sql, params).then(() => { message.success('已删除'); loadAll() })
      .catch((e) => message.error(e.message))
  }

  return (
    <Card
      title={<span>表：<Text code>{table}</Text></span>}
      extra={
        <Space>
          <Button size="small" icon={<ColumnWidthOutlined />}
            onClick={() => api.columns(connId, table).then((r) => setColumnsMeta(r.columns || []))
              .then(() => message.info('列信息已刷新'))}>结构</Button>
          <Button size="small" type="primary" icon={<PlusOutlined />} onClick={openInsert}>新增行</Button>
          <Button size="small" icon={<ReloadOutlined />} onClick={() => loadAll()} />
        </Space>
      }
    >
      {!pkCol && <Alert type="warning" showIcon style={{ marginBottom: 8 }}
        message="该表没有主键，编辑/删除不可用" />}
      <Table
        className="result-table"
        rowKey="__idx"
        size="small"
        loading={loading}
        columns={gridColumns}
        dataSource={gridData}
        pagination={{
          current: page, pageSize, total,
          showSizeChanger: true, pageSizeOptions: [10, 20, 50, 100, 200],
          onChange: (p, ps) => { setPage(p); setPageSize(ps) },
          showTotal: (t) => `共 ${t} 行`,
        }}
      />

      <Modal
        title={editModal?.mode === 'insert' ? '新增行' : '编辑行'}
        open={!!editModal}
        onCancel={() => setEditModal(null)}
        onOk={saveEdit}
        okText="保存"
        cancelText="取消"
        destroyOnClose
      >
        {(editModal?.values ? Object.keys(editModal.values) : []).map((c) => {
          const isPk = c === pkCol && editModal?.mode === 'edit'
          const nullable = (() => {
            const m = columnsMeta.find((m) => m[0] === c)
            return m ? String(m[2]) !== 'NO' : true
          })()
          return (
            <Form.Item key={c} label={isPk ? `${c}（主键）` : c}>
              <Input
                value={editModal.values[c]}
                disabled={isPk}
                placeholder={nullable ? '留空 = NULL' : '必填'}
                onChange={(e) => setEditModal((m) => ({ ...m, values: { ...m.values, [c]: e.target.value } }))}
              />
            </Form.Item>
          )
        })}
      </Modal>
    </Card>
  )
}
