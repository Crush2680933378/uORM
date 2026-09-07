import React, { useState } from 'react'
import { Button, Card, Form, Input, InputNumber, message, Modal, Popconfirm, Select, Space, Switch, Table } from 'antd'
import { ApiOutlined, DeleteOutlined, PlusOutlined } from '@ant-design/icons'
import { api } from '../api.js'

const DRIVER_LABEL = { mysql: 'MySQL', mariadb: 'MariaDB', postgresql: 'PostgreSQL', postgres: 'PostgreSQL', sqlite: 'SQLite' }

export default function Connections({ connections, drivers, onChange }) {
  const [open, setOpen] = useState(false)
  const [form] = Form.useForm()
  const [testing, setTesting] = useState(null)
  const driver = Form.useWatch('driver', form)
  const isSqlite = driver === 'sqlite'

  const columns = [
    { title: '名称', dataIndex: 'name' },
    { title: '驱动', dataIndex: 'driver', render: (d) => DRIVER_LABEL[d] || d },
    { title: '主机', dataIndex: 'host', render: (h, r) => (r.driver === 'sqlite' ? r.database : `${h}:${r.port}`) },
    { title: '数据库', dataIndex: 'database' },
    { title: '用户', dataIndex: 'username', render: (u) => u || '-' },
    {
      title: '操作',
      render: (_, r) => (
        <Space>
          <Button size="small" icon={<ApiOutlined />} loading={testing === r.id}
            onClick={async () => {
              setTesting(r.id)
              try {
                const res = await api.testConn(r.id)
                res.ok ? message.success('连接成功') : message.error('连接失败')
              } catch (e) { message.error(e.message) }
              setTesting(null)
            }}>测试</Button>
          <Popconfirm title="确定删除该连接？" onConfirm={async () => {
            try { await api.delConn(r.id); onChange() } catch (e) { message.error(e.message) }
          }}>
            <Button size="small" danger icon={<DeleteOutlined />} />
          </Popconfirm>
        </Space>
      ),
    },
  ]

  return (
    <Card title="连接管理" extra={
      <Button type="primary" icon={<PlusOutlined />} onClick={() => { form.resetFields(); form.setFieldsValue({ driver: 'mysql', host: '127.0.0.1', port: 3306, poolSize: 3 }); setOpen(true) }}>
        新建连接
      </Button>
    }>
      <Table rowKey="id" dataSource={connections} columns={columns} pagination={false} />

      <Modal title="新建连接" open={open} onCancel={() => setOpen(false)} onOk={async () => {
        try {
          const values = await form.validateFields()
          await api.addConn(values)
          setOpen(false)
          onChange()
          message.success('已添加')
        } catch (e) {
          if (e.message) message.error(e.message)
        }
      }} okText="保存" cancelText="取消">
        <Form form={form} layout="vertical">
          <Form.Item name="name" label="名称" rules={[{ required: true }]}><Input placeholder="生产库 / 本地测试..." /></Form.Item>
          <Form.Item name="driver" label="类型" rules={[{ required: true }]}>
            <Select options={(drivers.length ? drivers : ['mysql', 'postgresql', 'sqlite']).map((d) => ({ value: d, label: DRIVER_LABEL[d] || d }))} />
          </Form.Item>
          {isSqlite ? (
            <Form.Item name="database" label="数据库文件路径" rules={[{ required: true }]}><Input placeholder="./data/app.db" /></Form.Item>
          ) : (
            <>
              <Space>
                <Form.Item name="host" label="主机" rules={[{ required: true }]}><Input /></Form.Item>
                <Form.Item name="port" label="端口" rules={[{ required: true }]}><InputNumber min={1} max={65535} /></Form.Item>
              </Space>
              <Form.Item name="username" label="用户名" rules={[{ required: true }]}><Input /></Form.Item>
              <Form.Item name="password" label="密码" rules={[{ required: true }]}><Input.Password /></Form.Item>
              <Form.Item name="database" label="数据库" rules={[{ required: true }]}><Input /></Form.Item>
            </>
          )}
          <Space>
            <Form.Item name="poolSize" label="连接池大小"><InputNumber min={1} max={50} /></Form.Item>
            <Form.Item name="useTLS" label="TLS" valuePropName="checked"><Switch /></Form.Item>
          </Space>
        </Form>
      </Modal>
    </Card>
  )
}
