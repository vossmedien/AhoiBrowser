#!/usr/bin/env python3
"""Browser-facing HTML journeys for the local HTTPS E2E fixture."""

from __future__ import annotations

import html
import json
from typing import Mapping


STYLE = """
:root{color-scheme:dark;--bg:#102334;--card:#19364d;--ink:#f4f8fb;--accent:#56d6c9}
*{box-sizing:border-box}body{margin:0;font:16px/1.5 system-ui;background:var(--bg);color:var(--ink)}
main{width:min(1100px,calc(100% - 32px));margin:24px auto}.card{background:var(--card);padding:20px;
border-radius:16px;margin:12px 0}button,a,input,textarea{font:inherit}button,a.button{display:inline-block;
border:0;border-radius:9px;padding:9px 13px;background:var(--accent);color:#071c25;text-decoration:none;
margin:4px}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:12px}
iframe{width:100%;min-height:310px;border:2px solid #56d6c9;border-radius:12px;background:white}
pre,output{white-space:pre-wrap;word-break:break-word;background:#081823;padding:12px;border-radius:8px}
.drop{border:3px dashed #56d6c9;min-height:120px;padding:20px}.notice{border-left:5px solid #ffd166;padding:10px}
label{display:block;margin:10px 0}input,textarea{width:100%;max-width:620px;padding:8px}
"""


def document(
    title: str,
    body: str,
    urls: Mapping[str, str],
    *,
    extra_head: str = "",
) -> bytes:
    config = (
        json.dumps(dict(urls), sort_keys=True)
        .replace("<", "\\u003c")
        .replace(">", "\\u003e")
        .replace("&", "\\u0026")
    )
    return (
        "<!doctype html><html lang='en'><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        f"<title>{html.escape(title)}</title><style>{STYLE}</style>{extra_head}</head>"
        f"<body><main><h1>{html.escape(title)}</h1>{body}</main>"
        f"<script id='fixture-config' type='application/json'>{config}</script></body></html>"
    ).encode("utf-8")


def index(urls: Mapping[str, str]) -> bytes:
    journeys = (
        ("Download and upload", "/download-upload"),
        ("Three-pane split and DnD", "/split"),
        ("Redirect and popup", "/navigation"),
        ("Synthetic OAuth", "/oauth/authorize?client_id=ahoi-local&state=public-test-state"),
        ("Simulated passkey", "/passkey"),
        ("H.264/AAC, MSE and PiP", "/media"),
        ("WebRTC and capture", "/webrtc"),
        ("Permissions", "/permissions"),
        ("Cookies, CHIPS and privacy", "/privacy"),
        ("Storage, cache and service worker", "/storage"),
        ("Header echo, CSP and CORS", "/developer"),
        ("CSS/LESS/SASS/JavaScript injection", "/injection"),
        ("Synthetic login", "/login"),
    )
    links = "".join(
        f"<a class='button card' href='{html.escape(path)}'>{html.escape(label)}</a>"
        for label, path in journeys
    )
    body = (
        "<p class='notice'>Loopback-only fixture. Credentials and authorization codes are "
        "synthetic. Receipts never retain secrets, cookie values, query values, or uploaded bytes.</p>"
        f"<section class='grid'>{links}</section>"
        "<p><a href='/__fixture/manifest'>Machine-readable manifest</a> · "
        "<a href='/__fixture/receipts'>privacy-safe receipts</a></p>"
    )
    return document("AhoiBrowser local HTTPS E2E fixture", body, urls)


def download_upload(urls: Mapping[str, str]) -> bytes:
    body = """
<section class='card'><h2>Range download</h2>
<a id='download' class='button' download href='/download/deterministic.bin'>Download deterministic payload</a>
<a id='warning-download' class='button' download href='/download/harmless-warning.exe'>Harmless warning file</a>
<p>The manifest publishes size and SHA-256. Pause/resume must produce Range requests and the same hash.</p></section>
<section class='card'><h2>Upload and drag/drop</h2>
<form id='upload-form'><input id='upload-file' name='file' type='file' required><button>Upload</button></form>
<div id='upload-drop' class='drop' tabindex='0'>Drop a file here</div><output id='upload-result'></output></section>
<script>
const result=document.querySelector('#upload-result');
async function upload(file){const response=await fetch('/upload',{method:'POST',headers:{'Content-Type':file.type||'application/octet-stream','X-Ahoi-Filename':file.name},body:file});result.textContent=JSON.stringify(await response.json(),null,2)}
document.querySelector('#upload-form').onsubmit=e=>{e.preventDefault();upload(document.querySelector('#upload-file').files[0])};
const drop=document.querySelector('#upload-drop');drop.ondragover=e=>e.preventDefault();drop.ondrop=e=>{e.preventDefault();upload(e.dataTransfer.files[0])};
</script>"""
    return document("Download, pause/resume, upload and DnD", body, urls)


def split(urls: Mapping[str, str]) -> bytes:
    body = """
<p>Each pane owns independent media, dialog, download and permission controls. The URL drop target accepts only http(s).</p>
<div id='url-drop' class='drop'>Drop a safe HTTPS URL here</div><output id='url-result'></output>
<section class='grid' id='three-pane'>
<iframe title='Pane A' src='/pane/a'></iframe><iframe title='Pane B' src='/pane/b'></iframe><iframe title='Pane C' src='/pane/c'></iframe>
</section><script>
const target=document.querySelector('#url-drop');target.ondragover=e=>e.preventDefault();target.ondrop=e=>{e.preventDefault();const raw=e.dataTransfer.getData('text/uri-list')||e.dataTransfer.getData('text/plain');try{const url=new URL(raw);if(!['http:','https:'].includes(url.protocol))throw Error('only safe web URLs');document.querySelector('#url-result').textContent=url.href}catch(error){document.querySelector('#url-result').textContent='Rejected: '+error.message}};
</script>"""
    return document("Three-pane split and drag/drop", body, urls)


def pane(name: str, urls: Mapping[str, str]) -> bytes:
    safe = html.escape(name)
    body = f"""
<section class='card' data-pane='{safe}'><h2>Independent pane {safe}</h2>
<audio controls loop src='/media/sample.mp4'></audio>
<p><button id='dialog'>Dialog</button><a class='button' download href='/download/deterministic.bin'>Download</a>
<button id='permission'>Notification permission</button></p><output id='pane-result'></output></section>
<script>const out=document.querySelector('#pane-result');document.querySelector('#dialog').onclick=()=>out.textContent='dialog-'+prompt('Synthetic pane value','{safe}');document.querySelector('#permission').onclick=async()=>out.textContent=await Notification.requestPermission()</script>"""
    return document("Pane %s" % name.upper(), body, urls)


def navigation(urls: Mapping[str, str]) -> bytes:
    third = html.escape(urls["thirdPartyHttpsUrl"])
    body = f"""
<section class='card'><a id='same-redirect' class='button' href='/redirect/same?utm_source=fixture'>Same-origin redirect</a>
<a id='cross-redirect' class='button' href='/redirect/cross?gclid=synthetic'>Cross-origin redirect</a>
<button id='open-popup'>Requested popup</button><a class='button' target='_blank' rel='noopener' href='{third}/popup'>noopener popup</a></section>
<script>document.querySelector('#open-popup').onclick=()=>window.open('/popup','ahoi-fixture-popup','popup,width=520,height=480')</script>"""
    return document("Redirect and popup controls", body, urls)


def popup(urls: Mapping[str, str]) -> bytes:
    return document(
        "Synthetic popup",
        "<p id='popup-ready'>Popup ready. This page does not request credentials.</p><button onclick='close()'>Close</button>",
        urls,
    )


def oauth_authorize(urls: Mapping[str, str], state: str) -> bytes:
    safe_state = html.escape(state)
    body = f"""
<p class='notice'>Local OAuth simulation only. It does not contact an identity provider and does not validate a real OAuth integration.</p>
<form method='post' action='/oauth/approve'><input type='hidden' name='state' value='{safe_state}'>
<p>Client: <code>ahoi-local</code>; identity: <code>synthetic-user@example.invalid</code></p>
<button name='decision' value='allow'>Allow synthetic login</button><button name='decision' value='deny'>Deny</button></form>"""
    return document("Synthetic OAuth authorization", body, urls)


def oauth_callback(urls: Mapping[str, str], decision: str) -> bytes:
    body = (
        "<p id='oauth-result'>%s</p><p>No token, password, or real account was used.</p>"
        % html.escape(decision)
    )
    return document("Synthetic OAuth callback", body, urls)


def passkey(urls: Mapping[str, str]) -> bytes:
    body = """
<p class='notice'>This is a deterministic local simulation of passkey UI and challenge plumbing. It never calls navigator.credentials and is not a platform WebAuthn ceremony. Real Touch ID/passkey acceptance remains ASSISTED_E2E.</p>
<button id='register'>Simulate registration</button><button id='authenticate'>Simulate authentication</button><output id='passkey-result'></output>
<script>
async function ceremony(kind){const challenge=await (await fetch('/passkey/challenge?kind='+kind)).json();const response=await fetch('/passkey/verify',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({kind,challengeId:challenge.challengeId,credentialId:'synthetic-local-credential'})});document.querySelector('#passkey-result').textContent=JSON.stringify(await response.json(),null,2)}
document.querySelector('#register').onclick=()=>ceremony('register');document.querySelector('#authenticate').onclick=()=>ceremony('authenticate');
</script>"""
    return document("Locally simulated passkey flow", body, urls)


def media(urls: Mapping[str, str]) -> bytes:
    body = """
<p>The deterministic MP4 contains H.264 video and AAC audio. Playback capability still depends on the tested build's legal codec configuration.</p>
<video id='video' controls loop playsinline width='480' src='/media/sample.mp4'></video>
<p><button id='pip'>Picture in Picture</button><button id='mse'>MSE append</button></p><output id='media-result'></output>
<script>
const video=document.querySelector('#video'),out=document.querySelector('#media-result');
document.querySelector('#pip').onclick=async()=>{try{await video.requestPictureInPicture();out.textContent='PiP entered'}catch(e){out.textContent=e.name}};
document.querySelector('#mse').onclick=async()=>{if(!window.MediaSource){out.textContent='MSE unavailable';return}const type='video/mp4; codecs="avc1.42c00c, mp4a.40.2"';if(!MediaSource.isTypeSupported(type)){out.textContent='MSE codec unsupported';return}const source=new MediaSource();video.src=URL.createObjectURL(source);source.addEventListener('sourceopen',async()=>{const buffer=source.addSourceBuffer(type);const bytes=await (await fetch('/media/sample.mp4')).arrayBuffer();buffer.addEventListener('updateend',()=>{source.endOfStream();out.textContent='MSE appended'});buffer.appendBuffer(bytes)},{once:true})};
</script>"""
    return document("H.264/AAC, MSE and PiP", body, urls)


def webrtc(urls: Mapping[str, str]) -> bytes:
    body = """
<p class='notice'>Loopback-only WebRTC control. No ICE server is configured, so no packet leaves the machine.</p>
<video id='preview' autoplay muted playsinline width='420'></video><p>
<button id='camera'>Camera + microphone</button><button id='screen'>Screen capture</button><button id='peer'>Local peer connection</button><button id='stop'>Stop tracks</button></p><output id='rtc-result'></output>
<script>
let streams=[];const out=document.querySelector('#rtc-result'),preview=document.querySelector('#preview');
async function capture(kind){try{const stream=kind==='screen'?await navigator.mediaDevices.getDisplayMedia({video:true,audio:true}):await navigator.mediaDevices.getUserMedia({video:true,audio:true});streams.push(stream);preview.srcObject=stream;out.textContent=kind+' active'}catch(e){out.textContent=e.name}}
document.querySelector('#camera').onclick=()=>capture('camera');document.querySelector('#screen').onclick=()=>capture('screen');
document.querySelector('#peer').onclick=async()=>{const a=new RTCPeerConnection({iceServers:[]}),b=new RTCPeerConnection({iceServers:[]});a.onicecandidate=e=>e.candidate&&b.addIceCandidate(e.candidate);b.onicecandidate=e=>e.candidate&&a.addIceCandidate(e.candidate);a.createDataChannel('ahoi');await a.setLocalDescription(await a.createOffer());await b.setRemoteDescription(a.localDescription);await b.setLocalDescription(await b.createAnswer());await a.setRemoteDescription(b.localDescription);out.textContent='local peer connected'};
document.querySelector('#stop').onclick=()=>{streams.flatMap(s=>s.getTracks()).forEach(t=>t.stop());streams=[];preview.srcObject=null;out.textContent='tracks stopped'};
</script>"""
    return document("WebRTC, camera, microphone and screen capture", body, urls)


def permissions(urls: Mapping[str, str]) -> bytes:
    body = """
<button data-kind='location'>Location</button><button data-kind='notifications'>Notifications</button><button data-kind='clipboard-read'>Clipboard read</button><button data-kind='clipboard-write'>Clipboard write</button><output id='permission-result'></output>
<script>
const out=document.querySelector('#permission-result');document.querySelectorAll('button').forEach(button=>button.onclick=async()=>{try{const kind=button.dataset.kind;if(kind==='location')navigator.geolocation.getCurrentPosition(p=>out.textContent=`location ${p.coords.latitude},${p.coords.longitude}`,e=>out.textContent=e.message);else if(kind==='notifications')out.textContent=await Notification.requestPermission();else if(kind==='clipboard-read')out.textContent=await navigator.clipboard.readText();else{await navigator.clipboard.writeText('synthetic-ahoi-clipboard-value');out.textContent='synthetic value written'}}catch(e){out.textContent=e.name}})
</script>"""
    return document("Location, notifications and clipboard permissions", body, urls)


def privacy(urls: Mapping[str, str]) -> bytes:
    third = html.escape(urls["thirdPartyHttpsUrl"])
    body = f"""
<section class='card'><a class='button' href='/cookies/set'>Set first-party cookies</a><a class='button' href='/privacy/echo?utm_source=fixture&gclid=synthetic'>Echo GPC/referrer/tracking shape</a></section>
<iframe id='third-party-cookie' title='Third-party CHIPS control' src='{third}/cookies/third-party'></iframe>
<p>Receipts record only whether Cookie, Sec-GPC and Referer exist plus tracking parameter names. Values are never retained.</p>"""
    return document("Cookies, CHIPS, GPC, referrer and tracking", body, urls)


def storage(urls: Mapping[str, str]) -> bytes:
    body = """
<button id='exercise'>Exercise all storage APIs</button><button id='clear'>Clear fixture storage</button><output id='storage-result'></output>
<script>
const out=document.querySelector('#storage-result');
document.querySelector('#exercise').onclick=async()=>{localStorage.setItem('ahoi-local','v1');sessionStorage.setItem('ahoi-session','v1');const open=indexedDB.open('ahoi-e2e',1);await new Promise((ok,bad)=>{open.onupgradeneeded=()=>open.result.createObjectStore('values');open.onsuccess=ok;open.onerror=bad});const tx=open.result.transaction('values','readwrite');tx.objectStore('values').put('v1','key');await new Promise((ok,bad)=>{tx.oncomplete=ok;tx.onerror=bad});const cache=await caches.open('ahoi-e2e-v1');await cache.add('/assets/v1/data.json');const registration=await navigator.serviceWorker.register('/service-worker.js');const count=await (await fetch('/counter/storage')).json();out.textContent=JSON.stringify({local:localStorage.getItem('ahoi-local'),session:sessionStorage.getItem('ahoi-session'),serviceWorker:registration.scope,count},null,2)};
document.querySelector('#clear').onclick=async()=>{localStorage.clear();sessionStorage.clear();indexedDB.deleteDatabase('ahoi-e2e');for(const key of await caches.keys())await caches.delete(key);for(const r of await navigator.serviceWorker.getRegistrations())await r.unregister();out.textContent='cleared'};
</script>"""
    return document("Local/session storage, IndexedDB, cache and service worker", body, urls)


def developer(urls: Mapping[str, str]) -> bytes:
    third = html.escape(urls["thirdPartyHttpsUrl"])
    body = """
<button id='headers'>Header echo</button><button id='cors-allow'>Allowed CORS</button><button id='cors-deny'>Denied CORS</button>
<a class='button' href='/csp/strict'>Strict CSP page</a><output id='developer-result'></output>
<script>
const out=document.querySelector('#developer-result');async function request(url){try{const response=await fetch(url,{headers:{'X-Ahoi-Test':'public-fixture-value'}});out.textContent=JSON.stringify(await response.json(),null,2)}catch(e){out.textContent=e.name}}
document.querySelector('#headers').onclick=()=>request('/headers/echo');document.querySelector('#cors-allow').onclick=()=>request('__THIRD__/cors/allow');document.querySelector('#cors-deny').onclick=()=>request('__THIRD__/cors/deny');
</script>""".replace("__THIRD__", third)
    return document("Header echo, CSP and CORS", body, urls)


def injection(urls: Mapping[str, str]) -> bytes:
    body = """
<p class='notice'>Developer-toolkit fixture: LESS/SASS inputs are text controls; Ahoi's toolkit performs compilation. This page itself never executes LESS or SASS.</p>
<label>CSS<textarea id='css'>#injection-target { color: rgb(86, 214, 201); }</textarea></label>
<label>LESS<textarea id='less'>@accent: #56d6c9; #injection-target { color: @accent; }</textarea></label>
<label>SASS<textarea id='sass'>$accent: #56d6c9; #injection-target { color: $accent; }</textarea></label>
<label>JavaScript<textarea id='javascript'>document.querySelector('#injection-target').dataset.injected = 'yes';</textarea></label>
<button id='apply-css'>Apply CSS control</button><button id='apply-js'>Apply JavaScript control</button>
<p id='injection-target'>Injection target</p><output id='injection-result'></output>
<script>document.querySelector('#apply-css').onclick=()=>{const node=document.createElement('style');node.textContent=document.querySelector('#css').value;document.head.append(node);document.querySelector('#injection-result').textContent='CSS applied'};document.querySelector('#apply-js').onclick=()=>{Function(document.querySelector('#javascript').value)();document.querySelector('#injection-result').textContent='JavaScript applied'}</script>"""
    return document("CSS, LESS, SASS and JavaScript injection controls", body, urls)


def login(urls: Mapping[str, str], result: str = "") -> bytes:
    result_markup = f"<output id='login-result'>{html.escape(result)}</output>" if result else ""
    body = f"""
<p class='notice'>Use only repository-public synthetic credentials: <code>fixture-user</code> / <code>fixture-password</code>. Never enter a real secret.</p>
<form method='post' action='/login'><label>Username<input name='username' autocomplete='username' value='fixture-user'></label>
<label>Password<input name='password' autocomplete='current-password' type='password'></label><button>Sign in locally</button></form>{result_markup}"""
    return document("Synthetic login form", body, urls)


def strict_csp(urls: Mapping[str, str]) -> bytes:
    del urls
    return (
        "<!doctype html><html lang='en'><head><meta charset='utf-8'>"
        "<title>Strict CSP control</title></head><body><main>"
        "<h1>Strict CSP control</h1><p id='csp-inline-control'>This inline-free "
        "document is delivered with default-src self and object-src none.</p>"
        "</main></body></html>"
    ).encode("utf-8")
