// 文件说明：
// main —— React 入口：挂载根组件、配置 Ant Design 中文语境与主题色。

import React from 'react'
import ReactDOM from 'react-dom/client'
import { ConfigProvider, theme } from 'antd'
import zhCN from 'antd/locale/zh_CN'
import App from './App.jsx'
import './styles.css'

ReactDOM.createRoot(document.getElementById('root')).render(
  <ConfigProvider
    locale={zhCN}
    theme={{ algorithm: theme.defaultAlgorithm, token: { colorPrimary: '#2563eb' } }}
  >
    <App />
  </ConfigProvider>,
)
