#include <Arduino.h>
#include <FS.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WiFiUdp.h>
#include "uptime_formatter.h"

#define WDTM_TIMEOUT    20000
#define RUN_TIMEOUT     1
#define BEACON_TIMEOUT  38

#define RLY_T0          1000
#define RLY_T1          10000

#define  BUF_LEN        32
#define  PORT           8088
#define  IP             "192.168.1.2"
#define  PKT_TH         32

#define STA_RETRY       16

#define RELAY_OUT       13
#define EXTPWR_IN       4
#define UPSPWR_IN       5
#define LED_KALV        14
#define LED_EXTPWR      16
#define LED_UPSPWR      12

#define LED_BLINK       2

#define AP_SSID         "REPO_AP"
#define AP_PASS         "REPO_PASS"

//#define FORMAT          1
//#define DEBUG           1

WiFiUDP Udp;
char trx[BUF_LEN];
unsigned char snd_pkt;
unsigned char secs;
unsigned char sys;
unsigned char mnts;

bool hard_on;
bool ledb;

unsigned long msec;
unsigned long tmr;
unsigned long hard_to;

bool b1;
bool b2;

enum state {INIT = 1, RUNS, EXTF};

String ssid;
String pass;
String key;
String pwr = "OFF";
String ups = "OFF";
String uptime = "__";

const char* PARAM_INPUT_1 = "input1";
const char* PARAM_INPUT_2 = "input2";
const char* PARAM_INPUT_3 = "input3";

const char s1[] PROGMEM = {"<!DOCTYPE HTML><html><head><title>Server Configuration</title><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"></head><body><form action=\"/get\">______WiFi_SSID:<input type=\"text\" name=\"input1\" value=\""};
const char s2[] PROGMEM = {"\"><br><br>WiFi_PASSWORD:<input type=\"text\" name=\"input2\" value=\""};
const char s3[] PROGMEM = {"\"><br><br>____ PASSWORD:<input type=\"text\" name=\"input3\" value=\""};
const char s4[] PROGMEM = {"\"><input type=\"submit\" value=\"Reboot\"></form></body></html>"};

const char s5[] PROGMEM = {"<!DOCTYPE HTML><html><head><title>Server Status</title><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"></head><body><form action=\"/get?"};
const char s6[] PROGMEM = {"\"><br><br>_AC POWER:"};
const char s7[] PROGMEM = {"<br><br>UPS POWER:"};
const char s9[] PROGMEM = {"<br><br>UP TIME:"};
const char s10[] PROGMEM = {"<br><br>____ PASSWORD:<input type=\"text\" name=\"input1\" value=\""};
const char s11[] PROGMEM = {"\"><input type=\"submit\" value=\"Reset Server\"></form></body></html>"};

String index_html;
void (*reset)(void) = 0;

AsyncWebServer server(80);

String get_value(String line, String key) {
  int lpos = 0;
  String item;
  do {
    lpos = line.indexOf('\n');
    item = line.substring(0, lpos);
    int ipos = item.indexOf('=');
    String param = item.substring(0, ipos);
    if (param == key) {
      int vpos = item.indexOf('=');
      key = item.substring(vpos + 1);
      break;
    }
    lpos++;
    line = line.substring(lpos);
  } while (lpos);
  return key;
}

void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Invalid Request");
}

void setup() {
#ifdef  DEBUG
  Serial.begin(115200);
#endif
  pinMode(RELAY_OUT, OUTPUT);
  digitalWrite(RELAY_OUT, LOW);
  pinMode(LED_KALV, OUTPUT);
  pinMode(LED_UPSPWR, OUTPUT);
  pinMode(LED_EXTPWR, OUTPUT);
  pinMode(LED_BLINK, OUTPUT);

  pinMode(UPSPWR_IN, INPUT);
  pinMode(EXTPWR_IN, INPUT);

  SPIFFS.begin();
  delay(1000);

#ifdef FORMAT
  SPIFFS.format();
  reset();
#ifdef  DEBUG
  Serial.printf("Format completed\n");
#endif
#endif

  Dir dir = SPIFFS.openDir("/");
  String fn;
  if (dir.next())fn = dir.fileName();
#ifdef  DEBUG
  Serial.printf("Files %s\n", fn.c_str());
#endif
  if (fn != "/config.txt") reset();

  File fin = SPIFFS.open("/config.txt", "r");
  if (!fin)reset();
  String line;
  while (fin.available()) {
    char c = fin.read();
    if ((c == '\n') || (c == '\r')) {
      if (line[line.length() - 1] != '\n')line += '\n';
      continue;
    }
    if (c != ' ')line += c;
  }
  fin.close();
  ssid = get_value(line, "ssid");
  pass = get_value(line, "password");
  key = get_value(line, "key");

#ifdef  DEBUG
  Serial.printf("config: %s %s %s %d\n", ssid.c_str(), pass.c_str(), key.c_str(), key.length());
#endif

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED) {
#ifdef  DEBUG
    Serial.printf("Connecting to AP %s %s %d %d\n", ssid.c_str(), pass.c_str(), WiFi.status(), retry);
#endif
    digitalWrite(LED_UPSPWR, HIGH);
    digitalWrite(LED_EXTPWR, HIGH);
    digitalWrite(LED_KALV, HIGH);
    digitalWrite(LED_BLINK, LOW);
    delay(500);
    digitalWrite(LED_UPSPWR, LOW);
    digitalWrite(LED_EXTPWR, LOW);
    digitalWrite(LED_KALV, LOW);
    digitalWrite(LED_BLINK, HIGH);
    delay(500);
    retry++;
    if (retry > STA_RETRY) break;
  }
  if (retry > STA_RETRY) {
    IPAddress apip(10, 10, 10, 1);
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(apip, apip, IPAddress(255, 255, 255, 0));
    WiFi.softAP(AP_SSID, AP_PASS, 7, false, 1);
#ifdef  DEBUG
    Serial.printf("AP mode active\n");
#endif
    index_html = String(s1) + ssid + String(s2) + pass + String(s3) + key + String(s4);
    server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
      request->send_P(200, "text/html", index_html.c_str());
    });
    server.on("/get", HTTP_GET, [] (AsyncWebServerRequest * request) {
      if (request->hasParam(PARAM_INPUT_1)) {
        ssid = request->getParam(PARAM_INPUT_1)->value();
      }
      if (request->hasParam(PARAM_INPUT_2)) {
        pass = request->getParam(PARAM_INPUT_2)->value();
      }
      if (request->hasParam(PARAM_INPUT_3)) {
        key = request->getParam(PARAM_INPUT_3)->value();
      }
#ifdef  DEBUG
      Serial.printf("AP Web message %s %s %s\n", ssid.c_str(), pass.c_str(), key.c_str());
#endif
      File fout = SPIFFS.open("/config.txt", "w");
      if (!fout)reset();
      ssid = "ssid = " + ssid;
      pass =  "password = " + pass;
      key = "key = " + key;
      fout.println(ssid.c_str());
      fout.println(pass.c_str());
      fout.println(key.c_str());
      fout.close();
      reset();
    });
    server.onNotFound(notFound);
    server.begin();
    while (1) {
      digitalWrite(LED_UPSPWR, HIGH);
      digitalWrite(LED_EXTPWR, LOW);
      delay(500);
      digitalWrite(LED_UPSPWR, LOW);
      digitalWrite(LED_EXTPWR, HIGH);
      delay(500);
#ifdef  DEBUG
      Serial.printf("AP Webserver waiting\n");
#endif
    }
  }
#ifdef  DEBUG
  Serial.printf("STA MyIP %s %ddbm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
#endif
  Udp.begin((int)PORT);

  index_html = String(s5) + String(s6) + pwr + String(s7) + ups + String(s9) + uptime + String(s10) + String(s11);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send_P(200, "text/html", index_html.c_str());
  });
  server.on("/get", HTTP_GET, [] (AsyncWebServerRequest * request) {
    String webkey;
    if (request->hasParam(PARAM_INPUT_1)) {
      webkey = request->getParam(PARAM_INPUT_1)->value();
    }
#ifdef  DEBUG
    Serial.printf("STA Web message %s\n", webkey.c_str());
#endif
    if ((webkey == key) && (sys != INIT)) {
      hard_on = true;
      hard_to = RLY_T1;
      tmr = 0;
#ifdef  DEBUG
      Serial.printf("HARD ON HIGH S:%d H:%d R:%d %d %d\n", sys, hard_on, hard_to, millis(), tmr);
#endif
      digitalWrite(RELAY_OUT, HIGH);
      digitalWrite(LED_BLINK, LOW);
    }
    request->redirect("/");
  });
  server.onNotFound(notFound);
  server.begin();

  ESP.wdtDisable();
  ESP.wdtEnable(WDTM_TIMEOUT);
  ESP.wdtFeed();
  msec = millis();
  tmr = 0;
  b1 = false;
  b2 = false;
  snd_pkt = 0;
  secs = 0;
  sys = INIT;
  mnts = 0;
  hard_on = false;
  ledb =  false;
}

void loop()
{
  tmr += millis() - msec;
  msec = millis();
  ESP.wdtFeed();

  int pktsz = Udp.parsePacket();
  if (pktsz) {
    int n = Udp.read(trx, BUF_LEN);
    String msg = String(trx);
    msg.remove(msg.length() - 1);
#ifdef  DEBUG
    String rip = Udp.remoteIP().toString();
    Serial.printf("RX %s %s %d %d\n", rip.c_str(), msg.c_str(), msg.length(), sys);
#endif
    if (msg == key) {
      snd_pkt = 0;
#ifdef  DEBUG
      Serial.printf("KAL SERVER \n");
#endif
      digitalWrite(LED_KALV, digitalRead(LED_KALV) ^ 1);
    }
    Udp.flush();
  }

  if (b1) {
    ledb = true;
    b1 = false;
    b2 = true;
  }

  if (tmr & (1 << 8)) {
    if (!b1 && !b2)b1 = true;
  } else b2 = false;

  if (ledb) {
    uptime = uptime_formatter::getUptime();
#ifdef  DEBUG
    Serial.printf("UPTIME %s\n", uptime.c_str());
#endif
    ledb = false;
    secs++;
    if (secs > BEACON_TIMEOUT) {
      secs = 0;
      mnts++;
      int len = 0;
      if (sys == EXTF) {
        memcpy(trx, "HALT", 4);
        len = 4;
      } else {
        memcpy(trx, key.c_str(), key.length());
        len = key.length();
      }
      snd_pkt++;
#ifdef  DEBUG
      Serial.printf("TX msg:[");
      for (int i = 0; i < len; i++)Serial.printf("%c", trx[i]);
      Serial.printf("] [%d] PKT %d %d\n", len, snd_pkt, sys);
#endif
      Udp.beginPacket(IP, PORT);
      Udp.write((char *)&trx, len);
      Udp.endPacket();
    }
    if (digitalRead(EXTPWR_IN)) {
      if (pwr != "ON")pwr = "ON";
      digitalWrite(LED_EXTPWR, digitalRead(LED_EXTPWR) ^ 1);
    } else {
      if (pwr != "OFF")pwr = "OFF";
      digitalWrite(LED_EXTPWR, LOW);
    }
    if (digitalRead(UPSPWR_IN)) {
      if (ups != "ON")ups = "ON";
      digitalWrite(LED_UPSPWR, digitalRead(LED_UPSPWR) ^ 1);
    } else {
      if (ups != "OFF")ups = "OFF";
      digitalWrite(LED_UPSPWR, LOW);
    }
    index_html.clear();
    index_html = String(s5) + String(s6) + pwr + String(s7) + ups + String(s9) + uptime + String(s10) + String(s11);
  }

  if (snd_pkt > PKT_TH)reset();

  switch (sys) {
    case (INIT): {
        if ((mnts >= RUN_TIMEOUT) && (pwr == "ON") && (ups == "ON")) {
          tmr = 0;
          hard_on = true;
          hard_to = RLY_T0;
#ifdef  DEBUG
          Serial.printf("HARD ON HIGH S:%d H:%d R:%d %d %d\n", sys, hard_on, hard_to, millis(), tmr);
#endif
          digitalWrite(RELAY_OUT, HIGH);
          digitalWrite(LED_BLINK, LOW);
          sys = RUNS;
          mnts = 0;
        }
        break;
      }
    case (RUNS): {
        if (pwr != "ON") {
          sys = EXTF;
          mnts = 0;
          ledb = true;
          secs = BEACON_TIMEOUT;
        }
        break;
      }
    case (EXTF): {
        if (pwr == "ON") {
          sys = INIT;
          mnts = 0;
          tmr = 0;
        }
        break;
      }
  }
  if ((hard_on) && (tmr >= hard_to)) {
    digitalWrite(RELAY_OUT, LOW);
    digitalWrite(LED_BLINK, HIGH);
    hard_on = false;
    if (hard_to == RLY_T1) {
      sys = INIT;
      tmr = 0;
      secs = 0;
      mnts = 0;
    }
#ifdef  DEBUG
    Serial.printf("HARD ON LOW S:%d H:%d R:%d %d\n", sys, hard_on, hard_to, millis());
#endif
  }
}
