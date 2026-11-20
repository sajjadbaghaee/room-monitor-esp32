#pragma once

#include <Arduino.h>

namespace WifiUi
{
static const char WIFI_PAGE[] PROGMEM = R"WIFIHTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#0b1220">
<meta name="color-scheme" content="light">
<title>Wi-Fi | Evolved.City Room Monitor</title>
<link rel="icon" href="data:,">
<link rel="stylesheet" href="/ui.css?v=7.3.2">
<style>
.wifi-layout{max-width:760px}.status-strip{grid-template-columns:repeat(3,minmax(0,1fr));margin-top:0}
.wifi-card .admin-card-body{padding:24px}.wifi-card h2{font-size:1rem}.wifi-card>div>p{font-size:.78rem}
.row{display:flex;gap:8px}.row select,.row input{flex:1;min-width:0}.password-wrap{display:flex;gap:8px}.password-wrap input{flex:1}.manual{display:none;margin-top:9px}.manual.show{display:block}.scan-note{margin-top:10px;color:var(--muted);font-size:.72rem}.scan-note strong{color:var(--text)}details{margin-top:18px;padding-top:4px}summary{cursor:pointer;color:var(--primary-dark);font-size:.78rem;font-weight:800}.save{width:100%;margin-top:22px}
.provision-note{display:none}.provision-note.show{display:flex}
@media(max-width:620px){.status-strip{grid-template-columns:1fr}.row,.password-wrap{flex-direction:column}}
</style>
</head>
<body class="app-page">
<header class="topbar"><div class="topbar-inner"><a class="brand" href="/" aria-label="Evolved.City Room Monitor home"><span class="brand-mark" aria-hidden="true">E</span><span class="brand-copy"><strong>Evolved.City</strong><small>Room Monitor</small></span></a><div class="topbar-actions"><span class="status-pill"><span class="status-dot" aria-hidden="true"></span><span id="top-status">Network settings</span></span><button class="topbar-button" id="logout-button" type="button" hidden>Logout</button></div></div></header>
<main class="page-shell">
<section class="page-heading"><div><p class="eyebrow">Network configuration</p><h1>Wi-Fi</h1><p class="page-subtitle">Scan nearby networks, select one, and save the connection settings used by this Room Monitor.</p></div><div class="refresh-card"><span>Network scan</span><strong>Automatic</strong></div></section>
<nav class="page-tabs" aria-label="Room monitor pages"><a class="tab" href="/">Dashboard</a><a class="tab" href="/measurements.html">Measurements</a><a class="tab active" href="/wifi" aria-current="page">Wi-Fi</a><a class="tab" href="/configuration">Configuration</a><a class="tab" href="/diagnostics">Diagnostics</a><a class="tab" href="/system">System</a></nav>
<aside id="provision-note" class="inline-note provision-note" role="note"><span class="inline-note-icon" aria-hidden="true">i</span><div><strong>Factory-reset provisioning mode</strong><p>This Wi-Fi page is intentionally available without administrator authentication while the device has no saved SSID. After Wi-Fi is configured and the device restarts, administrator authentication is required.</p></div></aside>
<div class="wifi-layout">
<div class="info-strip status-strip"><div class="info-item"><span>Mode</span><strong id="mode">Checking…</strong></div><div class="info-item"><span>Connected network</span><strong id="connected-ssid">Checking…</strong></div><div class="info-item"><span>IP address</span><strong id="ip">Checking…</strong></div></div>
<section class="admin-card wifi-card"><div class="admin-card-body"><h2>Connect Room Monitor to Wi-Fi</h2><p>Select a detected network. Use Refresh scan whenever you want to update the list.</p><form id="wifi-form">
<div class="field"><label for="ssid-select">Wi-Fi network</label><div class="row"><select class="control" id="ssid-select" required><option value="">Scanning nearby networks…</option></select><button class="button-secondary" id="scan-button" type="button">Refresh scan</button></div><div id="manual-wrap" class="manual"><input class="control" id="manual-ssid" maxlength="32" autocomplete="off" placeholder="Enter hidden network SSID"></div><span class="field-help">Networks are ordered by signal strength. Hidden networks can be entered manually.</span><div class="scan-note" id="scan-note">Preparing Wi-Fi scan…</div></div>
<div class="field"><label for="password">Wi-Fi password</label><div class="password-wrap"><input class="control" id="password" type="password" maxlength="64" autocomplete="new-password" placeholder="Enter network password"><button class="button-secondary" id="show-password" type="button">Show</button></div><span class="field-help">If you keep the same saved SSID, leaving this blank keeps its existing password. For a different SSID, blank means an open network.</span></div>
<details><summary>Advanced settings</summary><div class="field"><label for="ip-field">Stored IP value <span style="font-weight:500;color:var(--muted)">(optional)</span></label><input class="control" id="ip-field" inputmode="decimal" autocomplete="off" placeholder="Example: 192.168.1.50"><span class="field-help">Kept for compatibility with the existing configuration. DHCP normally determines the active address after connection.</span></div></details>
<div id="notice" class="notice" role="status" aria-live="polite"></div><button class="button-primary save" id="save-button" type="submit">Save Wi-Fi &amp; restart</button>
</form></div></section>
</div>
<footer class="page-footer"><span>Evolved.City Room Monitoring</span><span class="footer-separator">•</span><span>Network manager</span></footer>
</main>
<script>
(function(){
var savedSsid='', scanTimer=null;
function el(id){return document.getElementById(id)}
function setNotice(type,msg){var n=el('notice');n.className='notice show '+type;n.textContent=msg}
function clearNotice(){var n=el('notice');n.className='notice';n.textContent=''}
function json(url,opts){opts=Object.assign({cache:'no-store'},opts||{});if(opts.method&&opts.method!=='GET'){opts.headers=Object.assign({},opts.headers||{},{'X-RoomMonitor-Request':'1'})}return fetch(url,opts).then(function(r){if(r.status===401){window.location.replace('/login?next=/wifi');throw new Error('Authentication required')}if(!r.ok)throw new Error('HTTP '+r.status);return r.json()})}
function loadStatus(){json('/api/wifi/status?ts='+Date.now()).then(function(d){savedSsid=d.savedSsid||'';el('mode').textContent=d.mode||'—';el('connected-ssid').textContent=d.connectedSsid||'Not connected';el('ip').textContent=d.ip||'—';if(d.persistedIp&&d.persistedIp!=='NaV')el('ip-field').value=d.persistedIp}).catch(function(){el('mode').textContent='Unavailable';el('connected-ssid').textContent='Unavailable';el('ip').textContent='Unavailable'})}
function signalText(rssi){if(rssi>=-55)return'Excellent';if(rssi>=-67)return'Good';if(rssi>=-75)return'Fair';return'Weak'}
function renderNetworks(list){var sel=el('ssid-select'), previous=sel.value;sel.innerHTML='';var first=document.createElement('option');first.value='';first.textContent=list.length?'Select a Wi-Fi network':'No networks found';sel.appendChild(first);list.forEach(function(n){var o=document.createElement('option');o.value=n.ssid;o.textContent=n.ssid+' · '+signalText(n.rssi)+' ('+n.rssi+' dBm) · '+(n.secure?'Secured':'Open');if(n.ssid===savedSsid)o.textContent+=' · Saved';sel.appendChild(o)});var other=document.createElement('option');other.value='__manual__';other.textContent='Other / hidden network…';sel.appendChild(other);if(previous&&Array.prototype.some.call(sel.options,function(o){return o.value===previous}))sel.value=previous;else if(savedSsid&&Array.prototype.some.call(sel.options,function(o){return o.value===savedSsid}))sel.value=savedSsid;updateManual()}
function pollScan(){json('/api/wifi/networks?ts='+Date.now()).then(function(d){if(d.scanning){el('scan-note').innerHTML='<strong>Scanning…</strong> Keep this page open.';scanTimer=setTimeout(pollScan,700);return}el('scan-button').disabled=false;if(d.error){el('scan-note').textContent=d.error;return}renderNetworks(d.networks||[]);el('scan-note').innerHTML='<strong>'+(d.networks||[]).length+'</strong> network'+((d.networks||[]).length===1?'':'s')+' found.'}).catch(function(){el('scan-button').disabled=false;el('scan-note').textContent='Wi-Fi scan status could not be read.'})}
function startScan(){clearTimeout(scanTimer);el('scan-button').disabled=true;el('scan-note').innerHTML='<strong>Scanning…</strong> Looking for nearby networks.';json('/api/wifi/scan',{method:'POST'}).then(function(){setTimeout(pollScan,350)}).catch(function(){el('scan-button').disabled=false;el('scan-note').textContent='Could not start a Wi-Fi scan.'})}
function updateManual(){var manual=el('ssid-select').value==='__manual__';el('manual-wrap').className=manual?'manual show':'manual';el('manual-ssid').required=manual}
el('ssid-select').addEventListener('change',updateManual);el('scan-button').addEventListener('click',startScan);el('show-password').addEventListener('click',function(){var p=el('password'),show=p.type==='password';p.type=show?'text':'password';this.textContent=show?'Hide':'Show'});
el('wifi-form').addEventListener('submit',function(e){e.preventDefault();clearNotice();var ssid=el('ssid-select').value==='__manual__'?el('manual-ssid').value.trim():el('ssid-select').value;if(!ssid){setNotice('error','Select a Wi-Fi network or enter a hidden SSID.');return}var b=el('save-button');b.disabled=true;b.textContent='Saving…';var body='ssid='+encodeURIComponent(ssid)+'&pass='+encodeURIComponent(el('password').value)+'&ip='+encodeURIComponent(el('ip-field').value.trim());fetch('/wifi/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded;charset=UTF-8','X-RoomMonitor-Request':'1'},body:body,cache:'no-store'}).then(function(r){if(r.status===401){window.location.replace('/login?next=/wifi');throw new Error('Authentication required')}return r.json().then(function(d){return{ok:r.ok,data:d}})}).then(function(x){if(!x.ok||!x.data.ok)throw new Error(x.data.message||'Could not save Wi-Fi settings.');setNotice('success',x.data.message||'Wi-Fi settings saved. Device is restarting…');b.textContent='Saved — restarting';setTimeout(function(){loadStatus()},5000)}).catch(function(err){setNotice('error',err.message||'Could not save Wi-Fi settings.');b.disabled=false;b.textContent='Save Wi-Fi & restart'})});
el('logout-button').addEventListener('click',function(){fetch('/api/admin/logout',{method:'POST',headers:{'X-RoomMonitor-Request':'1'},cache:'no-store'}).finally(function(){window.location.replace('/')})});fetch('/api/admin/session?ts='+Date.now(),{cache:'no-store'}).then(function(r){return r.json()}).then(function(d){el('logout-button').hidden=!d.authenticated}).catch(function(){});loadStatus();startScan();
})();
</script>
<script>
(function(){
  var oldLoadStatus=window.__unused;
  fetch('/api/wifi/status?ui='+Date.now(),{cache:'no-store'}).then(function(r){return r.json()}).then(function(d){if(d.apMode){var n=document.getElementById('provision-note');if(n)n.className='inline-note provision-note show';var s=document.getElementById('top-status');if(s)s.textContent='Provisioning mode'}}).catch(function(){});
})();
</script>
</body>
</html>
)WIFIHTML";
} // namespace WifiUi
