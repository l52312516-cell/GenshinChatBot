const http = require('http');
const server = http.createServer((request, response) => {
  if (!new URL(request.url, 'http://127.0.0.1').searchParams.has('token')) {
    response.writeHead(400);
    response.end();
    return;
  }
  let body = '';
  request.on('data', chunk => { body += chunk; });
  request.on('end', () => {
    process.stdout.write(body.includes('"messages"') ? 'REQUEST_RECEIVED\n' : 'BODY_RECEIVED\n');
    response.writeHead(200, {'Content-Type': 'application/json'});
    response.end(JSON.stringify({choices: [{message: {content: [{type: 'text', text: '**连接成功** /ignore'}]}}]}));
  });
});
server.listen(18080, '127.0.0.1');
