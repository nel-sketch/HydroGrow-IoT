// HydroGrow local LAN->Supabase HTTPS gateway. Node 18+; no packages required.
// Run only on a trusted private LAN. Never expose port 8081 to the internet.
const http = require('node:http');
const crypto = require('node:crypto');
const {URL} = require('node:url');

const PORT = Number(process.env.GATEWAY_PORT || '8081');
const LAN_KEY = process.env.HYDRO_LAN_KEY || '';
const CLOUD_KEY = process.env.HYDRO_DEVICE_TOKEN || '';
const FUNCTION_URL = (process.env.HYDRO_FUNCTION_URL || '').replace(/\/$/, '');
if (!LAN_KEY || LAN_KEY.length < 24 || !CLOUD_KEY || CLOUD_KEY.length < 24 || !/^https:\/\/[^/]+/.test(FUNCTION_URL)) {
  console.error('Set HYDRO_LAN_KEY, HYDRO_DEVICE_TOKEN (24+ chars each), HYDRO_FUNCTION_URL (https).');
  process.exit(1);
}
if (!Number.isInteger(PORT) || PORT < 1024 || PORT > 65535) process.exit(1);
let lastCmd = null;
const safeCompare = (a, b) => {
  const x=Buffer.from(a), y=Buffer.from(b);
  return x.length===y.length && crypto.timingSafeEqual(x,y);
};
function send(res, code, body) {
  res.writeHead(code, {'Content-Type':'text/plain; charset=utf-8','Cache-Control':'no-store'});
  res.end(body+'\n');
}
function toValue(q, name, lo, hi) {
  const raw=q.get(name);
  if(raw===null || raw==='nan') return null;
  const v=Number(raw);
  return Number.isFinite(v) && v>=lo && v<=hi ? v : null;
}
async function cloud(path, method='GET', payload) {
  const response=await fetch(FUNCTION_URL+path, {
    method, headers:{'x-device-key':CLOUD_KEY, ...(payload?{'content-type':'application/json'}:{})},
    body:payload?JSON.stringify(payload):undefined,
    signal:AbortSignal.timeout(12000)
  });
  const body=(await response.text()).trim();
  if(!response.ok) throw new Error(`Cloud HTTP ${response.status}: ${body.slice(0,100)}`);
  return body;
}
const server=http.createServer(async(req,res)=>{
  if(req.method!=='GET') return send(res,405,'METHOD_NOT_ALLOWED');
  let u;
  try { u=new URL(req.url,'http://gateway.local'); } catch {return send(res,400,'BAD_REQUEST');}
  const key=u.searchParams.get('key')||'';
  if(!safeCompare(key,LAN_KEY)) return send(res,403,'DENIED');
  try {
    if(u.pathname==='/api/command') {
      const raw=await cloud('/poll');
      const m=/^(\d+)\|(ON|OFF|AUTO)$/.exec(raw);
      if(!m) {lastCmd=null;return send(res,200,'0|NONE');}
      const id=Number(m[1]);
      if(!Number.isSafeInteger(id) || id<1 || id>2147483647) return send(res,503,'COMMAND_ID_TOO_LARGE');
      lastCmd=id;
      console.log('Cloud command',id,m[2]);
      return send(res,200,`${id}|${m[2]}`);
    }
    if(u.pathname==='/api/upload') {
      const q=u.searchParams;
      if(!['0','1'].includes(q.get('water')) || !['0','1'].includes(q.get('pump')))
        return send(res,400,'BAD_FIELDS');
      const ack=Number(q.get('ack')||'0');
      const stat=q.get('status')||'none';
      const payload={
        temperature:toValue(q,'t',-50,100),humidity:toValue(q,'h',0,100),
        ph_voltage:toValue(q,'v',0,5.05),water_high:q.get('water')==='1',pump_on:q.get('pump')==='1',
        ack_id:Number.isSafeInteger(ack)&&ack>0?ack:0,
        ack_status:['executed','blocked'].includes(stat)?stat:'none'
      };
      const reply=await cloud('/telemetry','POST',payload);
      console.log('Telemetry:',payload.temperature,payload.humidity,payload.ph_voltage,
        'water',payload.water_high,'pump',payload.pump_on,'ack',payload.ack_id,payload.ack_status);
      return send(res,200,reply==='OK'?'OK':reply);
    }
    return send(res,404,'NOT_FOUND');
  }catch(e){console.error('Gateway error:',e.message);return send(res,503,'CLOUD_UNAVAILABLE');}
});
server.listen(PORT,'0.0.0.0',()=>console.log(`HydroGrow gateway on LAN port ${PORT}; cloud TLS verification enabled`));
