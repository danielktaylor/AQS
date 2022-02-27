/**
   Use the latest sensor readings to calculate the Air Quality
   Index value using the EPA AQI reporting method.
*/

// EPA AQI breakpoints for PM2.5
//
// We don't use the PM10 measurement because the PMS5003
// doesn't accurately measure that particle size.

const struct {
  float pMin;
  float pRange;
  uint16_t aqMin;
  uint16_t aqRange;
} AQITable25[] = {
      {  0.0,    12.0,   0, 50},
      { 12.1,    23.4,  51, 49},
      { 35.5,    24.3, 101, 49},
      { 55.5,    84.9, 151, 49},
      {150.5,    99.9, 201, 99},
      {250.5,    99.9, 301, 99},
      {350.5,   249.9, 401, 99},
      {500.5, 99999.9, 501, 498}
};
static const int AQITableLength25 = sizeof(AQITable25)/sizeof(AQITable25[0]);

uint16_t derivedAQI25(uint16_t reading) {
  int i;
  for (i = 0; i < AQITableLength25; i++) {
    if (reading < AQITable25[i].pMin) break;
  }
  i--;
  float aqi = ((reading -  AQITable25[i].pMin)*(AQITable25[i].aqRange))/AQITable25[i].pRange + AQITable25[i].aqMin;
  return (uint16_t)aqi;
}

void calculateEpaAqi()
{
  uint16_t pm2p5_aqi = derivedAQI25(g_pm2p5_sp_value);
  g_epa_aqi_value = pm2p5_aqi;
}
