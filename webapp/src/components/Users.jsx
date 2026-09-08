// 用户管理（仅超级管理员可见）：列表 / 新增 / 删除 / 改角色 / 重置密码
import React, { useState } from 'react'
import { Button, Card, Form, Input, Modal, Popconfirm, Select, Space, Table, Tag, message } from 'antd'
import { DeleteOutlined, PlusOutlined, KeyOutlined } from '@ant-design/icons'
import { api } from '../api.js'

const ROLE_LABEL = { superadmin: '超级管理员', admin: '管理员', user: '普通用户' }
const ROLE_COLOR = { superadmin: 'red', admin: 'blue', user: 'default' }

export default function Users({ users, onChange }) {
  const [open, setOpen] = useState(false)
  const [form] = Form.useForm()

  const columns = [
    { title: '用户名', dataIndex: 'username' },
    {
      title: '角色', dataIndex: 'role',
      render: (r) => <Tag color={ROLE_COLOR[r] || 'default'}>{ROLE_LABEL[r] || r}</Tag>,
    },
    { title: '创建时间', dataIndex: 'createdAt' },
    {
      title: '操作',
      render: (_, r) => (
        <Space>
          <Button size="small" icon={<KeyOutlined />}
            onClick={() => {
              Modal.confirm({
                title: `重置 ${r.username} 的密码`,
                content: '将重置为 uorm-reset-2026（请转告该用户尽快修改）',
                okText: '重置',
                onOk: async () => {
                  try { await api.setUserPassword(r.username, 'uorm-reset-2026'); message.success('已重置') }
                  catch (e) { message.error(e.message) }
                },
              })
            }}>重置密码</Button>
          <Popconfirm title={`确定删除用户 ${r.username}？`} onConfirm={async () => {
            try { await api.delUser(r.username); onChange() } catch (e) { message.error(e.message) }
          }}>
            <Button size="small" danger icon={<DeleteOutlined />} />
          </Popconfirm>
        </Space>
      ),
    },
  ]

  return (
    <Card
      title="用户管理"
      extra={<Button type="primary" icon={<PlusOutlined />} onClick={() => setOpen(true)}>新增用户</Button>}
    >
      <Table rowKey="username" dataSource={users} columns={columns} pagination={false} />

      <Modal title="新增用户" open={open} onCancel={() => setOpen(false)} onOk={async () => {
        try {
          const v = await form.validateFields()
          await api.addUser(v)
          setOpen(false); form.resetFields(); onChange()
          message.success('已创建')
        } catch (e) { if (e.message) message.error(e.message) }
      }} okText="创建" cancelText="取消">
        <Form form={form} layout="vertical" initialValues={{ role: 'user' }}>
          <Form.Item name="username" label="用户名" rules={[{ required: true }]}><Input /></Form.Item>
          <Form.Item name="password" label="初始密码" rules={[{ required: true, min: 6, message: '至少 6 位' }]}>
            <Input.Password />
          </Form.Item>
          <Form.Item name="role" label="角色" rules={[{ required: true }]}>
            <Select options={[
              { value: 'user', label: '普通用户（只读浏览与查询）' },
              { value: 'admin', label: '管理员（连接管理 + 全部 SQL）' },
              { value: 'superadmin', label: '超级管理员（用户管理 + 全部权限）' },
            ]} />
          </Form.Item>
        </Form>
      </Modal>
    </Card>
  )
}
