// 数据网格（Navicat 风格）：双击单元格行内编辑 / 新增行 / 删除行 / 排序 / 分页 /
// CSV 导出 / NULL 与空串区分 / 表结构抽屉（含建表 DDL）
import React, { useEffect, useState } from 'react'
import { Alert, Button, Card, Form, Input, message, Modal, Space, Table, Tooltip, Typography, Drawer } from 'antd'
import {
  PlusOutlined, ReloadOutlined, DeleteOutlined, EditOutlined,
  ColumnWidthOutlined, ExportOutlined, FileTextOutlined, CheckOutlined, CloseOutlined,
} from '@ant-design/icons'
import { api, getToken } from '../api.js'

const { Text } = Typography

const isNumericType = (t) => /int|decimal|numeric|real|float|double|serial/i.test(t || '')

function makeParam(colMeta, value) {
  if (value === '' || value === null || value === undefined) return { type: 0 }
  if (isNumericType(colMeta)) {
    const n = Number(value)
    if (!isNaN(n)) return Number.isInteger(n) ? { type: 1, i: n } : { type: 2, d: n }
  }
  return { type: 3, s: String(value) }
}

// 单元格显示
function CellView({ v }) {
  if (v === null || v === undefined) return <span style={{ color: '#b6bcc6', fontStyle: 'italic' }}>(NULL)</span>
  if (v === '') return <span style={{ color: '#b6bcc6' }}>(空串)</span>
  const s = String(v)
  if (s.length > 120) return <span title={s}>{s.slice(0, 120)}…</span>
  return <span>{s}</span>
}

export default function TableGrid({ connId, table, readOnly = false }) {
  const [data, setData] = useState(null)
  const [total, setTotal] = useState(0)
  const [page, setPage] = useState(1)
  const [pageSize, setPageSize] = useState(50)
  const [sorter, setSorter] = useState(null)
  const [loading, setLoading] = useState(false)
  const [columnsMeta, setColumnsMeta] = useState([])
  const [ddl, setDdl] = useState(null)
  const [ddlOpen, setDdlOpen] = useState(false)
  const [filter, setFilter] = useState('')

  // 行内编辑状态：{rowIdx, colName, value}
  const [editing, setEditing] = useState(null)
  // 新增行：{values:{}}（多行数组可扩展）
  const [insertRow, setInsertRow] = useState(null) // null | {values:{}}
  const [selected, setSelected] = useState([])     // 选中的行（用于删除）

  const pkCol = columnsMeta.find((c) => String(c[4]) === '1' || c[4] === true)?.[0] || null
  const canEdit = !readOnly && !!pkCol

  const metaOf = (c) => columnsMeta.find((m) => m[0] === c)?.[1] || ''

  const loadAll = () => {
    if (!connId || !table) return
    setLoading(true)
    Promise.all([
      api.columns(connId, table).catch(() => null),
      api.rows(connId, table, {
        limit: pageSize, offset: (page - 1) * pageSize,
        orderBy: sorter?.field || '', desc: sorter?.desc ? '1' : '',
      }),
    ]).then(([cols, rowsRes]) => {
      setColumnsMeta(cols?.columns || [])
      setData(rowsRes.data)
      setTotal(rowsRes.total || 0)
    }).catch((e) => message.error(e.message)).finally(() => setLoading(false))
  }

  useEffect(() => { setPage(1); setSorter(null); setEditing(null); setInsertRow(null); setSelected([]) }, [connId, table])
  useEffect(() => { loadAll() }, [connId, table, page, pageSize, sorter])

  if (!connId || !table) return null

  // ---------------- 行内编辑 ----------------
  const startEdit = (rowIdx, colName, value) => {
    if (readOnly) { message.info('视图为只读'); return }
    if (!pkCol) { message.warning('该表没有主键，无法编辑'); return }
    if (colName === pkCol) { message.warning('主键列不可编辑'); return }
    setEditing({ rowIdx, colName, value: value === null || value === undefined ? '' : String(value) })
  }

  const commitEdit = () => {
    if (!editing) return
    const { rowIdx, colName } = editing
    const rowObj = gridData.find((g) => g.__idx === rowIdx)
    const oldValue = rowObj ? rowObj[colName] : null
    const param = makeParam(metaOf(colName), editing.value)
    const sql = `UPDATE \`${table}\` SET \`${colName}\` = ? WHERE \`${pkCol}\` = ?`
    const pkParam = makeParam(metaOf(pkCol), String(rowObj?.[pkCol] ?? ''))
    api.execute(connId, sql, [param, pkParam]).then(() => {
      message.success('已保存')
      setEditing(null)
      loadAll()
    }).catch((e) => message.error(e.message))
  }

  const cancelEdit = () => setEditing(null)

  // ---------------- 新增行 ----------------
  const insertCols = (data?.columns || []).filter((c) => c !== pkCol)
  const saveInsert = () => {
    if (!insertRow) return
    const filled = insertCols.filter((c) => insertRow.values[c] !== '' && insertRow.values[c] !== undefined && insertRow.values[c] !== null)
    if (!filled.length) { message.warning('请至少填写一个字段'); return }
    const sql = `INSERT INTO \`${table}\` (${filled.map((c) => `\`${c}\``).join(', ')}) VALUES (${filled.map(() => '?').join(', ')})`
    const params = filled.map((c) => makeParam(metaOf(c), insertRow.values[c]))
    api.execute(connId, sql, params).then(() => {
      message.success('已插入')
      setInsertRow(null)
      loadAll()
    }).catch((e) => message.error(e.message))
  }

  // ---------------- 删除选中行 ----------------
  const deleteSelected = () => {
    if (!pkCol) { message.warning('该表没有主键，无法删除'); return }
    if (!selected.length) { message.warning('请先勾选要删除的行'); return }
    Modal.confirm({
      title: `确认删除选中的 ${selected.length} 行？`,
      okText: '删除', okType: 'danger', cancelText: '取消',
      onOk: async () => {
        for (const idx of selected) {
          const obj = gridData.find((g) => g.__idx === idx)
          if (!obj) continue
          const sql = `DELETE FROM \`${table}\` WHERE \`${pkCol}\` = ?`
          const params = [makeParam(metaOf(pkCol), String(obj[pkCol]))]
          await api.execute(connId, sql, params)
        }
        message.success('已删除')
        setSelected([])
        loadAll()
      },
    })
  }

  // ---------------- 导出 CSV ----------------
  const exportCsv = () => {
    const cols = data?.columns || []
    const esc = (s) => {
      const v = s === null || s === undefined ? '' : String(s)
      return /[",\n]/.test(v) ? '"' + v.replace(/"/g, '""') + '"' : v
    }
    let csv = '\uFEFF' + cols.map(esc).join(',') + '\n'
    for (const row of data?.rows || []) csv += row.map(esc).join(',') + '\n'
    const blob = new Blob([csv], { type: 'text/csv;charset=utf-8' })
    const a = document.createElement('a')
    a.href = URL.createObjectURL(blob)
    a.download = `${table}.csv`
    a.click()
    URL.revokeObjectURL(a.href)
  }

  // ---------------- DDL 抽屉 ----------------
  const showDdl = () => {
    api.columns(connId, table).then((r) => setColumnsMeta(r.columns || [])).catch(() => {})
    api.query(connId, `SELECT 1`).catch(() => {})
    fetch(`/api/connections/${connId}/tables/${encodeURIComponent(table)}/ddl`, {
      headers: { 'X-Auth-Token': getToken() },
    }).then((r2) => r2.json()).then((j) => {
      setDdl(j.ddl || '(无 DDL)')
      setDdlOpen(true)
    }).catch((e) => message.error(e.message))
  }

  // ---------------- 过滤（前端过滤当前结果） ----------------
  const filterRows = (rows) => {
    if (!filter.trim()) return rows
    const kw = filter.toLowerCase()
    return rows.filter((row) => row.some((v) => v !== null && v !== undefined && String(v).toLowerCase().includes(kw)))
  }

  const visibleRows = filterRows(data?.rows || [])
  const showGrid = (data?.columns || []).map((c, ci) => {
    const meta = columnsMeta.find((m) => m[0] === c)
    return {
      title: (
        <span style={{ cursor: 'pointer', userSelect: 'none' }} onClick={() =>
          setSorter(sorter?.field === c ? { field: c, desc: !sorter.desc } : { field: c, desc: false })}>
          {c}{sorter?.field === c ? (sorter.desc ? ' ↓' : ' ↑') : ''}
          <div style={{ fontWeight: 400, fontSize: 11, color: '#94a3b8' }}>{String(meta?.[1] || '').split('(')[0]}</div>
        </span>
      ),
      dataIndex: c, key: c, ellipsis: true,
      onCell: (record) => ({
        onDoubleClick: () => startEdit(record.__idx, c, record[c]),
      }),
      render: (v, record) => {
        if (editing && editing.rowIdx === record.__idx && editing.colName === c) {
          return (
            <Input
              size="small" autoFocus value={editing.value}
              onChange={(e) => setEditing((ed) => ({ ...ed, value: e.target.value }))}
              onPressEnter={commitEdit}
              onKeyDown={(e) => { if (e.key === 'Escape') cancelEdit() }}
              onBlur={commitEdit}
              style={{ margin: -4 }}
            />
          )
        }
        return <CellView v={v} />
      },
    }
  })

  const gridData = visibleRows.map((row, i) => {
    const obj = { __idx: i }
    ;(data?.columns || []).forEach((c, j) => { obj[c] = row[j] })
    return obj
  })

  return (
    <Card
      title={<span>表：<Text code>{table}</Text>{readOnly && <Tag>视图（只读）</Tag>}</span>}
      extra={
        <Space size={4} wrap>
          <Input.Search
            size="small" placeholder="过滤当前结果…" allowClear
            style={{ width: 160 }} onSearch={(v) => setFilter(v)}
          />
          <Tooltip title="新增行"><Button size="small" type="primary" icon={<PlusOutlined />} onClick={() => {
            setInsertRow({ values: Object.fromEntries(insertCols.map((c) => [c, ''])) })
          }} /></Tooltip>
          <Tooltip title="删除选中行"><Button size="small" danger icon={<DeleteOutlined />} onClick={deleteSelected}
            disabled={!selected.length || !pkCol} /></Tooltip>
          <Tooltip title="结构 / DDL"><Button size="small" icon={<FileTextOutlined />} onClick={showDdl} /></Tooltip>
          <Tooltip title="导出 CSV"><Button size="small" icon={<ExportOutlined />} onClick={exportCsv} /></Tooltip>
          <Tooltip title="刷新"><Button size="small" icon={<ReloadOutlined />} onClick={loadAll} /></Tooltip>
        </Space>
      }
    >
      {!pkCol && !readOnly && <Alert type="warning" showIcon style={{ marginBottom: 8 }}
        message="该表没有主键：双击编辑/删除不可用，仅浏览" />}
      {readOnly && <Alert type="info" showIcon style={{ marginBottom: 8 }} message="视图为只读对象" />}
      <Table
        className="result-table"
        rowKey="__idx"
        size="small"
        loading={loading}
        columns={showGrid}
        dataSource={gridData}
        rowSelection={readOnly ? undefined : {
          selectedRowKeys: selected,
          onChange: (keys) => setSelected(keys.map(Number)),
          columnWidth: 32,
        }}
        pagination={{
          current: page, pageSize, total: filter ? gridData.length : total,
          showSizeChanger: true, pageSizeOptions: [10, 20, 50, 100, 200],
          onChange: (p, ps) => { setPage(p); setPageSize(ps) },
          showTotal: (t) => `共 ${t} 行`,
        }}
      />

      {/* 新增行 Modal */}
      <Modal
        title="新增行"
        open={!!insertRow}
        onCancel={() => setInsertRow(null)}
        onOk={saveInsert}
        okText="插入"
        cancelText="取消"
        destroyOnClose
      >
        {insertCols.map((c) => (
          <Form.Item key={c} label={c}>
            <Input value={insertRow.values[c]}
              onChange={(e) => setInsertRow((r) => ({ ...r, values: { ...r.values, [c]: e.target.value } }))}
              placeholder="留空 = NULL" />
          </Form.Item>
        ))}
      </Modal>

      {/* 结构 + DDL 抽屉 */}
      <Drawer title={`结构：${table}`} open={ddlOpen} onClose={() => setDdlOpen(false)} width={640}>
        <Table
          size="small" pagination={false} className="result-table"
          dataSource={columnsMeta.map((c, i) => ({ key: i, name: c[0], type: c[1], nullable: c[2], def: c[3], pk: c[4] }))}
          columns={[
            { title: '列名', dataIndex: 'name' },
            { title: '类型', dataIndex: 'type' },
            { title: '可空', dataIndex: 'nullable', render: (v) => String(v) },
            { title: '默认', dataIndex: 'def', render: (v) => (v === '' || v === null) ? '-' : String(v) },
            { title: '主键', dataIndex: 'pk', render: (v) => (String(v) === '1' || v === true) ? '是' : '' },
          ]}
        />
        <div style={{ marginTop: 16 }}>
          <Text strong>建表语句</Text>
          <pre className="sql-editor" style={{ whiteSpace: 'pre-wrap', maxHeight: 300, overflow: 'auto' }}>{ddl || '(无)'}</pre>
        </div>
      </Drawer>
    </Card>
  )
}
