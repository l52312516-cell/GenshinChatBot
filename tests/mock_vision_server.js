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
    const match = /"url":"data:image\/jpeg;base64,([A-Za-z0-9+/=]+)"/.exec(body);
    process.stdout.write(match && match[1].length > 100 ? 'JPEG_PAYLOAD\n' : 'BAD_PAYLOAD\n');
    response.writeHead(200, {'Content-Type': 'application/json'});
    response.end(JSON.stringify({choices: [{message: {content: [{type: 'text', text: '-2026年8月30日23:09-\n视觉测试成功\n\n'}]}}]}));
  });
});
server.listen(18080, '127.0.0.1');
