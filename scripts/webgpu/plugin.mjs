import { compile } from './compiler.mjs'
export default function shaderCompiler() {
  let busy = false
  return {
    name: 'nova-local-hlsl',
    configureServer(server) {
      server.middlewares.use('/__nova/compile', async (req, res) => {
        const send = (status, value) => {
          res.statusCode = status
          res.setHeader('Content-Type', 'application/json')
          res.end(JSON.stringify(value))
        }
        if (req.method !== 'POST') return send(405, { error: 'POST required' })
        const origin = req.headers.origin
        if (!origin || !['http://127.0.0.1:5173', 'http://localhost:5173'].includes(origin))
          return send(403, { error: 'Only local preview origin is allowed' })
        if (busy) return send(429, { error: '编译中，请稍后再试' })
        busy = true
        try {
          let body = ''
          for await (const chunk of req) {
            body += chunk
            if (Buffer.byteLength(body) > 70000) throw Error('请求过大')
          }
          send(200, await compile(JSON.parse(body).source))
        } catch (e) {
          send(400, { error: e.message })
        } finally {
          busy = false
        }
      })
    },
  }
}
