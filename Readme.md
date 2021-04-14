# Oximeter F7 BLE Client

connects to F7 Oximeter and reports SpO2, PI and ppm on serial console

TODOS

* Android app can reconnect a lost BLE connection (closed app) without restarting the oximeter.
  Need to find out the communication neccessary for that...
* Offer data as a web service
* Push data to MQTT server or post to influxDB
* Use proper time via ntp
* RGB led reflects status (cyan: not connected to ble, magenta: not connected to WLAN, green: healthy -> red: unhealthy)
* Short button press: AP mode (pulsing led?)
* Long button press: "factory" reset -> AP mode
