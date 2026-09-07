import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

// 开发模式：npm run dev，API 代理到 C++ 服务（默认 8080）
// 生产模式：npm run build，产物 dist/ 由 C++ 服务直接托管
export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    proxy: {
      '/api': 'http://127.0.0.1:8080',
    },
  },
  build: {
    outDir: 'dist',
  },
})
