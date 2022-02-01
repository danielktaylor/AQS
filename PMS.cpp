#include "Arduino.h"
#include "PMS.h"

/* From https://github.com/SwapBap/PMS */

PMS::PMS(Stream& stream)
{
  this->_stream = &stream;
}

// Standby mode. For low power consumption and prolong the life of the sensor.
void PMS::sleep()
{
  uint8_t command[] = { 0x42, 0x4D, 0xE4, 0x00, 0x00, 0x01, 0x73 };
  _stream->write(command, sizeof(command));
}

// Operating mode. Stable data should be got at least 30 seconds after the sensor wakeup from the sleep mode because of the fan's performance.
void PMS::wakeUp()
{
  uint8_t command[] = { 0x42, 0x4D, 0xE4, 0x00, 0x01, 0x01, 0x74 };
  _stream->write(command, sizeof(command));
}

// Active mode. Default mode after power up. In this mode sensor would send serial data to the host automatically.
void PMS::activeMode()
{
  uint8_t command[] = { 0x42, 0x4D, 0xE1, 0x00, 0x01, 0x01, 0x71 };
  _stream->write(command, sizeof(command));
  _mode = MODE_ACTIVE;
}

// Passive mode. In this mode sensor would send serial data to the host only for request.
void PMS::passiveMode()
{
  uint8_t command[] = { 0x42, 0x4D, 0xE1, 0x00, 0x00, 0x01, 0x70 };
  _stream->write(command, sizeof(command));
  _mode = MODE_PASSIVE;
}

// Request read in Passive Mode.
void PMS::requestRead()
{
  if (_mode == MODE_PASSIVE)
  {
    uint8_t command[] = { 0x42, 0x4D, 0xE2, 0x00, 0x00, 0x01, 0x71 };
    _stream->write(command, sizeof(command));
  }
}

// Non-blocking function for parse response.
bool PMS::read(DATA& data)
{
  _data = &data;
  loop();

  return _status == STATUS_OK;
}

// Blocking function for parse response. Default timeout is 1s.
bool PMS::readUntil(DATA& data, uint16_t timeout)
{
  _data = &data;
  uint32_t start = millis();
  do
  {
    loop();
    if (_status == STATUS_OK) break;
  } while (millis() - start < timeout);

  return _status == STATUS_OK;
}

void PMS::loop()
{
  _status = STATUS_WAITING;
  if (_stream->available())
  {
    // Read a byte at a time until we get to the special '0x42' start-byte
    if (_stream->peek() != 0x42) {
      _stream->read();
      return;
    }

    // Now read all 32 bytes
    if (_stream->available() < 32) {
      return;
    }

    uint8_t buffer[32];
    uint16_t sum = 0;
    _stream->readBytes(buffer, 32);

    // get checksum ready
    for (uint8_t i=0; i<30; i++) {
      sum += buffer[i];
    }

    // The data comes in endian'd, this solves it so it works on all platforms
    uint16_t buffer_u16[15];
    for (uint8_t i=0; i<15; i++) {
      buffer_u16[i] = buffer[2 + i*2 + 1];
      buffer_u16[i] += (buffer[2 + i*2] << 8);
    }

    // put it into a nice struct :)
    struct pms5003data sdata;
    memcpy((void *)&sdata, (void *)buffer_u16, 30);

    if (sum != sdata.checksum) {
      return;
    }
    // success!
    _status = STATUS_OK;

    // Standard Particles, CF=1.
    _data->PM_SP_UG_1_0 = sdata.pm10_standard;
    _data->PM_SP_UG_2_5 = sdata.pm25_standard;
    _data->PM_SP_UG_10_0 = sdata.pm100_standard;

    // Atmospheric Environment.
    _data->PM_AE_UG_1_0 = sdata.pm10_env;
    _data->PM_AE_UG_2_5 = sdata.pm25_env;
    _data->PM_AE_UG_10_0 = sdata.pm100_env;

    // Total particles
    _data->PM_TOTALPARTICLES_0_3 = sdata.particles_03um;
    _data->PM_TOTALPARTICLES_0_5 = sdata.particles_05um;
    _data->PM_TOTALPARTICLES_1_0 = sdata.particles_10um;
    _data->PM_TOTALPARTICLES_2_5 = sdata.particles_25um;
    _data->PM_TOTALPARTICLES_5_0 = sdata.particles_50um;
    _data->PM_TOTALPARTICLES_10_0 = sdata.particles_100um;
  }
}
