const http = require('http');
const fs = require('fs');
const path = require('path');

const archive = process.argv[2];
const server = http.createServer((request, response) => {
  if (request.url !== '/onnxruntime.zip' && request.url !== '/runtime.nupkg') {
    response.writeHead(404);
    response.end();
    return;
  }
  const size = fs.statSync(archive).size;
  response.writeHead(200, {'Content-Type': 'application/zip', 'Content-Length': size});
  fs.createReadStream(archive).pipe(response);
});
server.listen(18081, '127.0.0.1');
