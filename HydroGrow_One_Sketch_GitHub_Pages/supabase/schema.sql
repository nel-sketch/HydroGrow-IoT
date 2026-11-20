-- Execute once in the Supabase SQL editor, in a fresh project.
create table if not exists public.devices (
  id text primary key,
  owner_id uuid not null references auth.users(id) on delete cascade,
  label text not null default 'HydroGrow Uno',
  last_seen timestamptz,
  created_at timestamptz not null default now()
);
create table if not exists public.telemetry (
  id bigint generated always as identity primary key,
  device_id text not null references public.devices(id) on delete cascade,
  at timestamptz not null default now(),
  temperature double precision,
  humidity double precision,
  ph_voltage double precision,
  water_high boolean not null,
  pump_on boolean not null
);
create index if not exists telemetry_device_at_idx on public.telemetry(device_id, at desc);
create table if not exists public.commands (
  id bigint generated always as identity primary key,
  device_id text not null references public.devices(id) on delete cascade,
  owner_id uuid not null references auth.users(id),
  command text not null check (command in ('ON','OFF','AUTO')),
  status text not null default 'pending' check (status in ('pending','executed','blocked','expired')),
  requested_at timestamptz not null default now(),
  expires_at timestamptz not null default (now() + interval '45 seconds'),
  executed_at timestamptz
);
create index if not exists commands_device_idx on public.commands(device_id,id desc);
create table if not exists public.alerts (
  id bigint generated always as identity primary key,
  device_id text not null references public.devices(id) on delete cascade,
  at timestamptz not null default now(),
  code text not null,
  message text not null
);
create index if not exists alerts_device_at_idx on public.alerts(device_id,at desc);

alter table public.devices enable row level security;
alter table public.telemetry enable row level security;
alter table public.commands enable row level security;
alter table public.alerts enable row level security;

create policy "Owner reads devices" on public.devices for select to authenticated using (owner_id=auth.uid());
-- Device registration is ADMIN-ONLY via SQL editor, not browser sign-up.
create policy "Owner reads measurements" on public.telemetry for select to authenticated using (
 exists(select 1 from public.devices d where d.id=telemetry.device_id and d.owner_id=auth.uid())
);
create policy "Owner reads commands" on public.commands for select to authenticated using (
 exists(select 1 from public.devices d where d.id=commands.device_id and d.owner_id=auth.uid())
);
create policy "Owner requests commands" on public.commands for insert to authenticated with check (
 owner_id=auth.uid() and status='pending' and expires_at > now() and expires_at <= now()+interval '60 seconds'
 and exists(select 1 from public.devices d where d.id=commands.device_id and d.owner_id=auth.uid())
);
create policy "Owner reads alerts" on public.alerts for select to authenticated using (
 exists(select 1 from public.devices d where d.id=alerts.device_id and d.owner_id=auth.uid())
);
-- No client inserts/updates for sensor data, alert records or command status.
-- The Edge Function is the ONLY writer, using server-side service-role access.
