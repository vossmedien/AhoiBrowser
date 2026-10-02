// Observe one owned fixture target, then navigate it explicitly. No HID input.
// usage: node navigation-probe.mjs <port> <target-id> <url> <output.json>
import {writeFileSync} from 'node:fs';
const [port, targetId, url, output, diagnosticMode] = process.argv.slice(2);
const diagnosePending = diagnosticMode === '--diagnose-pending';
const targets = await (await fetch(`http://127.0.0.1:${port}/json`)).json();
const target = targets.find(t => t.id === targetId);
if (!target) throw new Error('owned target missing');
const evidence = {target, events: [], snapshots: []};
const socket = new WebSocket(target.webSocketDebuggerUrl);
let nextId = 0;
const pending = new Map();
const save = () => writeFileSync(output, JSON.stringify(evidence, null, 2) + '\n');
const deadline = setTimeout(() => {
  evidence.failure = 'protocol observation deadline';
  save();
  process.exit(4);
}, diagnosePending ? 65000 : 40000);
socket.onmessage = event => {
  const message = JSON.parse(event.data);
  if (message.id) {
    const callback = pending.get(message.id);
    if (callback) { pending.delete(message.id); callback(message); }
  } else if (/^(Network|Page)\./.test(message.method ?? '')) {
    evidence.events.push(message);
    if (diagnosePending) save();
  }
};
function command(method, params = {}, timeoutMs = 7000) {
  return new Promise((resolve, reject) => {
    const id = ++nextId;
    const timer = setTimeout(() => {
      pending.delete(id);
      reject(new Error(`command deadline: ${method}`));
    }, timeoutMs);
    pending.set(id, response => { clearTimeout(timer); resolve(response); });
    socket.send(JSON.stringify({id, method, params}));
  });
}
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
const expression = `JSON.stringify({href:location.href,title:document.title,
  ready:document.readyState,body:document.body?.innerText?.slice(0,2000),
  visibility:document.visibilityState})`;
async function snapshot(phase) {
  const frames = await command('Page.getFrameTree');
  const document = await command('Runtime.evaluate', {expression, returnByValue: true});
  evidence.snapshots.push({phase, frames, document});
  save();
}
await new Promise((resolve, reject) => {
  socket.onopen = resolve;
  socket.onerror = () => reject(new Error('owned target socket failed'));
});
try {
  evidence.networkEnable = await command('Network.enable');
  evidence.pageEnable = await command('Page.enable');
  await snapshot('before-explicit-navigation');
  // Give the pending startup intent its own observation window. Otherwise a
  // subsequent Page.navigate aborts it before its actual outcome is known.
  for (const seconds of [1, 3, 6]) {
    await pause(seconds * 1000);
    await snapshot(`startup-after-${seconds}-second-wait`);
  }
  evidence.navigation = await command('Page.navigate', {url},
      diagnosePending ? 25000 : 7000);
  save();
  for (const seconds of [1, 3, 6]) {
    await pause(seconds * 1000);
    await snapshot(`after-${seconds}-second-wait`);
  }
  evidence.close = await command('Browser.close');
} catch (error) {
  evidence.failure = error.message;
  process.exitCode = 4;
} finally {
  save();
  clearTimeout(deadline);
  socket.close();
}
