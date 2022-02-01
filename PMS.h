#ifndef PMS_H
#define PMS_H

/* From https://github.com/SwapBap/PMS */

#include "Stream.h"

struct pms5003data {
  uint16_t framelen;
  uint16_t pm10_standard, pm25_standard, pm100_standard;
  uint16_t pm10_env, pm25_env, pm100_env;
  uint16_t particles_03um, particles_05um, particles_10um, particles_25um, particles_50um, particles_100um;
  uint16_t unused;
  uint16_t checksum;
};

class PMS
{
  public:
    static const uint16_t SINGLE_RESPONSE_TIME = 1000;
    static const uint16_t TOTAL_RESPONSE_TIME = 1000 * 10;
    static const uint16_t STEADY_RESPONSE_TIME = 1000 * 30;

    static const uint16_t BAUD_RATE = 9600;

    struct DATA {
      // Standard Particles, CF=1
      uint16_t PM_SP_UG_1_0;
      uint16_t PM_SP_UG_2_5;
      uint16_t PM_SP_UG_10_0;

      // Atmospheric environment
      uint16_t PM_AE_UG_1_0;
      uint16_t PM_AE_UG_2_5;
      uint16_t PM_AE_UG_10_0;

      // Total particles
      uint16_t PM_TOTALPARTICLES_0_3;
      uint16_t PM_TOTALPARTICLES_0_5;
      uint16_t PM_TOTALPARTICLES_1_0;
      uint16_t PM_TOTALPARTICLES_2_5;
      uint16_t PM_TOTALPARTICLES_5_0;
      uint16_t PM_TOTALPARTICLES_10_0;
    };

    PMS(Stream&);
    void sleep();
    void wakeUp();
    void activeMode();
    void passiveMode();

    void requestRead();
    bool read(DATA& data);
    bool readUntil(DATA& data, uint16_t timeout = SINGLE_RESPONSE_TIME);

  private:
    enum STATUS { STATUS_WAITING, STATUS_OK };
    enum MODE { MODE_ACTIVE, MODE_PASSIVE };

    uint8_t _payload[24];
    Stream* _stream;
    DATA* _data;
    STATUS _status;
    MODE _mode = MODE_ACTIVE;

    uint8_t _index = 0;
    uint16_t _frameLen;
    uint16_t _checksum;
    uint16_t _calculatedChecksum;

    void loop();
};

#endif
