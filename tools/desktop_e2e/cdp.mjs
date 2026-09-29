// usage: node cdp.mjs <port> <url-substring|target-id> <method> [params-json]
// Sends one DevTools protocol command to the first page target whose URL
// contains the substring (or whose id matches) and prints the JSON result.
// Node 22's built-in WebSocket; no extra packages.
const [port, match, method, paramsText] = process.argv.slice(2);
const targets = await (await fetch(`http://127.0.0.1:${port}/json`)).json();
const target = targets.find(
    (t) => t.type === 'page' && (t.id === match || t.url.includes(match)));
if (!target) {
  console.log(JSON.stringify({error: `no page target matching ${match}`}));
  process.exit(3);
}
const socket = new WebSocket(target.webSocketDebuggerUrl);
const timer = setTimeout(() => {
  console.log(JSON.stringify({error: 'timeout'}));
  process.exit(4);
}, 8000);
socket.onopen = () => socket.send(JSON.stringify(
    {id: 1, method, params: paramsText ? JSON.parse(paramsText) : {}}));
socket.onmessage = (event) => {
  const message = JSON.parse(event.data);
  if (message.id !== 1) return;
  clearTimeout(timer);
  console.log(JSON.stringify(message.error ? {error: message.error} : message.result));
  socket.close();
  process.exit(message.error ? 5 : 0);
};
