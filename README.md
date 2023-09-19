Air Quality Sensor firmware for D1 Mini
=======================================

Read from a Plantower PMS5003 particulate matter sensor using a Wemos D1
Mini (or other ESP8266-based board) and report the values to an MQTT
broker and show them on a 128x32 OLED display.

On startup, the device reads its own ESP8266 chip ID and uses this as the
basis for its MQTT topics and client ID, to ensure that they will be
unique for each device.

This ID is then used to generate topics for it to report its readings.
The topics take the following form, with its unique ID substituted:

 * tele/d9616f/PM1
 * tele/d9616f/PM2.5
 * tele/d9616f/PM10
 * tele/d9616f/PPD0.3
 * tele/d9616f/PPD0.5
 * ...etc

The values are also reported in a combined format as JSON, in the same
structure used by Tasmota, at a topic similar to:

 * tele/d9616f/SENSOR

Dependencies
------------

These dependencies can all be fulfilled in the Arduino IDE using
"Sketch -> Include Library -> Manage Libraries..."

 * PubSubClient library by Nick O'Leary https://pubsubclient.knolleary.net

It also uses a version of Mariusz Kacki's "PMS" library that was forked by
SwapBap. You do not need to install this library separately, because it's
included in the project.

Connections
-----------

For particulate matter sensor:

 * PMS5003 pin 1 (VCC) to D1 Mini "5V" pin (labeled “VBUS” on my board)
 * PMS5003 pin 2 (GND) to D1 Mini "GND" pin
 * PMS5003 pin 4 (RX) to D1 Mini "D6" pin (labeled “MISO | 12” on my board)
 * PMS5003 pin 5 (TX) to D1 Mini "D7" pin (labeled “MOSI | 13” on my board)

