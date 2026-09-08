// 对象资源管理器：连接 -> 表 -> 列（Navicat 风格左栏树）
import React, { useEffect, useState } from 'react'
import { Tree, Tag, Tooltip } from 'antd'
import {
  DatabaseOutlined, TableOutlined, ReloadOutlined,
} from '@ant-design/icons'
import { api } from '../api.js'

const DRIVER_COLOR = { mysql: 'blue', postgresql: 'geekblue', sqlite: 'green', mariadb: 'cyan', postgres: 'geekblue' }

export default function ObjectExplorer({
  connections, tablesMap, columnsMap, activeConn, activeTable,
  onSelectConn, onSelectTable, onRefresh,
}) {
  const [expanded, setExpanded] = useState([])

  // 默认展开活动连接节点
  useEffect(() => {
    if (activeConn && !expanded.includes(activeConn)) {
      setExpanded((e) => [...e, activeConn])
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [activeConn])

  const treeData = connections.map((c) => ({
    key: c.id,
    icon: <DatabaseOutlined style={{ color: c.id === activeConn ? '#2563eb' : undefined }} />,
    title: (
      <Tooltip title={`${c.driver} · ${c.database}`}>
        <span style={{ fontWeight: c.id === activeConn ? 600 : 400 }}>{c.name}</span>
      </Tooltip>
    ),
    children: (tablesMap[c.id] || []).map((t) => ({
      key: `t:${c.id}:${t.name}`,
      icon: <TableOutlined />,
      title: <span style={{ fontSize: 13 }}>{t.name}</span>,
      children: (columnsMap[`${c.id}:${t.name}`] || []).map((col) => ({
        key: `c:${c.id}:${t.name}:${col[0]}`,
        icon: <span style={{ fontSize: 10, color: '#94a3b8' }}>▸</span>,
        title: (
          <span style={{ fontSize: 12, color: '#475569' }}>
            {col[0]} <span style={{ color: '#94a3b8' }}>{col[1]}</span>
          </span>
        ),
        selectable: false,
      })),
    })),
  }))

  return (
    <div style={{ padding: '8px 0', height: '100%', display: 'flex', flexDirection: 'column' }}>
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', padding: '0 12px 6px' }}>
        <span style={{ fontSize: 12, color: '#64748b', fontWeight: 600 }}>对象资源管理器</span>
        <ReloadOutlined style={{ cursor: 'pointer', color: '#64748b' }} onClick={onRefresh} />
      </div>
      <div style={{ flex: 1, overflow: 'auto' }}>
        <Tree
          showIcon
          blockNode
          expandedKeys={expanded}
          onExpand={(keys) => setExpanded(keys)}
          treeData={treeData}
          selectedKeys={activeTable ? [`t:${activeConn}:${activeTable}`] : []}
          onSelect={(_, info) => {
            const key = info.node.key
            if (key.startsWith('t:')) {
              const [, connId, ...rest] = key.split(':')
              onSelectConn(connId)
              onSelectTable(rest.join(':'))
            } else {
              onSelectConn(key)
            }
          }}
        />
      </div>
    </div>
  )
}
