#ifndef WLAN_H
#define WLAN_h

// Call this to start wlan task
// It will connect to preconfigured wlan, get current time and log to syslog
// It will also start a webserver for firmware updates and sensor values
// Finally it will post sensor data to a preconfigured influx database
void startWlan();

#endif