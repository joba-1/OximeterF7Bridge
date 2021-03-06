#include <Arduino.h>
#include <freertos/task.h>

#include "wlan.h"
#include "webdata.h"

#include <ESPmDNS.h>
#include <HTTPUpdateServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClient.h>

#include <WiFiUdp.h>
#include <Syslog.h>

// Post to InfluxDB
#include <HTTPClient.h>

// My ssid and password
#include "WlanConfig.h"

// NTP server parameters
static const char ntpServer[] = "de.pool.ntp.org";
static const long gmtOffset_sec = 3600;
static const int daylightOffset_sec = 3600;

// Syslog server
static const char syslogServer[] = "job4";
static const int syslogPort = 514;
static WiFiUDP syslogUdp;
static Syslog syslog(syslogUdp, SYSLOG_PROTO_IETF);

// Post to InfluxDB (create database f7 in cli influx with 'create database f7')
static const char influxServer[] = "job4";
static const uint16_t influxPort = 8086;
static const char influxUri[] = "/write?db=f7&precision=s";

static WiFiClient influxWifi;
static HTTPClient influxHttp;
int influxStatus = 0;

// hostname pattern
static const char hostFormat[] = "%s-%s";

// webserver and updater
static WebServer httpServer(80);
static HTTPUpdateServer httpUpdater;

bool wlanConnected = false;

static const char PAGE[] =
    "<!DOCTYPE html>\n"
    "<html>\n"
    " <head>\n"
    "  <title id='title'></title>\n"
    " </head>\n"
    " <body>\n"
    "  <h1>F7 - Oximeter</h1>\n"
    "  <p>\n"
    "  <table>\n"
    "   <tr><td>Device</td></tr>\n"
    "   <tr><td>Id</td><td id='device'>00:00:00:00:00:00</td></tr>\n"
    "   <tr><td>Connected</td><td id='conn'>0</td></tr>\n"
    "   <tr><td><br/></td></tr>\n"
    "   <tr><td>Data</td></tr>\n"
    "   <tr><td>SpO<sub>2</sub></td><td id='spo2'>0</td></tr>\n"
    "   <tr><td>Perfusionsindex</td><td id='pi'>0</td></tr>\n"
    "   <tr><td>Puls pro Minute</td><td id='ppm'>0</td></tr>\n"
    "   <tr><td><br/></td></tr>\n"
    "   <tr><td>Database</td></tr>\n"
    "   <tr><td>Influx Status</td><td id='influx'>0</td></tr>\n"
    "  </table>\n"
    "  </p>\n"
    "  <button type='button' onclick='window.location.href=\"update\"'>Firmware Update</button>\n"
    "  <script>\n"
    "   setInterval(function() { getData(); }, 2000);\n"
    "   function getData() {\n"
    "    var xhttp = new XMLHttpRequest();\n"
    "    xhttp.onreadystatechange = function() {\n"
    "     if( this.readyState == 4 && this.status == 200 ) {\n"
    "      var data = JSON.parse(this.responseText);\n"
    "      document.getElementById('title').innerHTML = data.title;\n"
    "      document.getElementById('device').innerHTML = data.device;\n"
    "      document.getElementById('conn').innerHTML = data.connected;\n"
    "      document.getElementById('spo2').innerHTML = data.spo2;\n"
    "      document.getElementById('pi').innerHTML = data.dpi/10.0;\n"
    "      document.getElementById('ppm').innerHTML = data.ppm;\n"
    "      document.getElementById('influx').innerHTML = data.influx;\n"
    "     }\n"
    "    };\n"
    "    xhttp.open('GET', 'json', true);\n"
    "    xhttp.send();\n"
    "   }\n"
    "  </script>\n"
    " </body>\n"
    "</html>\n";

void handleRoot() {
  httpServer.send(200, "text/html", PAGE); 
}

void handleJson() {
  const char format[] =
      "{\"title\":\"%s\",\"device\":\"%s\",\"connected\":%u,\"spo2\":%u,"
      "\"dpi\":%u,\"ppm\":%u,\"influx\":%d}";
  char page[sizeof(format) + 100];
  snprintf(page, sizeof(page), format, WiFi.getHostname(),
           webData.f7Device, webData.f7Connected, webData.f7Data.spO2,
           webData.f7Data.deziPI, webData.f7Data.ppm, influxStatus);
  httpServer.send(200, "application/json", page);
}

void handleNotFound() { 
  httpServer.send(404, "text/html", PAGE); 
}

void postInflux( const webData_t &data ) {
  char fmt[] = "health,dev=\"%s\",ver=%u spo2=%u,pi=%u.%u,ppm=%u,conn=%u\n";
  char msg[sizeof(fmt) + 100];
  snprintf(msg, sizeof(msg), fmt, data.f7Device, 1, data.f7Data.spO2,
           data.f7Data.deziPI / 10, data.f7Data.deziPI % 10, data.f7Data.ppm,
           data.f7Connected);
  influxHttp.begin(influxWifi, influxServer, influxPort, influxUri);
  influxHttp.setUserAgent(data.firmware);
  influxStatus = influxHttp.POST(msg);
  influxHttp.end();
}

bool operator!=(const webData_t &lhs, const webData_t &rhs) {
  return lhs.f7Connected != rhs.f7Connected ||
         lhs.f7Data.spO2 != rhs.f7Data.spO2 ||
         lhs.f7Data.deziPI != rhs.f7Data.deziPI ||
         lhs.f7Data.ppm != rhs.f7Data.ppm ||
         strcmp(lhs.f7Device, rhs.f7Device);
}

void handleInflux() {
  static webData_t lastData = webData;
  static int lastStatus = 0;

  if( lastData != webData ) {
    lastData = webData;
    postInflux(lastData);
  }

  if( influxStatus != lastStatus ) {
    lastStatus = influxStatus;
    syslog.logf("Influx status: %d", influxStatus);
  }
}

void setHostname() {
  char hostName[100];
  String id = WiFi.macAddress().substring(9);
  id.remove(5, 1);
  id.remove(2, 1);
  snprintf(hostName, sizeof(hostName), hostFormat, webData.firmware, id.c_str());
  WiFi.setHostname(hostName);
}

void handleF7ConnectLogs() {
  static bool connected = false;
  if( connected != webData.f7Connected ) {
    connected = webData.f7Connected;
    char msg[80];
    snprintf(msg, sizeof(msg), "Device %s %sconnected", webData.f7Device, connected ? "" : "dis");
    syslog.log(LOG_NOTICE, msg); // logf() sometimes cut message or caused exceptions...
  }
}

void wlanTask( void *parms ) {
  (void)parms;

  Serial.printf("Task '%s' running on core %u\n", pcTaskGetTaskName(NULL), xPortGetCoreID());

  setHostname();

  do {
    WiFi.begin(WlanConfig::Ssid, WlanConfig::Password);
  } while (WiFi.waitForConnectResult() != WL_CONNECTED);

  wlanConnected = true;

  // Syslog setup
  syslog.server(syslogServer, syslogPort);
  syslog.deviceHostname(WiFi.getHostname());
  syslog.appName("F7");
  syslog.defaultPriority(LOG_KERN);

  configTime(gmtOffset_sec, daylightOffset_sec, WiFi.gatewayIP().toString().c_str(), ntpServer);

  char msg[80];
  snprintf(msg, sizeof(msg), "Host %s started with IP %s\n", WiFi.getHostname(),
           WiFi.localIP().toString().c_str());
  Serial.print(msg);
  syslog.log(LOG_NOTICE, msg);

  MDNS.begin(WiFi.getHostname());
  httpUpdater.setup(&httpServer);
  httpServer.on("/", handleRoot);
  httpServer.on("/json", handleJson);
  httpServer.onNotFound(handleNotFound);
  httpServer.begin();
  MDNS.addService("http", "tcp", 80);

  struct tm t = {0};
  getLocalTime(&t);
  Serial.print("Webserver ready on ");
  Serial.println(&t, "%A, %B %d %Y %H:%M:%S");

  for (;;) {
    wlanConnected = WiFi.isConnected();
    httpServer.handleClient();
    handleF7ConnectLogs();
    handleInflux();
    // We run in lowest priority, no need for a delay()...
  }
}

void startWlan() {
  uint32_t wlanCpuId = 0;
  if (portNUM_PROCESSORS > 1 && xPortGetCoreID() == 0) {
    wlanCpuId = 1; // use the other core...
  }
  xTaskCreatePinnedToCore(wlanTask, "wlan", 5*1024, nullptr, 0, nullptr, wlanCpuId);
}
