#ifndef WEBDATA_H
#define WEBDATA_H

// contains data structures and variables that are used in main and wlan modules

#include <stdint.h>
#include <string>

typedef struct {
  uint8_t ppm;
  uint8_t spO2;
  uint8_t deziPI;
} f7Data_t;

typedef struct webData {
  const char *firmware;
  char f7Device[18];
  bool f7Connected;
  f7Data_t f7Data;
} webData_t;

extern webData_t webData;

#endif