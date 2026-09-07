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
