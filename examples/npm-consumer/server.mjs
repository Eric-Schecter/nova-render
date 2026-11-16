import { createServer } from 'node:http'
import { readFile } from 'node:fs/promises'
import { resolve, sep, extname } from 'node:path'
const root = process.cwd(),
  types = {
    '.js': 'text/javascript',
    '.mjs': 'text/javascript',
    '.json': 'application/json',
    '.html': 'text/html',
    '.bin': 'application/octet-stream',
  }
const server = createServer(async (req, res) => {
  try {
    const path = decodeURIComponent(new URL(req.url, 'http://localhost').pathname),
      relative = path.startsWith('/nova/') ? 'public' + path : path === '/' ? '/index.html' : path
    const file = resolve(root, '.' + (relative.startsWith('/') ? relative : '/' + relative))
    if (!file.startsWith(root + sep)) throw Error('Outside server root')
    res.setHeader('Content-Type', types[extname(file)] || 'application/octet-stream')
    res.end(await readFile(file))
  } catch {
    res.writeHead(404)
    res.end('Not found')
  }
})
server.listen(Number(process.env.PORT || 0), '127.0.0.1', () =>
  console.log('READY http://127.0.0.1:' + server.address().port),
)
