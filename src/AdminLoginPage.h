#pragma once

#include <Arduino.h>

namespace AdminUi
{
static const char LOGIN_PAGE[] PROGMEM = R"LOGINHTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#0b1220">
<meta name="color-scheme" content="light">
<title>Administrator sign in | Evolved.City Room Monitor</title>
<link rel="icon" href="data:,">
<link rel="stylesheet" href="/ui.css?v=7.3.2">
<style>
.login-shell{width:min(470px,calc(100% - 28px));margin:0 auto;padding:64px 0 44px}.login-card{overflow:hidden}.login-card .admin-card-body{padding:28px}.login-icon{display:grid;width:46px;height:46px;place-items:center;margin-bottom:18px;border-radius:14px;background:var(--primary-soft);color:var(--primary-dark);font-size:1.1rem;font-weight:900}.login-card h1{margin:0;color:var(--text);font-size:1.55rem;letter-spacing:-.035em}.login-card>div>p{margin:8px 0 0;color:var(--muted);font-size:.82rem;line-height:1.55}.login-submit{width:100%;margin-top:22px}.login-meta{display:flex;justify-content:space-between;gap:12px;margin-top:18px;color:var(--muted);font-size:.69rem}.login-meta a{color:var(--primary-dark);font-weight:750;text-decoration:none}.login-meta a:hover{text-decoration:underline}.password-row{display:flex;gap:8px}.password-row .control{flex:1;min-width:0}
@media(max-width:520px){.login-shell{padding-top:36px}.login-card .admin-card-body{padding:22px}.password-row{flex-direction:column}}
</style>
</head>
<body class="app-page">
<header class="topbar"><div class="topbar-inner"><a class="brand" href="/" aria-label="Evolved.City Room Monitor home"><span class="brand-mark" aria-hidden="true">E</span><span class="brand-copy"><strong>Evolved.City</strong><small>Room Monitor</small></span></a><div class="topbar-actions"><span class="status-pill"><span class="status-dot locked" aria-hidden="true"></span>Administrator access</span></div></div></header>
<main class="login-shell">
<section class="admin-card login-card"><div class="admin-card-body">
<div class="login-icon" aria-hidden="true">↳</div>
<h1>Administrator sign in</h1>
<p>Sign in to manage Wi-Fi, device configuration, system updates, and administrator security settings. Use the administrator credentials configured when the firmware was built. Change the password after commissioning.</p>
<form id="login-form">
<div class="field"><label for="username">Username</label><input class="control" id="username" name="username" type="text" maxlength="32" autocomplete="username" value="admin" required></div>
<div class="field"><label for="password">Password</label><div class="password-row"><input class="control" id="password" name="password" type="password" maxlength="64" autocomplete="current-password" required autofocus><button class="button-secondary" id="show-password" type="button">Show</button></div></div>
<div id="notice" class="notice" role="status" aria-live="polite"></div>
<button class="button-primary login-submit" id="login-button" type="submit">Sign in</button>
</form>
<div class="login-meta"><span>Session lasts up to 8 hours.</span><a href="/">Back to dashboard</a></div>
</div></section>
<footer class="page-footer"><span>Evolved.City Room Monitoring</span><span class="footer-separator">•</span><span>Secure administration</span></footer>
</main>
<script>
(function(){
'use strict';
function el(id){return document.getElementById(id)}
function destination(){var p=new URLSearchParams(window.location.search).get('next')||'/';return p.charAt(0)==='/'&&p.slice(0,2)!=='//' ? p : '/'}
function notice(type,text){var n=el('notice');n.className='notice show '+type;n.textContent=text}
function formBody(){return 'username='+encodeURIComponent(el('username').value.trim())+'&password='+encodeURIComponent(el('password').value)}
el('show-password').addEventListener('click',function(){var p=el('password'),show=p.type==='password';p.type=show?'text':'password';this.textContent=show?'Hide':'Show'})
el('login-form').addEventListener('submit',function(event){event.preventDefault();var button=el('login-button');button.disabled=true;button.textContent='Signing in…';fetch('/api/admin/login',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded;charset=UTF-8'},body:formBody(),cache:'no-store'}).then(function(response){return response.json().then(function(data){return{ok:response.ok,data:data}})}).then(function(result){if(!result.ok||!result.data.ok)throw new Error(result.data.message||'Sign in failed.');notice('success','Signed in. Opening administrator page…');setTimeout(function(){window.location.replace(destination())},300)}).catch(function(error){notice('error',error.message||'Sign in failed.');button.disabled=false;button.textContent='Sign in'})})
fetch('/api/admin/session?ts='+Date.now(),{cache:'no-store'}).then(function(r){return r.json()}).then(function(d){if(d.authenticated)window.location.replace(destination())}).catch(function(){})
})();
</script>
</body>
</html>
)LOGINHTML";
} // namespace AdminUi
