#pragma once

#include <Arduino.h>

namespace SharedUi
{
// Shared visual system for every Room Monitor page. This stylesheet lives in
// firmware so /system, /wifi, and the recovery page keep the same appearance
// even when SPIFFS is unavailable.
static const char COMMON_CSS[] PROGMEM = R"CSS(
:root{
  --bg:#f4f7fb;--surface:#fff;--surface-soft:#f8fafc;--text:#172033;
  --muted:#657087;--border:#e2e8f0;--border-strong:#cbd5e1;
  --primary:#1769e0;--primary-dark:#0d4eb5;--primary-soft:#eaf2ff;
  --success:#159a68;--success-soft:#e9f8f1;--danger:#b83b48;
  --danger-soft:#fff0f1;--warning:#79551a;--warning-soft:#fff8e8;
  --shadow:0 16px 40px rgba(15,23,42,.08);
  --shadow-small:0 6px 18px rgba(15,23,42,.06);
  --radius-xl:22px;--radius-lg:16px;--radius-md:12px;
  --temperature:#e35d6a;--humidity:#2e7bdc;--co2:#7b61d1;--air:#17a37a;
  --voc:#b36c21;--pressure:#597189;--sound:#c04c8c;--light:#b78915
}
*{box-sizing:border-box}
[hidden]{display:none!important}
html{min-height:100%;font-family:Inter,ui-sans-serif,-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;color:var(--text);background:var(--bg);-webkit-text-size-adjust:100%}
body{min-height:100vh;margin:0;background:radial-gradient(circle at top right,rgba(23,105,224,.08),transparent 28rem),var(--bg)}
button,input,select{font:inherit}a{color:inherit}
.brand{display:inline-flex;align-items:center;gap:11px;color:#fff;text-decoration:none}
.brand-mark{display:inline-grid;width:38px;height:38px;place-items:center;flex:0 0 auto;border-radius:11px;background:linear-gradient(145deg,#2c82ff,#11a88a);color:#fff;font-size:1.05rem;font-weight:800;box-shadow:0 8px 18px rgba(22,105,224,.22)}
.brand-copy{display:flex;min-width:0;flex-direction:column;line-height:1.15}
.brand-copy strong{font-size:.98rem;font-weight:750;letter-spacing:-.01em}.brand-copy small{margin-top:3px;color:rgba(255,255,255,.66);font-size:.72rem;font-weight:500}
.topbar{position:sticky;top:0;z-index:10;background:rgba(11,18,32,.96);border-bottom:1px solid rgba(255,255,255,.06)}
.topbar-inner{width:min(1180px,calc(100% - 32px));min-height:70px;margin:0 auto;display:flex;align-items:center;justify-content:space-between;gap:20px}
.topbar-actions{display:flex;align-items:center;gap:10px}.status-pill{display:inline-flex;align-items:center;gap:8px;min-height:34px;padding:0 12px;border:1px solid rgba(255,255,255,.12);border-radius:999px;color:rgba(255,255,255,.84);background:rgba(255,255,255,.05);font-size:.78rem;font-weight:650}
.status-dot{width:8px;height:8px;border-radius:50%;background:#31d690;box-shadow:0 0 0 4px rgba(49,214,144,.13)}
.status-dot.locked{background:#8fb5ff;box-shadow:0 0 0 4px rgba(143,181,255,.13)}
.icon-button{display:grid;width:36px;height:36px;place-items:center;border:1px solid rgba(255,255,255,.12);border-radius:10px;color:#fff;background:rgba(255,255,255,.05);cursor:pointer;font-size:1.18rem;line-height:1;text-decoration:none;transition:background 160ms ease,transform 160ms ease}
.topbar-button{display:inline-grid;min-height:36px;place-items:center;padding:0 12px;border:1px solid rgba(255,255,255,.12);border-radius:10px;color:#fff;background:rgba(255,255,255,.05);cursor:pointer;text-decoration:none;font-size:.75rem;font-weight:750;transition:background 160ms ease,transform 160ms ease}.topbar-button:hover{background:rgba(255,255,255,.1)}.topbar-button:active{transform:scale(.98)}.topbar-button:disabled{opacity:.55;cursor:not-allowed}
.icon-button:hover{background:rgba(255,255,255,.1)}.icon-button:active{transform:scale(.96)}
.page-shell{width:min(1180px,calc(100% - 32px));margin:0 auto;padding:42px 0 34px}
.page-heading{display:flex;align-items:flex-end;justify-content:space-between;gap:28px}.eyebrow{margin:0 0 8px;color:var(--primary);font-size:.75rem;font-weight:800;letter-spacing:.11em;text-transform:uppercase}
.page-heading h1{margin:0;color:var(--text);font-size:clamp(1.85rem,4vw,2.65rem);line-height:1.08;letter-spacing:-.035em}
.page-subtitle{max-width:690px;margin:11px 0 0;color:var(--muted);font-size:.98rem;line-height:1.65}
.refresh-card{display:flex;min-width:140px;flex-direction:column;gap:2px;padding:11px 14px;border:1px solid var(--border);border-radius:var(--radius-md);background:var(--surface);box-shadow:var(--shadow-small)}
.refresh-card span{color:var(--muted);font-size:.72rem}.refresh-card strong{color:var(--text);font-size:.9rem;font-weight:750}
.page-tabs{display:flex;gap:4px;margin:30px 0 22px;padding:4px;width:fit-content;border:1px solid var(--border);border-radius:12px;background:rgba(255,255,255,.72)}
.tab{padding:8px 14px;border-radius:8px;color:var(--muted);text-decoration:none;font-size:.82rem;font-weight:700;transition:background 160ms ease,color 160ms ease}.tab:hover{color:var(--text);background:var(--surface-soft)}.tab.active{color:var(--primary-dark);background:var(--primary-soft)}
.page-footer{display:flex;justify-content:center;gap:8px;margin-top:28px;color:#8b96aa;font-size:.69rem}.footer-separator{color:#c1c8d3}
.admin-card{border:1px solid var(--border);border-radius:var(--radius-lg);background:var(--surface);box-shadow:var(--shadow-small)}
.admin-card-body{padding:22px 24px}.admin-card h2{margin:0;color:var(--text);font-size:1rem;letter-spacing:-.015em}.admin-card p{margin:6px 0 0;color:var(--muted);font-size:.78rem;line-height:1.55}
.info-strip{display:grid;gap:10px;margin-bottom:20px}.info-item{padding:12px 14px;border:1px solid var(--border);border-radius:var(--radius-md);background:rgba(255,255,255,.84)}.info-item span{display:block;color:var(--muted);font-size:.66rem;font-weight:800;text-transform:uppercase;letter-spacing:.05em}.info-item strong{display:block;margin-top:5px;color:var(--text);font-size:.82rem;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.info-item strong.ok{color:var(--success)}.info-item strong.bad{color:var(--danger)}
.field{margin-top:17px}.field label{display:block;margin-bottom:7px;color:var(--text);font-size:.79rem;font-weight:750}.field-help{display:block;margin-top:6px;color:var(--muted);font-size:.7rem;line-height:1.45}
.control{width:100%;min-height:46px;margin:0;padding:0 13px;outline:none;border:1px solid var(--border-strong);border-radius:11px;color:var(--text);background:#fff;font-size:.88rem;transition:border-color 150ms ease,box-shadow 150ms ease}.control::placeholder{color:#9aa4b5}.control:focus{border-color:var(--primary);box-shadow:0 0 0 4px rgba(23,105,224,.11)}
.button-primary{min-height:46px;padding:0 16px;border:0;border-radius:11px;color:#fff;background:linear-gradient(135deg,var(--primary),#135cc5);box-shadow:0 10px 22px rgba(23,105,224,.2);cursor:pointer;font-size:.82rem;font-weight:800}.button-primary:hover{background:linear-gradient(135deg,#1e72ea,var(--primary-dark))}.button-primary:disabled{cursor:not-allowed;opacity:.48;box-shadow:none}
.button-secondary{min-height:44px;padding:0 13px;border:1px solid var(--border-strong);border-radius:10px;background:#fff;color:var(--text);font-size:.78rem;font-weight:800;cursor:pointer}.button-secondary:hover{background:var(--surface-soft)}.button-secondary:disabled{opacity:.55;cursor:not-allowed}
.notice{display:none;margin-top:18px;padding:13px 14px;border-radius:12px;font-size:.77rem;line-height:1.5}.notice.show{display:block}.notice.success{color:#146b4e;background:var(--success-soft);border:1px solid #cdeedf}.notice.error{color:#a52f3a;background:var(--danger-soft);border:1px solid #f5d1d5}.notice.info{color:var(--warning);background:var(--warning-soft);border:1px solid #f0dfb6}
.inline-note{display:flex;align-items:flex-start;gap:12px;margin-bottom:20px;padding:14px 16px;border:1px solid #dbe7f8;border-radius:var(--radius-md);background:#f7faff}.inline-note-icon{display:grid;width:24px;height:24px;place-items:center;flex:0 0 auto;border-radius:50%;color:#fff;background:var(--primary);font-size:.72rem;font-weight:800}.inline-note strong{display:block;color:var(--text);font-size:.8rem}.inline-note p{margin:3px 0 0;color:var(--muted);font-size:.74rem;line-height:1.55}
@media(max-width:620px){.topbar-inner,.page-shell{width:min(100% - 24px,1180px)}.topbar-inner{min-height:64px}.status-pill{display:none}.page-shell{padding-top:30px}.page-heading{align-items:flex-start;flex-direction:column;gap:16px}.refresh-card{min-width:0;width:100%;flex-direction:row;align-items:center;justify-content:space-between}.page-tabs{width:100%}.tab{flex:1;text-align:center}.page-footer{flex-wrap:wrap}.admin-card-body{padding:20px}}
@media(prefers-reduced-motion:reduce){*,*::before,*::after{scroll-behavior:auto!important;transition:none!important}}
)CSS";
} // namespace SharedUi
