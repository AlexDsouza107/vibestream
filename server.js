// server.js - tiny web server (Node.js built-ins only, no npm install needed).
// Spawns the C engine once and forwards each browser command to it, one at a time.
const http = require('http');
const fs = require('fs');
const path = require('path');
const readline = require('readline');
const { spawn } = require('child_process');

const PORT = process.env.PORT || 3000;
const WEB = path.join(__dirname, 'web');
const ENGINE = path.join(__dirname, process.platform === 'win32' ? 'engine.exe' : 'engine');

if (!fs.existsSync(ENGINE)) {
  console.error('C engine not found. Run `make` first (or use `npm start`).');
  process.exit(1);
}

const engine = spawn(ENGINE, [], { stdio: ['pipe', 'pipe', 'inherit'] });
engine.on('exit', (code) => { console.error('Engine exited (' + code + ')'); process.exit(1); });

// The engine answers every command with exactly one line, so we match them FIFO.
const waiting = [];
readline.createInterface({ input: engine.stdout }).on('line', (line) => {
  const next = waiting.shift();
  if (next) next(line);
});
waiting.push(() => {}); // swallow the engine's startup banner

function send(cmd) {
  return new Promise((resolve) => {
    waiting.push(resolve);
    engine.stdin.write(cmd.replace(/[\r\n]+/g, ' ').slice(0, 400) + '\n');
  });
}

const TYPES = { '.html': 'text/html', '.css': 'text/css', '.js': 'text/javascript', '.svg': 'image/svg+xml' };

http.createServer((req, res) => {
  const url = new URL(req.url, 'http://localhost');

  if (url.pathname === '/api/cmd' && req.method === 'POST') {
    let body = '';
    req.on('data', (c) => { body += c; if (body.length > 4096) req.destroy(); });
    req.on('end', async () => {
      let cmd = '';
      try { cmd = String(JSON.parse(body).cmd || ''); } catch (e) { /* ignore */ }
      const out = await send(cmd);
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(out);
    });
    return;
  }

  // static files, guarded against path traversal
  const rel = url.pathname === '/' ? 'index.html' : decodeURIComponent(url.pathname);
  const file = path.normalize(path.join(WEB, rel));
  if (!file.startsWith(WEB + path.sep) || !fs.existsSync(file) || !fs.statSync(file).isFile()) {
    res.writeHead(404); res.end('Not found'); return;
  }
  res.writeHead(200, { 'Content-Type': TYPES[path.extname(file)] || 'application/octet-stream' });
  fs.createReadStream(file).pipe(res);
}).listen(PORT, () => console.log('VibeStream running at http://localhost:' + PORT));

process.on('SIGINT', () => { engine.stdin.end(); setTimeout(() => process.exit(0), 100); });
