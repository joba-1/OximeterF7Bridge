#include <Arduino.h>

#include "wlan.h"

#include <ESPmDNS.h>
#include <HTTPUpdateServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClient.h>

// Ssid and password
#include "WlanConfig.h"

// NTP server parameters
const char *ntpServer = "de.pool.ntp.org";
const long gmtOffset_sec = 3600;
const int daylightOffset_sec = 3600;

// hostname pattern
const char hostFormat[] = "%s-%s";

// webserver and updater
WebServer httpServer(80);
HTTPUpdateServer httpUpdater;

const char PAGE[] = "<!DOCTYPE html> <html>\n"
  "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n"
  " <title>Updater</title>\n"
  "</head>\n"
  " <body>\n"
  "  <h1>ESP32 Web Server</h1>"
  "  <a href=\"update\">Firmware Update</a>"
  " </body>\n"
  "</html>\n";

void handleIndex() { 
  httpServer.send(200, "text/html", PAGE); 
}

void handleNotFound() { 
  httpServer.send(404, "text/html", PAGE); 
}

void wlanTask( void *parms ) {
  const char *name = (const char *)parms;
  char hostName[20];
  String id = WiFi.macAddress().substring(9);
  id.remove(5, 1);
  id.remove(2, 1);
  snprintf(hostName, sizeof(hostName), hostFormat, name, id.c_str());
  WiFi.setHostname(hostName);

  do {
    WiFi.begin(WlanConfig::Ssid, WlanConfig::Password);
  } while( WiFi.waitForConnectResult() != WL_CONNECTED);

  configTime(gmtOffset_sec, daylightOffset_sec, WiFi.gatewayIP().toString().c_str(), ntpServer);
  // now getLocalTime( struct tm &t ) should give correct time.

  MDNS.begin(hostName);
  httpUpdater.setup(&httpServer);
  httpServer.on("/", handleIndex);
  httpServer.onNotFound(handleNotFound);
  httpServer.begin();
  MDNS.addService("http", "tcp", 80);

  for (;;) {
    httpServer.handleClient();
    // We run in lowest priority, no need for a delay()...
  }
}

void startWlan( const char *name ) {
  uint32_t wlanCpuId = 0;
  if (portNUM_PROCESSORS > 1 && xPortGetCoreID() == 0) {
    wlanCpuId = 1; // use the other core...
  }
  xTaskCreatePinnedToCore(wlanTask, "wlan", 10240, (void *)name, 0, NULL, wlanCpuId);
}
