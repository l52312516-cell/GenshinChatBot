const http = require('http');
const fs = require('fs');

const files = {
  '/runtime.zip': process.argv[2],
  '/runtime.nupkg': process.argv[3],
  '/directml.nupkg': process.argv[4],
  '/det.onnx': process.argv[5],
  '/rec.onnx': process.argv[6],
  '/dict.txt': process.argv[7],
};

const server = http.createServer((request, response) => {
  const file = files[request.url];
  if (!file || !fs.existsSync(file)) {
    response.writeHead(404);
    response.end();
    return;
  }
  response.writeHead(200, { 'Content-Length': fs.statSync(file).size });
  fs.createReadStream(file).pipe(response);
});

server.listen(18081, '127.0.0.1');
