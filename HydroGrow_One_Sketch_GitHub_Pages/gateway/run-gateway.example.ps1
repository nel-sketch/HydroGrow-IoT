# Copy commands into PowerShell, edit placeholders locally. DO NOT commit real tokens.
$env:HYDRO_LAN_KEY="REPLACE_WITH_A_NEW_24_PLUS_CHARACTER_LOCAL_KEY"
$env:HYDRO_DEVICE_TOKEN="REPLACE_WITH_24_PLUS_CHARACTER_SUPABASE_DEVICE_TOKEN"
$env:HYDRO_FUNCTION_URL="https://YOUR_PROJECT_REF.supabase.co/functions/v1/device"
node gateway.js
