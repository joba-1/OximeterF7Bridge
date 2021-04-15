#include <Arduino.h>

#include "wlan.h"
#include "webdata.h"

#include <ESPmDNS.h>
#include <HTTPUpdateServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClient.h>

// Ssid and password
#include "WlanConfig.h"

// NTP server parameters
static const char *ntpServer = "de.pool.ntp.org";
static const long gmtOffset_sec = 3600;
static const int daylightOffset_sec = 3600;

// hostname pattern
static const char hostFormat[] = "%s-%s";

// webserver and updater
static WebServer httpServer(80);
static HTTPUpdateServer httpUpdater;

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
    "   <tr><td>Device</td><td id='device'>00:00:00:00:00:00</td></tr>\n"
    "   <tr><td>Connected</td><td id='conn'>0</td></tr>\n"
    "   <tr><td>SpO<sub>2</sub></td><td id='spo2'>0</td></tr>\n"
    "   <tr><td>Perfusionsindex</td><td id='pi'>0</td></tr>\n"
    "   <tr><td>Puls pro Minute</td><td id='ppm'>0</td></tr>\n"
    "  </table>\n"
    "  </p>\n"
    "  <button type='button' "
    "onclick='window.location.href=\"update\"'>Firmware Update</button>\n"
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
      "\"dpi\":%u,\"ppm\":%u}";
  char page[sizeof(format) + 100];
  snprintf(page, sizeof(page), format, WiFi.getHostname(),
           webData.f7Device, webData.f7Connected, webData.f7Data.spO2,
           webData.f7Data.deziPI, webData.f7Data.ppm);
  httpServer.send(200, "application/json", page);
}

void handleNotFound() { 
  httpServer.send(404, "text/html", PAGE); 
}

void setHostname() {
  char hostName[100];
  String id = WiFi.macAddress().substring(9);
  id.remove(5, 1);
  id.remove(2, 1);
  snprintf(hostName, sizeof(hostName), hostFormat, webData.firmware, id.c_str());
  WiFi.setHostname(hostName);
}

void wlanTask( void *parms ) {
  (void)parms;

  setHostname();

  do {
    WiFi.begin(WlanConfig::Ssid, WlanConfig::Password);
  } while (WiFi.waitForConnectResult() != WL_CONNECTED);

  configTime(gmtOffset_sec, daylightOffset_sec, WiFi.gatewayIP().toString().c_str(), ntpServer);

  Serial.printf("Connected as %s with IP %s\n", WiFi.getHostname(),
                WiFi.localIP().toString().c_str());

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
    httpServer.handleClient();
    // We run in lowest priority, no need for a delay()...
  }
}

void startWlan() {
  uint32_t wlanCpuId = 0;
  if (portNUM_PROCESSORS > 1 && xPortGetCoreID() == 0) {
    wlanCpuId = 1; // use the other core...
  }
  xTaskCreatePinnedToCore(wlanTask, "wlan", 10240, nullptr, 0, nullptr, wlanCpuId);
}
