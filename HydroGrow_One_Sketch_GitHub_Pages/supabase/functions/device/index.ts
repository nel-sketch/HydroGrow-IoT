// Supabase Edge Function. Device bearer key and service role never appear in frontend.
import { createClient } from 'npm:@supabase/supabase-js@2';
const env = (name: string) => Deno.env.get(name) || '';
const deviceId = env('HYDRO_DEVICE_ID') || 'hydrogrow-uno-01';
const token = env('HYDRO_DEVICE_TOKEN');
const db = createClient(env('SUPABASE_URL'), env('SUPABASE_SERVICE_ROLE_KEY'), {
  auth: { persistSession: false }
});
const response = (body: string, code = 200) => new Response(body, {
  status: code,
  headers: { 'content-type': 'text/plain; charset=utf-8', 'cache-control': 'no-store' }
});
const encoder = new TextEncoder();
async function equalToken(a: string, b: string) {
  const [ah, bh] = await Promise.all([crypto.subtle.digest('SHA-256', encoder.encode(a)), crypto.subtle.digest('SHA-256', encoder.encode(b))]);
  const x = new Uint8Array(ah), y = new Uint8Array(bh);
  let diff = 0;
  for (let i=0;i<x.length;i++) diff |= x[i] ^ y[i];
  return diff === 0;
}
function num(x: unknown, lower: number, upper: number): number | null {
  if (x === null || x === '' || x === undefined) return null;
  const n = Number(x);
  return Number.isFinite(n) && n >= lower && n <= upper ? n : null;
}
async function alert(code: string, message: string) {
  const since = new Date(Date.now()-10*60*1000).toISOString();
  const {data} = await db.from('alerts').select('id').eq('device_id', deviceId).eq('code', code).gte('at', since).limit(1);
  if (!data?.length) await db.from('alerts').insert({device_id: deviceId, code, message});
}
Deno.serve(async request => {
  if (!token || token.length < 24 || !env('SUPABASE_SERVICE_ROLE_KEY')) return response('SERVER_NOT_CONFIGURED',503);
  if (!(await equalToken(request.headers.get('x-device-key') || '',token))) return response('UNAUTHORIZED',401);
  const url = new URL(request.url);
  if (url.pathname.endsWith('/poll') && request.method === 'GET') {
    // Expired requests are ignored, so a delayed ON cannot start the pump.
    const {data,error} = await db.from('commands').select('id,command').eq('device_id',deviceId)
      .eq('status','pending').gt('expires_at',new Date().toISOString()).order('id',{ascending:false}).limit(1);
    if (error) return response('DB_ERROR',500);
    return response(data?.length ? `${data[0].id}|${data[0].command}\n` : '0|NONE\n');
  }
  if (url.pathname.endsWith('/telemetry') && request.method === 'POST') {
    let p: Record<string,unknown>;
    try { p = await request.json(); } catch { return response('BAD_JSON',400); }
    if (typeof p.water_high !== 'boolean' || typeof p.pump_on !== 'boolean') return response('BAD_FIELDS',400);
    const record = {
      device_id:deviceId, temperature:num(p.temperature,-50,100), humidity:num(p.humidity,0,100),
      ph_voltage:num(p.ph_voltage,0,5.05), water_high:p.water_high, pump_on:p.pump_on
    };
    const {error} = await db.from('telemetry').insert(record);
    if (error) return response('DB_ERROR',500);
    await db.from('devices').update({last_seen:new Date().toISOString()}).eq('id',deviceId);
    if (!record.water_high) await alert('LOW_WATER','Water level is LOW: pump must remain OFF');
    if (record.temperature===null || record.humidity===null) await alert('SENSOR_ERROR','DHT11 reading unavailable');
    if (record.temperature!==null && record.temperature>=38) await alert('HOT','Temperature exceeds 38°C');
    const ackId = Number(p.ack_id);
    if (Number.isSafeInteger(ackId) && ackId>0 && ['executed','blocked'].includes(String(p.ack_status))) {
      await db.from('commands').update({status:p.ack_status,executed_at:new Date().toISOString()})
        .eq('device_id',deviceId).eq('id',ackId).eq('status','pending');
    }
    return response('OK\n');
  }
  return response('NOT_FOUND',404);
});
