const http = require('http');
const fs = require('fs');

const source = process.argv[2];
let interrupted = false;

function sendFile(request, response, slow) {
  const size = fs.statSync(source).size;
  const match = /^bytes=(\d+)-$/.exec(request.headers.range || '');
  const start = match ? Number(match[1]) : 0;
  if (start >= size) {
    response.writeHead(416, { 'Content-Range': `bytes */${size}` });
    response.end();
    return;
  }
  response.writeHead(start ? 206 : 200, {
    'Accept-Ranges': 'bytes',
    'Content-Length': size - start,
    'Content-Range': `bytes ${start}-${size - 1}/${size}`,
  });
  const stream = fs.createReadStream(source, { start, highWaterMark: 64 * 1024 });
  if (!slow) {
    stream.pipe(response);
    return;
  }
  stream.on('data', chunk => {
    stream.pause();
    response.write(chunk, () => setTimeout(() => stream.resume(), 8));
  });
  stream.on('end', () => response.end());
}

const server = http.createServer((request, response) => {
  const url = new URL(request.url, 'http://127.0.0.1');
  if (url.searchParams.get('token') !== 'query-ok') {
    response.writeHead(403);
    response.end('missing query token');
    return;
  }
  if (url.pathname === '/flaky-runtime.zip' && !interrupted) {
    interrupted = true;
    const size = fs.statSync(source).size;
    response.writeHead(200, { 'Content-Length': size, 'Accept-Ranges': 'bytes' });
    const stream = fs.createReadStream(source, { end: Math.min(size - 1, 1024 * 1024 - 1) });
    stream.on('end', () => response.destroy());
    stream.pipe(response, { end: false });
    return;
  }
  if (url.pathname === '/flaky-runtime.zip') {
    sendFile(request, response, false);
    return;
  }
  if (url.pathname === '/slow-runtime.zip') {
    sendFile(request, response, true);
    return;
  }
  response.writeHead(404);
  response.end();
});

server.listen(18082, '127.0.0.1');
