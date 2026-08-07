// Executable harness for page modules: loads a data/*.js page module under a
// minimal window/document shim with a stubbed transport, and hands back the
// section loaders it registered. Lets a test drive real page code and observe
// what it actually does, rather than asserting on its source text.
//
// Usage: node test/test_web/helpers/page_module_harness.js <module.js>
const mkEl = () => new Proxy({}, { get: (t,k) =>
  k==="value"?"":k==="dataset"?{}:k==="classList"?{add(){},remove(){},toggle(){},contains:()=>false}:
  k==="style"?{}:k==="textContent"?"":k==="innerHTML"?"":typeof k==="string"?(()=>mkEl()):undefined,
  set: () => true });
global.window={addEventListener(){},dispatchEvent(){},setTimeout,clearTimeout,setInterval,clearInterval,location:{href:""},getSelection:()=>null};
global.document={readyState:"complete",addEventListener(){},currentScript:{dataset:{}},
  getElementById:()=>mkEl(),querySelector:()=>mkEl(),querySelectorAll:()=>[],createElement:()=>mkEl(),
  body:mkEl(),documentElement:mkEl(),visibilityState:"visible"};
global.Event=class{};global.CustomEvent=class{};global.navigator={};
const reg={};
window.PABootstrap={registerSection:(n,l)=>{reg[n]=l},setResourceLabels(){},retryNow(){},refreshSections(){},getState:()=>({})};
class ApiError extends Error{constructor(m,o={}){super(m);this.kind=o.kind||"network"}}
window.PAApi={ApiError,get:()=>Promise.reject(new ApiError("fail",{kind:"network"})),
  postForm:()=>Promise.resolve({ok:true,data:{}}),messageFor:()=>"Network error",gateControls(){}};
window.PAUtils={showFeedback(){},escapeHtml:v=>String(v??""),escapeAttr:v=>String(v??""),debounce:f=>f};
window.PAStatusStream={isSupported:()=>false,subscribe:()=>()=>{},getLastStatus:()=>null};
window.PAHealthSignals=require("/home/mattias/Documents/GitHub/protoArtoo/data/health_signals.js");

const file = process.argv[2];
require(`/home/mattias/Documents/GitHub/protoArtoo/data/${file}`);
const names = Object.keys(reg);
(async () => {
  for (const n of names) {
    let verdict;
    try { await reg[n](); verdict = "RESOLVED  <-- SWALLOWS"; }
    catch (e) { verdict = "rejected (" + (e.constructor?.name||"Error") + ")  ok"; }
    console.log(`  ${file.padEnd(10)} ${n.padEnd(22)} ${verdict}`);
  }
  process.exit(0);
})();
