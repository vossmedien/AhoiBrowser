// usage: node cdp_set_file.mjs <port> <url-substring> <css selector> <file path>
// Sets a file input's files in one DevTools session (node ids are scoped to
// the session, so cdp.mjs's one-command sessions cannot chain them).
const [port, match, selector, file] = process.argv.slice(2);
const targets = await (await fetch(`http://127.0.0.1:${port}/json`)).json();
const target = targets.find((t) => t.type === 'page' && t.url.includes(match));
if (!target) { console.log(JSON.stringify({error: `no page ${match}`})); process.exit(3); }
const socket = new WebSocket(target.webSocketDebuggerUrl);
let id = 0; const pending = new Map();
const send = (method, params = {}) => new Promise((resolve) => {
  pending.set(++id, resolve); socket.send(JSON.stringify({id, method, params}));
});
socket.onmessage = (event) => {
  const message = JSON.parse(event.data);
  if (pending.has(message.id)) { pending.get(message.id)(message); pending.delete(message.id); }
};
setTimeout(() => { console.log(JSON.stringify({error: 'timeout'})); process.exit(4); }, 8000);
socket.onopen = async () => {
  const doc = await send('DOM.getDocument', {depth: 0});
  const node = await send('DOM.querySelector', {nodeId: doc.result.root.nodeId, selector});
  const set = await send('DOM.setFileInputFiles', {files: [file], nodeId: node.result.nodeId});
  console.log(JSON.stringify(set.error ? {error: set.error} : {ok: true, nodeId: node.result.nodeId}));
  socket.close(); process.exit(set.error ? 5 : 0);
};
