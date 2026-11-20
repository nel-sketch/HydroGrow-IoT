/* HydroGrow Uno + ESP-01 AT firmware -> LAN gateway -> Supabase/GitHub Pages.
   ONE firmware sketch: Upload this file to ARDUINO UNO only. ESP-01 keeps AT firmware.
   Gateway on PC terminates HTTPS to Supabase. LAN link is plain HTTP: trusted LAN ONLY.
   Do not use port forwarding or expose gateway to the internet.
   Physical pump is DISABLED by default pending water-level and relay verification.

   HydroGrow Arduino Uno + ESP-01 AT firmware LAN prototype.
   Tested logically; hardware-specific pins, UART speed, relay polarity and float
   polarity MUST be verified before enabling the pump.
   Required libraries: DHT (Adafruit), Adafruit Unified Sensor, RTClib,
   LiquidCrystal_I2C with lcd.init().
*/
#include <Wire.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>
#include <DHT.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>
#include <math.h>

// CHANGE THESE FOUR SETTINGS. PC_SERVER must be your COMPUTER's IPv4 on LAN.
const char WIFI_SSID[] = "YOUR_2_4GHZ_WIFI";
const char WIFI_PASS[] = "YOUR_WIFI_PASSWORD";
const char PC_SERVER[] = "192.168.1.52"; // change if PC IP changes
const char API_KEY[] = "CHANGE_ME_TO_LONG_RANDOM_KEY";
const uint16_t SERVER_PORT = 8081; // local HTTPS-forwarding gateway (not old dashboard)

const byte PIN_DHT = 2, PIN_FLOAT = 3, PIN_RELAY = 7, PIN_PH = A0;
const bool RELAY_ACTIVE_LOW = true;
// With INPUT_PULLUP, determine this from real tests of float position.
// Set to HIGH if HIGH means sufficient water, else LOW.
const int FLOAT_WATER_HIGH_LEVEL = LOW;
const bool ENABLE_PUMP_CONTROL = false; // FIRST VERIFY FLOAT, RELAY AND LOW-VOLTAGE LOAD
const bool ENABLE_AUTO_SCHEDULE = false; // test thoroughly before true
const byte SCHEDULE_1_HOUR = 8, SCHEDULE_1_MINUTE = 0;
const byte SCHEDULE_2_HOUR = 17, SCHEDULE_2_MINUTE = 0;
const unsigned long PUMP_DURATION_MS = 20000UL;
const unsigned long MAX_PUMP_RUNTIME_MS = 30000UL;
const unsigned long POLL_INTERVAL_MS = 12000UL;
const unsigned long SENSOR_INTERVAL_MS = 2500UL;
const unsigned long WIFI_RETRY_INTERVAL_MS = 30000UL;

// Do not infer water pH from a single buffer. Two-point calibration required.
const bool PH_CALIBRATED = false;
const float PH_SLOPE = -5.7f; // PLACEHOLDER, ignored until calibrated
const float PH_INTERCEPT = 21.3f; // PLACEHOLDER, ignored until calibrated

SoftwareSerial esp(10, 11); // RX, TX. Arduino D11 -> logic shifter -> ESP RX
DHT dht(PIN_DHT, DHT11);
RTC_DS3231 rtc;
LiquidCrystal_I2C lcd(0x27, 16, 2);
bool rtcOk=false, wifiOk=false, pumpOn=false, manualMode=false;
float temperature=NAN, humidity=NAN, phVolts=0, phValue=NAN;
int levelRaw=HIGH;
unsigned long lastSensor=0, lastPoll=0, lastWifiTry=0, pumpStarted=0;
unsigned long lastDisplay=0;
long lastCommandSequence=-1;
unsigned long ackId=0;
const char* ackStatus="none";
// Stores most recently executed daily schedule in EEPROM to prevent duplicate
// dosing/watering after restart. Two cells: date integer YYYYMMDD.
struct DailyMemory { uint32_t marker; uint32_t date1; uint32_t date2; };
DailyMemory memoryData;
const uint32_t EEPROM_MARKER=0x48594452UL;

void drivePump(bool enabled) {
  digitalWrite(PIN_RELAY, enabled ? (RELAY_ACTIVE_LOW ? LOW : HIGH) : (RELAY_ACTIVE_LOW ? HIGH : LOW));
  pumpOn=enabled;
  if(enabled) pumpStarted=millis();
}
bool waterHigh() { return levelRaw == FLOAT_WATER_HIGH_LEVEL; }
void readSensors() {
  temperature=dht.readTemperature();
  humidity=dht.readHumidity();
  long raw=0;
  for(byte i=0;i<10;i++) raw += analogRead(PIN_PH);
  phVolts=(raw/10.0f)*5.0f/1023.0f;
  phValue=PH_CALIBRATED ? (PH_SLOPE*phVolts+PH_INTERCEPT) : NAN;
  levelRaw=digitalRead(PIN_FLOAT);
  if (!waterHigh() && pumpOn) { drivePump(false); Serial.println(F("SAFETY: LOW WATER -> PUMP OFF")); }
}
void printSensor() {
  Serial.print(F("T=")); Serial.print(temperature);
  Serial.print(F(" H=")); Serial.print(humidity);
  Serial.print(F(" pH_V=")); Serial.print(phVolts,3);
  Serial.print(F(" water=")); Serial.print(waterHigh()?F("HIGH"):F("LOW"));
  Serial.print(F(" pump=")); Serial.println(pumpOn?F("ON"):F("OFF"));
}
void updateLCD() {
  lcd.setCursor(0,0);
  lcd.print(F("T:")); if(isnan(temperature)) lcd.print(F("ERR ")); else {lcd.print(temperature,1);lcd.print(F(" "));}
  lcd.print(F("H:")); if(isnan(humidity)) lcd.print(F("ERR")); else lcd.print(humidity,0);
  lcd.print(F("    "));
  lcd.setCursor(0,1);
  lcd.print(F("pV:")); lcd.print(phVolts,2);
  lcd.print(waterHigh()?F(" W:H"):F(" W:L"));
  lcd.print(pumpOn?F(" ON "):F(" OFF"));
}
void receiveDrain() { while(esp.available()) esp.read(); }
bool waitToken(const char *needle, unsigned long timeout) {
  const byte n=strlen(needle); byte matched=0;
  unsigned long start=millis();
  while(millis()-start<timeout) {
    while(esp.available()) {
      char c=esp.read();
      if(c==needle[matched]) { matched++; if(matched==n) return true; }
      else matched=(c==needle[0])?1:0;
    }
  }
  return false;
}
bool at(const __FlashStringHelper *command, const char* expect, unsigned long timeout) {
  receiveDrain(); esp.println(command); return waitToken(expect,timeout);
}
bool connectWifi() {
  Serial.println(F("ESP-01 checking AT firmware..."));
  if(!at(F("AT"),"OK",1500)) { Serial.println(F("AT no response. Check 9600 baud, supply, TX/RX."));return false; }
  at(F("ATE0"),"OK",1500);
  if(!at(F("AT+CWMODE=1"),"OK",2000)) return false;
  if(!at(F("AT+CIPMUX=0"),"OK",2000)) return false;
  receiveDrain();
  esp.print(F("AT+CWJAP=\""));esp.print(WIFI_SSID);esp.print(F("\",\""));esp.print(WIFI_PASS);esp.println(F("\""));
  if(!waitToken("OK",18000)) {Serial.println(F("Wi-Fi join failed"));return false;}
  Serial.println(F("ESP-01 joined Wi-Fi")); return true;
}
// HTTP GET over ESP-AT TCP. Verify HTTP status before accepting payload.
// The outgoing request is streamed to save Arduino Uno SRAM.
bool httpGet(const char* path, char* out, byte maxLen) {
  if(maxLen == 0) return false;
  out[0]='\0';
  receiveDrain();
  Serial.println(F("HTTP: opening TCP..."));
  esp.print(F("AT+CIPSTART=\"TCP\",\"")); esp.print(PC_SERVER);
  esp.print(F("\",")); esp.println(SERVER_PORT);
  // CIPSTART generally returns both CONNECT and OK. Consume through OK.
  if (!waitToken("OK", 6500)) {
    Serial.println(F("HTTP ERROR: TCP connect failed"));
    esp.println(F("AT+CIPCLOSE"));
    return false;
  }
  receiveDrain();
  // Length is exact; the longest telemetry URL would overflow the old 215-byte buffer.
  const char* prefix="GET ";
  const char* middle=" HTTP/1.0\r\nHost: ";
  const char* suffix="\r\nConnection: close\r\n\r\n";
  unsigned int reqLen=strlen(prefix)+strlen(path)+strlen(middle)+strlen(PC_SERVER)+strlen(suffix);
  esp.print(F("AT+CIPSEND=")); esp.println(reqLen);
  if(!waitToken(">", 3500)) {
    Serial.println(F("HTTP ERROR: no CIPSEND prompt"));
    esp.println(F("AT+CIPCLOSE"));
    return false;
  }
  esp.print(prefix); esp.print(path); esp.print(middle);
  esp.print(PC_SERVER); esp.print(suffix);

  // ESP-AT wraps the HTTP response in +IPD,...; parse the HTTP status and body.
  const char startPrefix[]="HTTP/1.";
  byte prefixPos=0, headerPos=0, statusPos=0, bodyPos=0;
  char statusLine[42]; statusLine[0]='\0';
  bool readingStatus=false, statusDone=false, headersDone=false, bodyDone=false;
  bool got200=false;
  unsigned long started=millis();
  while(millis()-started<8500 && !bodyDone) {
    while(esp.available()) {
      char c=(char)esp.read();
      if(!statusDone) {
        if(!readingStatus) {
          if(c==startPrefix[prefixPos]) {
            prefixPos++;
            if(prefixPos==strlen(startPrefix)) {
              readingStatus=true;
              memcpy(statusLine,startPrefix,prefixPos);
              statusPos=prefixPos;
            }
          } else prefixPos=(c==startPrefix[0])?1:0;
        } else {
          if(c=='\r'||c=='\n') {
            statusLine[statusPos]='\0';
            // expected 'HTTP/1.0 200 OK' or 'HTTP/1.1 200 OK'
            // Check code at fixed positions after 'HTTP/1.x ' (index 9).
            got200=(statusPos>=12 && statusLine[9]=='2' && statusLine[10]=='0' && statusLine[11]=='0');
            if(!got200) {
              Serial.print(F("HTTP server status: ")); Serial.println(statusLine);
            }
            statusDone=true;
            headerPos=(c=='\r')?1:0;
          } else if(statusPos<sizeof(statusLine)-1) statusLine[statusPos++]=c;
        }
      } else if(!headersDone) {
        const char marker[]="\r\n\r\n";
        if(c==marker[headerPos]) {
          headerPos++;
          if(headerPos==4) {headersDone=true;headerPos=0;}
        } else headerPos=(c=='\r')?1:0;
      } else {
        if(c=='\r'||c=='\n') {
          if(bodyPos>0) bodyDone=true;
        } else if(bodyPos<maxLen-1) {
          out[bodyPos++]=c;
        }
      }
      if(bodyDone) break;
    }
  }
  out[bodyPos]='\0';
  esp.println(F("AT+CIPCLOSE"));
  if(!statusDone) Serial.println(F("HTTP ERROR: no HTTP status received"));
  else if(!headersDone) Serial.println(F("HTTP ERROR: incomplete headers"));
  else if(bodyPos==0) Serial.println(F("HTTP ERROR: empty body"));
  return statusDone && got200 && headersDone && bodyPos>0;
}
void checkCommand() {
  char path[120];
  snprintf(path,sizeof(path),"/api/command?key=%s",API_KEY);
  char response[46];
  Serial.println(F("Checking command..."));
  if(!httpGet(path,response,sizeof(response))) {
    Serial.println(F("Command request FAILED"));
    wifiOk=false;
    return;
  }
  Serial.print(F("Command response: ")); Serial.println(response);
  char *sep=strchr(response,'|'); if(!sep) return;
  *sep='\0'; long seq=atol(response);
  if(seq<=0 || seq==lastCommandSequence) return;
  lastCommandSequence=seq;
  ackId=(unsigned long)seq;
  ackStatus="blocked";
  const char *command=sep+1;
  if(strcmp(command,"OFF")==0) {manualMode=true;drivePump(false);ackStatus="executed";Serial.println(F("Remote: OFF"));}
  else if(strcmp(command,"ON")==0) {
    manualMode=true;
    if(ENABLE_PUMP_CONTROL && waterHigh()) {drivePump(true);ackStatus="executed";Serial.println(F("Remote: ON (30s max)"));}
    else {drivePump(false);Serial.println(F("Remote ON BLOCKED: PUMP DISABLED OR LOW WATER"));}
  } else if(strcmp(command,"AUTO")==0) {manualMode=false;drivePump(false);ackStatus="executed";Serial.println(F("Remote: AUTO (schedule disabled by default)"));}
}
void sendState() {
  char path[235], t[12], h[12], pv[12], ph[12];
  // AVR snprintf does not support %f; dtostrf converts floats safely.
  if(isnan(temperature)) strcpy(t,"nan"); else dtostrf(temperature,1,1,t);
  if(isnan(humidity)) strcpy(h,"nan"); else dtostrf(humidity,1,1,h);
  dtostrf(phVolts,1,3,pv);
  if(isnan(phValue)) strcpy(ph,"nan"); else dtostrf(phValue,1,2,ph);
  snprintf(path,sizeof(path),"/api/upload?key=%s&t=%s&h=%s&v=%s&ph=%s&water=%d&pump=%d&manual=%d&ack=%lu&status=%s",API_KEY,t,h,pv,ph,waterHigh()?1:0,pumpOn?1:0,manualMode?1:0,ackId,ackStatus);
  char reply[12];
  Serial.println(F("Uploading sensor data..."));
  if(httpGet(path,reply,sizeof(reply))) {
    Serial.print(F("Server reply: ")); Serial.println(reply);
    if(strcmp(reply,"OK")!=0) {Serial.println(F("Unexpected upload reply")); wifiOk=false;}
  } else {
    Serial.println(F("HTTP UPLOAD FAILED"));
    wifiOk=false;
  }
}
uint32_t todayKey(const DateTime &n) {return (uint32_t)n.year()*10000UL+(uint32_t)n.month()*100UL+n.day();}
void autoSchedule() {
  if(!ENABLE_AUTO_SCHEDULE || !rtcOk || manualMode || !waterHigh() || pumpOn) return;
  DateTime now=rtc.now();
  uint32_t today=todayKey(now);
  byte slot=0;
  if(now.hour()==SCHEDULE_1_HOUR && now.minute()==SCHEDULE_1_MINUTE && memoryData.date1!=today)slot=1;
  if(now.hour()==SCHEDULE_2_HOUR && now.minute()==SCHEDULE_2_MINUTE && memoryData.date2!=today)slot=2;
  if(slot) {
    if(slot==1) memoryData.date1=today; else memoryData.date2=today;
    EEPROM.put(0,memoryData); // once per schedule/day; no reboot duplicates
    drivePump(true);
    Serial.println(F("AUTO schedule started"));
  }
}
void setup() {
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY,RELAY_ACTIVE_LOW?HIGH:LOW); // relay off before initialization
  pinMode(PIN_FLOAT, INPUT_PULLUP);
  Serial.begin(9600);
  esp.begin(9600); // ESP-01 AT firmware must be at 9600 baud
  Wire.begin(); dht.begin(); lcd.init();lcd.backlight();
  lcd.print(F("HydroGrow boot"));
  rtcOk=rtc.begin();
  if(rtcOk && rtc.lostPower()) {rtc.adjust(DateTime(F(__DATE__),F(__TIME__)));Serial.println(F("RTC lost power: VERIFY LOCAL TIME"));}
  EEPROM.get(0,memoryData);
  if(memoryData.marker!=EEPROM_MARKER) {
    memoryData.marker=EEPROM_MARKER;memoryData.date1=0;memoryData.date2=0;
    EEPROM.put(0,memoryData);
  }
  readSensors();drivePump(false);
  Serial.print(F("Server: "));Serial.print(PC_SERVER);Serial.print(F(":"));Serial.println(SERVER_PORT);
  wifiOk=connectWifi();lastWifiTry=millis();
  lastPoll=millis()-POLL_INTERVAL_MS;
}
void loop() {
  unsigned long now=millis();
  if(now-lastSensor>=SENSOR_INTERVAL_MS) {lastSensor=now;readSensors();printSensor();}
  if(now-lastDisplay>=2000) {lastDisplay=now;updateLCD();}
  if(pumpOn && (!waterHigh() || now-pumpStarted>=MAX_PUMP_RUNTIME_MS || (!manualMode && now-pumpStarted>=PUMP_DURATION_MS))) {
    drivePump(false);Serial.println(F("Pump off: safety/timeout"));
  }
  autoSchedule();
  if(!wifiOk && now-lastWifiTry>WIFI_RETRY_INTERVAL_MS) {lastWifiTry=now;wifiOk=connectWifi();}
  if(wifiOk && now-lastPoll>=POLL_INTERVAL_MS) {
    lastPoll=now;
    checkCommand();
    if(wifiOk) sendState();
  }
}
