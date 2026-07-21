/*
 * AI Hear Bridge v4 + QoS1 — WiFiManager + device‑level MQTT with reliable delivery
 */
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <PubSubClient.h>
#include <EEPROM.h>

#define EE_MAGIC     0x1234
#define EE_OFF_MAGIC 0
#define EE_OFF_SSID  4
#define EE_OFF_PASS  40

static WiFiClient    wifi_client;
static PubSubClient  mqtt(wifi_client);
static ESP8266WebServer server(80);
static char          rx_buf[256];
static uint8_t       rx_idx = 0;
static char ssid[33] = "", pass[33] = "";
static char mqtt_client_id[32], alert_topic[64], status_topic[64], env_topic[64];
static String last_alert = "";  /* HTTP alert state */
static uint32_t last_alert_time = 0;
static uint32_t pub_seq = 0, last_mqtt = 0, last_status = 0;
static uint32_t boot_id = 0;

/* ---- EEPROM ---- */
static void save_wifi() {
  EEPROM.put(EE_OFF_MAGIC,(uint16_t)EE_MAGIC);
  for(int i=0;i<32;i++){EEPROM.write(EE_OFF_SSID+i,ssid[i]);EEPROM.write(EE_OFF_PASS+i,pass[i]);}
  EEPROM.commit();
}
static bool load_wifi() {
  uint16_t m; EEPROM.get(EE_OFF_MAGIC,m);
  if(m!=EE_MAGIC) return false;
  for(int i=0;i<32;i++){ssid[i]=EEPROM.read(EE_OFF_SSID+i);pass[i]=EEPROM.read(EE_OFF_PASS+i);}
  ssid[32]=pass[32]=0;
  return strlen(ssid)>0;
}

/* ---- AP config page ---- */
static const char PAGE[] PROGMEM = R"raw(
<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>AI Hear 配网</title>
<style>*{margin:0;padding:0;box-sizing:border-box}body{font-family:Arial,sans-serif;background:#FFF8F5;color:#4A4A4A;padding:30px 20px}
h2{text-align:center;margin-bottom:30px;color:#F2A0A0}input{width:100%;padding:14px;margin:10px 0;border:2px solid #F0E8E3;border-radius:12px;font-size:16px}
button{width:100%;padding:14px;background:#F2A0A0;color:#fff;border:none;border-radius:12px;font-size:18px;font-weight:600;margin-top:10px;cursor:pointer}
.success{color:#A8D8CA;text-align:center;margin-top:20px;display:none}
</style></head><body><h2>🔧 AI Hear 配网</h2>
<form onsubmit="save(event)">
<input id="s" placeholder="WiFi名称" required><input id="p" type="password" placeholder="WiFi密码" required>
<button type="submit">保存并连接</button></form>
<div class="success" id="ok">✅ 已保存，正在连接...</div>
<script>
function save(e){e.preventDefault();
var x=new XMLHttpRequest();x.open('GET','/save?ssid='+encodeURIComponent(s.value)+'&pass='+encodeURIComponent(p.value));
x.onload=function(){if(x.status==200){document.querySelector('form').style.display='none';ok.style.display='block'}};
x.send()}
</script></body></html>
)raw";

static void handleRoot() { server.send(200,"text/html",FPSTR(PAGE)); }
static void handleAlert() {
  char buf[128];
  snprintf(buf,sizeof(buf),"{\"alert\":\"%s\",\"time\":%lu}",
           last_alert.c_str(),(unsigned long)last_alert_time);
  server.send(200,"application/json",buf);
}
static void handleClear() { last_alert=""; last_alert_time=0; server.send(200,"text/plain","OK"); }
static void handleSave() {
  String s=server.arg("ssid"), p=server.arg("pass");
  if(s.length()>0&&s.length()<33&&p.length()>0&&p.length()<33){
    strncpy(ssid,s.c_str(),32); strncpy(pass,p.c_str(),32);
    save_wifi(); server.send(200,"text/plain","OK"); delay(500); ESP.restart();
  } else server.send(400,"text/plain","ERR");
}

/* Keep all MQTT stream parsing inside PubSubClient. Reading PUBACK bytes
 * directly here can consume an interleaved App command and corrupt the stream. */
static bool publish_reliable(const char *topic, const char *payload, bool retain = false) {
  if (!mqtt.connected()) return false;
  if (mqtt.publish(topic, payload, retain)) return true;
  mqtt.loop();
  delay(50);
  return mqtt.connected() && mqtt.publish(topic, payload, retain);
}

/* ── UART ── */
static void parse_command(const char *cmd) {
  if(strncmp(cmd,"+PUB:",5)==0){
    const char *s=cmd+5,*sep=strchr(s,':');
    if(!sep||sep==s){Serial.println("+ERR:PARSE");return;}
    char t[64];int l=sep-s;if(l>63)l=63;memcpy(t,s,l);t[l]=0;
    if(!mqtt.connected()){Serial.println("+ERR:MQTT_DOWN");return;}
    bool ok=true;
    pub_seq++;
    if(strcmp(t,"aihear/alert")==0){
      char cls[32]="unknown"; float score=0.0f;
      const char *colon=strchr(sep+1,':');
      if(colon){
        size_t n=(size_t)(colon-(sep+1)); if(n>sizeof(cls)-1)n=sizeof(cls)-1;
        memcpy(cls,sep+1,n);cls[n]=0;score=strtof(colon+1,nullptr);
      }
      char json[192];
      snprintf(json,sizeof(json),
        "{\"eventId\":\"%s-%08lx-%lu\",\"class\":\"%s\",\"score\":%.3f,\"uptimeMs\":%lu}",
        mqtt_client_id,(unsigned long)boot_id,(unsigned long)pub_seq,cls,
        (double)score,(unsigned long)millis());
      ok=publish_reliable(alert_topic,json);
      if(strstr(sep+1,"baby_cry")||strstr(sep+1,"help")){last_alert=sep+1;last_alert_time=millis();}
    } else if(strcmp(t,"aihear/env")==0){
      /* env data → device‑scoped topic only, no global broadcast */
      ok=publish_reliable(env_topic,sep+1,false);
    } else {
      bool retain = strstr(t, "/state") != nullptr;
      ok=publish_reliable(t,sep+1,retain);
    }
    if(!ok){Serial.println("+ERR:PUB_FAIL");return;}
    Serial.printf("+PUBACK:%lu\r\n",(unsigned long)pub_seq);
  }else if(strcmp(cmd,"+STATUS")==0)
    Serial.printf("+STATUS:%d:%d\r\n",(WiFi.status()==WL_CONNECTED)?2:0,mqtt.connected()?2:0);
  else Serial.println("+ERR:UNKNOWN");
}

static void publish_status() {
  char json[160];
  snprintf(json,sizeof(json),
    "{\"deviceId\":\"%s\",\"online\":true,\"uptimeMs\":%lu,\"seq\":%lu,\"fw\":\"bridge-v4\"}",
    mqtt_client_id,(unsigned long)millis(),(unsigned long)pub_seq);
  mqtt.publish(status_topic,json,false);
}

/* ── MQTT callback: forward App commands to STM32, filtered by device ID ── */
static void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, "aihear/cmd") == 0) {
    char buf[64];
    unsigned int len = length < sizeof(buf) - 1 ? length : sizeof(buf) - 1;
    memcpy(buf, payload, len); buf[len] = '\0';
    /* Only forward if device ID matches exactly — never accept broadcasts */
    const char *did = strstr(buf, "\"device\":\"");
    if (!did) return;                             /* no device field → reject */
    did += 10;  /* skip "device":" */
    if (strncmp(did, mqtt_client_id, strlen(mqtt_client_id)) != 0)
      return;  /* not for this device — ignore */
    Serial.printf("+CMD:%s\r\n", buf);
  }
}

static void mqtt_reconnect() {
  mqtt.setServer("broker-cn.emqx.io",1883);
  mqtt.setCallback(mqtt_callback);
  mqtt.connect(mqtt_client_id);
  if(mqtt.connected()) {
    mqtt.subscribe("aihear/cmd");
    publish_status();
  }
}

/* ---- setup ---- */
void setup() {
  Serial.begin(115200);
  delay(2000);
  EEPROM.begin(128);
  uint8_t mac[6]; WiFi.macAddress(mac);
  snprintf(mqtt_client_id,sizeof(mqtt_client_id),"aihear_%02x%02x%02x",mac[3],mac[4],mac[5]);
  snprintf(alert_topic,sizeof(alert_topic),"aihear/v1/demo/%s/alert",mqtt_client_id);
  snprintf(status_topic,sizeof(status_topic),"aihear/v1/demo/%s/status",mqtt_client_id);
  snprintf(env_topic,   sizeof(env_topic),   "aihear/v1/demo/%s/env",   mqtt_client_id);
  boot_id=ESP.getChipId()^micros()^ESP.getCycleCount();
  Serial.printf("+DEVICEID:%s\r\n",mqtt_client_id);

  if(load_wifi()){
    Serial.println("+READY");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid,pass);
    server.on("/alert",handleAlert);
    server.on("/clear",handleClear);
    server.begin();
  } else {
    Serial.println("+READY");
    Serial.println("+CONFIG:open_ap");
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("AIHear_Setup","12345678");
    server.on("/",handleRoot);
    server.on("/save",handleSave);
    server.on("/alert",handleAlert);
    server.on("/clear",handleClear);
    server.begin();
  }
}

/* ---- loop ---- */
void loop() {
  uint32_t now=millis();

  /* GPIO0 long-press 5s = clear WiFi and restart to AP mode */
  pinMode(0,INPUT_PULLUP);
  static uint32_t gpio0_down=0;
  if(digitalRead(0)==LOW){
    if(!gpio0_down) gpio0_down=now;
    else if(now-gpio0_down>5000){
      EEPROM.put(EE_OFF_MAGIC,(uint16_t)0); EEPROM.commit();
      Serial.println("+CONFIG:reset_ap"); delay(500); ESP.restart();
    }
  } else gpio0_down=0;
  if(WiFi.status()==WL_CONNECTED&&WiFi.getMode()==WIFI_AP_STA){
    WiFi.softAPdisconnect(true);  /* stop AP once connected */
  }

  /* UART read */
  while(Serial.available()){
    char c=Serial.read();
    if(c=='\n'){if(rx_idx&&rx_buf[rx_idx-1]=='\r')rx_idx--;rx_buf[rx_idx]=0;parse_command(rx_buf);rx_idx=0;}
    else if(rx_idx<255) rx_buf[rx_idx++]=c;
  }

  /* AP mode: serve config page */
  if(WiFi.getMode()==WIFI_AP_STA && WiFi.status()!=WL_CONNECTED){
    server.handleClient();
    static uint32_t last_id=0;
    if(now-last_id>5000){last_id=now;Serial.printf("+DEVICEID:%s\r\n",mqtt_client_id);}
    return;
  }

  /* Repeat DEVICEID every 5s in case STM32 missed it */
  static uint32_t last_id_sta=0;
  if(now-last_id_sta>5000){last_id_sta=now;Serial.printf("+DEVICEID:%s\r\n",mqtt_client_id);}

  /* STA mode: MQTT */
  if(!mqtt.connected()){if(now-last_mqtt>5000){last_mqtt=now;mqtt_reconnect();}return;}
  mqtt.loop();
  if(now-last_status>60000){
    last_status=now;
    Serial.printf("+STATUS:%d:%d\r\n",(WiFi.status()==WL_CONNECTED)?2:0,mqtt.connected()?2:0);
    publish_status();
  }
}
