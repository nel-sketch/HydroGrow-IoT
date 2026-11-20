'use strict';
const conf = window.HYDRO_CONFIG;
const $ = id => document.getElementById(id);
const configured = conf.supabaseUrl.startsWith('https://') && !conf.supabaseUrl.includes('YOUR_') && !conf.supabaseAnonKey.includes('YOUR_');
const client = configured ? window.supabase.createClient(conf.supabaseUrl, conf.supabaseAnonKey) : null;
let user = null, lastMeasurement = null, timer = null;
const fmt = (v,unit='',digits=1) => typeof v === 'number' && Number.isFinite(v) ? v.toFixed(digits)+unit : '—';
const time = v => v ? new Date(v).toLocaleString() : '—';
function status(message,ok){$('connection').textContent=message;$('connection').className=ok?'online':'offline';}
function text(id,value){$(id).textContent=value;}
function chart(rows){const c=$('trend'),ctx=c.getContext('2d');ctx.clearRect(0,0,c.width,c.height);const values=rows.filter(r=>typeof r.temperature==='number').map(r=>r.temperature).reverse();ctx.strokeStyle='#b5c9ba';ctx.lineWidth=1;for(let i=0;i<4;i++){const y=30+i*45;ctx.beginPath();ctx.moveTo(20,y);ctx.lineTo(600,y);ctx.stroke()}if(values.length<2){ctx.fillStyle='#667970';ctx.fillText('Not enough readings to draw a chart',28,110);return}const min=Math.min(...values)-1,max=Math.max(...values)+1;ctx.strokeStyle='#167356';ctx.lineWidth=3;ctx.beginPath();values.forEach((v,i)=>{const x=20+i*580/(values.length-1),y=175-(v-min)*135/(max-min);i?ctx.lineTo(x,y):ctx.moveTo(x,y)});ctx.stroke();ctx.fillStyle='#305449';ctx.fillText(`${max.toFixed(1)}°C`,20,22);ctx.fillText(`${min.toFixed(1)}°C`,20,198)}
async function load(){if(!user)return;const id=conf.deviceId;
  try{
    const [device,measures,commands,alerts]=await Promise.all([
      client.from('devices').select('id,last_seen').eq('id',id).maybeSingle(),
      client.from('telemetry').select('*').eq('device_id',id).order('at',{ascending:false}).limit(100),
      client.from('commands').select('id,command,status,requested_at').eq('device_id',id).order('id',{ascending:false}).limit(12),
      client.from('alerts').select('code,message,at').eq('device_id',id).order('at',{ascending:false}).limit(10)
    ]);
    for(const r of [device,measures,commands,alerts])if(r.error)throw r.error;
    if(!device.data){status('Device not registered',false);return}
    const rows=measures.data||[];lastMeasurement=rows[0]||null;
    const online=!!device.data.last_seen && Date.now()-new Date(device.data.last_seen).getTime()<45000;
    status(online?'Controller ONLINE':'Controller OFFLINE',online);
    text('updated',lastMeasurement?`Last received ${time(lastMeasurement.at)}`:'Waiting for telemetry');
    text('temp',fmt(lastMeasurement?.temperature,' °C'));
    text('humidity',fmt(lastMeasurement?.humidity,' %',0));
    text('ph',fmt(lastMeasurement?.ph_voltage,' V',2));
    text('water',lastMeasurement?(lastMeasurement.water_high?'HIGH':'LOW'):'—');
    text('pumpstate',lastMeasurement?(lastMeasurement.pump_on?'Pump ON':'Pump OFF'):'Pump status unknown');
    $('on').disabled=!online || !lastMeasurement?.water_high;
    const alertMessages=(alerts.data||[]).map(a=>`${time(a.at)} — ${a.message}`);
    if(!online)alertMessages.unshift('Controller offline or no recent readings');
    $('alerts').replaceChildren(...(alertMessages.length?alertMessages:['No alerts recorded.']).map(m=>{const li=document.createElement('li');li.textContent=m;return li}));
    $('commands').replaceChildren(...(commands.data||[]).map(r=>{const tr=document.createElement('tr');for(const value of [time(r.requested_at),r.command,r.status]){const td=document.createElement('td');td.textContent=value;tr.append(td)}return tr}));
    chart(rows);
  }catch(e){status('Cloud connection error',false);text('commandstatus',e.message||String(e))}
}
async function command(mode){if(!user)return;if(mode==='ON' && (!lastMeasurement?.water_high || Date.now()-new Date(lastMeasurement.at).getTime()>45000)){text('commandstatus','Pump ON blocked: no fresh HIGH water reading');return}
  const {error}=await client.from('commands').insert({device_id:conf.deviceId,owner_id:user.id,command:mode});
  text('commandstatus',error?`Error: ${error.message}`:`${mode} requested. Waiting for Arduino acknowledgement.`);
  await load();
}
async function show(){const {data:{session}}=await client.auth.getSession();user=session?.user||null;$('login').classList.toggle('hidden',!!user);$('app').classList.toggle('hidden',!user);$('signout').classList.toggle('hidden',!user);if(timer)clearInterval(timer);if(user){await load();timer=setInterval(load,12000)}else status('Not signed in',false)}
$('loginform').addEventListener('submit',async e=>{e.preventDefault();if(!client)return;text('loginerror','');const {error}=await client.auth.signInWithPassword({email:$('email').value,password:$('password').value});if(error)text('loginerror',error.message);else await show()});
$('signout').onclick=async()=>{await client.auth.signOut();await show()};
$('on').onclick=()=>command('ON');$('off').onclick=()=>command('OFF');$('auto').onclick=()=>command('AUTO');
if(conf.cameraHttpsUrl.startsWith('https://')){const f=document.createElement('iframe');f.src=conf.cameraHttpsUrl;f.title='Secured ESP32-CAM gateway';f.referrerPolicy='no-referrer';$('camera').replaceChildren(f)}
if(!configured){text('loginerror','Configure site/config.js with your Supabase project URL and publishable key before using this dashboard.');$('loginform').querySelector('button').disabled=true;status('Configuration required',false)}else{client.auth.onAuthStateChange(()=>setTimeout(show,0));show()}
