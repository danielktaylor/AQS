/**
  Particulate matter sensor firmware for D1 Mini (ESP8266) and PMS5003

  Read from a Plantower PMS5003 particulate matter sensor using a Wemos D1
  Mini (or other ESP8266-based board) and report the values to an MQTT
  broker and to the serial console.

  External dependencies. Install using the Arduino library manager:
     "PubSubClient" by Nick O'Leary

  Bundled dependencies. No need to install separately:
     "PMS Library" by Mariusz Kacki, forked by SwapBap

  Written by Jonathan Oxer for www.superhouse.tv
    https://github.com/superhouse/AirQualitySensorD1Mini

  Inspired by https://github.com/SwapBap/WemosDustSensor/

  Copyright 2020 SuperHouse Automation Pty Ltd www.superhouse.tv
*/
#define VERSION "2.6"
/*--------------------------- Configuration ------------------------------*/
// Configuration should be done in the included file:
#include "config.h"

/*--------------------------- Libraries ----------------------------------*/
#include <Wire.h>                     // For I2C
#include <SoftwareSerial.h>           // Allows PMS to avoid the USB serial port
#include <ESP8266WiFi.h>              // ESP8266 WiFi driver
#include <PubSubClient.h>             // Required for MQTT
#include "PMS.h"                      // Particulate Matter Sensor driver (embedded)

/*--------------------------- Global Variables ---------------------------*/
// Particulate matter sensor
#define   PMS_STATE_ASLEEP        0   // Low power mode, laser and fan off
#define   PMS_STATE_WAKING_UP     1   // Laser and fan on, not ready yet
#define   PMS_STATE_READY         2   // Warmed up, ready to give data
uint8_t   g_pms_state           = PMS_STATE_WAKING_UP;
uint32_t  g_pms_state_start     = 0;  // Timestamp when PMS state last changed
uint8_t   g_pms_ae_readings_taken  = false;  // true/false: whether any readings have been taken
uint8_t   g_pms_ppd_readings_taken = false;  // true/false: whether PPD readings have been taken

uint16_t  g_pm1p0_sp_value      = 0;  // Standard Particle calibration pm1.0 reading
uint16_t  g_pm2p5_sp_value      = 0;  // Standard Particle calibration pm2.5 reading
uint16_t  g_pm10p0_sp_value     = 0;  // Standard Particle calibration pm10.0 reading

uint16_t  g_pm1p0_ae_value      = 0;  // Atmospheric Environment pm1.0 reading
uint16_t  g_pm2p5_ae_value      = 0;  // Atmospheric Environment pm2.5 reading
uint16_t  g_pm10p0_ae_value     = 0;  // Atmospheric Environment pm10.0 reading

uint32_t  g_pm0p3_ppd_value     = 0;  // Particles Per Deciliter pm0.3 reading
uint32_t  g_pm0p5_ppd_value     = 0;  // Particles Per Deciliter pm0.5 reading
uint32_t  g_pm1p0_ppd_value     = 0;  // Particles Per Deciliter pm1.0 reading
uint32_t  g_pm2p5_ppd_value     = 0;  // Particles Per Deciliter pm2.5 reading
uint32_t  g_pm5p0_ppd_value     = 0;  // Particles Per Deciliter pm5.0 reading
uint32_t  g_pm10p0_ppd_value    = 0;  // Particles Per Deciliter pm10.0 reading

uint16_t   g_epa_aqi_value        = 0;  // Air Quality Index value using EPA reporting system
uint16_t  g_us_aqi_value        = 0;  // Air Quality Index value using US reporting system

// MQTT
char g_mqtt_message_buffer[255];      // General purpose buffer for MQTT messages
char g_command_topic[50];             // MQTT topic for receiving commands

#if REPORT_MQTT_SEPARATE
char g_pm1p0_ae_mqtt_topic[50];       // MQTT topic for reporting pm1.0 AE value
char g_pm2p5_ae_mqtt_topic[50];       // MQTT topic for reporting pm2.5 AE value
char g_pm10p0_ae_mqtt_topic[50];      // MQTT topic for reporting pm10.0 AE value
char g_pm0p3_ppd_mqtt_topic[50];      // MQTT topic for reporting pm0.3 PPD value
char g_pm0p5_ppd_mqtt_topic[50];      // MQTT topic for reporting pm0.5 PPD value
char g_pm1p0_ppd_mqtt_topic[50];      // MQTT topic for reporting pm1.0 PPD value
char g_pm2p5_ppd_mqtt_topic[50];      // MQTT topic for reporting pm2.5 PPD value
char g_pm5p0_ppd_mqtt_topic[50];      // MQTT topic for reporting pm5.0 PPD value
char g_pm10p0_ppd_mqtt_topic[50];     // MQTT topic for reporting pm10.0 PPD value
char g_epa_aqi_mqtt_topic[50];         // MQTT topic for EPA-format AQI value
#endif
#if REPORT_MQTT_JSON
char g_mqtt_json_topic[50];           // MQTT topic for reporting all values using JSON
#endif

// Wifi
#define WIFI_CONNECT_INTERVAL           500  // Wait 500ms intervals for wifi connection
#define WIFI_CONNECT_MAX_ATTEMPTS        10  // Number of attempts/intervals to wait

// General
uint32_t g_device_id;                    // Unique ID from ESP chip ID
uint32_t g_device_id2;                    // Unique ID from ESP chip ID

/*--------------------------- Function Signatures ------------------------*/
void mqttCallback(char* topic, byte* payload, uint8_t length);
bool initWifi();
void reconnectMqtt();
void updatePmsReadings();
void reportToMqtt();

/* -------------------------- Resources ----------------------------------*/
#include "aqi.h"                         // Air Quality Index calculations

/*--------------------------- Instantiate Global Objects -----------------*/
// Software serial port
SoftwareSerial pmsSerial(PMS_RX_PIN, PMS_TX_PIN); // Rx pin = GPIO2 (D4 on Wemos D1 Mini)

// Particulate matter sensor
PMS pms(pmsSerial);                      // Use the software serial port for the PMS
PMS::DATA g_data;

// MQTT
WiFiClient esp_client;
PubSubClient client(esp_client);

/*--------------------------- Program ------------------------------------*/
/**
  Setup
*/
void setup()
{
  Serial.begin(SERIAL_BAUD_RATE);   // GPIO1, GPIO3 (TX/RX pin on ESP-12E Development Board)
  Serial.println();
  Serial.print("Air Quality Sensor starting up, v");
  Serial.println(VERSION);

  // Open a connection to the PMS and put it into passive mode
  pmsSerial.begin(PMS_BAUD_RATE);   // Connection for PMS5003
  pms.passiveMode();                // Tell PMS to stop sending data automatically
  delay(100);
  pms.wakeUp();                     // Tell PMS to wake up (turn on fan and laser)

  // We need a unique device ID for our MQTT client connection
  g_device_id = ESP.getChipId();  // Get the unique ID of the ESP8266 chip
  Serial.print("Device ID: ");
  Serial.println(g_device_id, HEX);

  // Set up the topics for publishing sensor readings. By inserting the unique ID,
  // the result is of the form: "tele/d9616f/AE1P0" etc
  sprintf(g_command_topic,         "cmnd/%x/COMMAND",   ESP.getChipId());  // For receiving commands
#if REPORT_MQTT_SEPARATE
  sprintf(g_pm1p0_ae_mqtt_topic,   "tele/%x/AE1P0",     ESP.getChipId());  // Data from PMS
  sprintf(g_pm2p5_ae_mqtt_topic,   "tele/%x/AE2P5",     ESP.getChipId());  // Data from PMS
  sprintf(g_pm10p0_ae_mqtt_topic,  "tele/%x/AE10P0",    ESP.getChipId());  // Data from PMS
  sprintf(g_pm0p3_ppd_mqtt_topic,  "tele/%x/PPD0P3",    ESP.getChipId());  // Data from PMS
  sprintf(g_pm0p5_ppd_mqtt_topic,  "tele/%x/PPD0P5",    ESP.getChipId());  // Data from PMS
  sprintf(g_pm1p0_ppd_mqtt_topic,  "tele/%x/PPD1P0",    ESP.getChipId());  // Data from PMS
  sprintf(g_pm2p5_ppd_mqtt_topic,  "tele/%x/PPD2P5",    ESP.getChipId());  // Data from PMS
  sprintf(g_pm5p0_ppd_mqtt_topic,  "tele/%x/PPD5P0",    ESP.getChipId());  // Data from PMS
  sprintf(g_pm10p0_ppd_mqtt_topic, "tele/%x/PPD10P0",   ESP.getChipId());  // Data from PMS
  sprintf(g_epa_aqi_mqtt_topic,    "tele/%x/AQI",       ESP.getChipId());  // Calculated value
#endif
#if REPORT_MQTT_JSON
  sprintf(g_mqtt_json_topic,       "tele/%x/SENSOR",    ESP.getChipId());  // Data from PMS
#endif

  // Report the MQTT topics to the serial console
  Serial.println(g_command_topic);          // For receiving messages
#if REPORT_MQTT_SEPARATE
  Serial.println("MQTT topics:");
  Serial.println(g_pm1p0_ae_mqtt_topic);    // From PMS
  Serial.println(g_pm2p5_ae_mqtt_topic);    // From PMS
  Serial.println(g_pm10p0_ae_mqtt_topic);   // From PMS
  Serial.println(g_pm0p3_ppd_mqtt_topic);   // From PMS
  Serial.println(g_pm0p5_ppd_mqtt_topic);   // From PMS
  Serial.println(g_pm1p0_ppd_mqtt_topic);   // From PMS
  Serial.println(g_pm2p5_ppd_mqtt_topic);   // From PMS
  Serial.println(g_pm5p0_ppd_mqtt_topic);   // From PMS
  Serial.println(g_pm10p0_ppd_mqtt_topic);  // From PMS
  Serial.println(g_epa_aqi_mqtt_topic);      // Calculated value
#endif
#if REPORT_MQTT_JSON
  Serial.println(g_mqtt_json_topic);        // From PMS
#endif

  // Connect to WiFi
  Serial.println("Connecting to WiFi");
  if (initWifi())
  {
    Serial.println("WiFi connected");
  } else {
    Serial.println("WiFi FAILED");
  }
  delay(100);

  /* Set up the MQTT client */
  client.setServer(mqtt_broker, 1883);
  client.setCallback(mqttCallback);
  client.setBufferSize(255);
}

/**
  Main loop
*/
void loop()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    if (!client.connected())
    {
      reconnectMqtt();
    }
  }
  client.loop();  // Process any outstanding MQTT messages

  updatePmsReadings();
}

/**
  Update particulate matter sensor values
*/
void updatePmsReadings()
{
  uint32_t time_now = millis();

  // Check if we've been in the sleep state for long enough
  if (PMS_STATE_ASLEEP == g_pms_state)
  {
    if (time_now - g_pms_state_start
        >= ((g_pms_report_period * 1000) - (g_pms_warmup_period * 1000)))
    {
      // It's time to wake up the sensor
      //Serial.println("Waking up sensor");
      pms.wakeUp();
      g_pms_state_start = time_now;
      g_pms_state = PMS_STATE_WAKING_UP;
    }
  }

  // Check if we've been in the waking up state for long enough
  if (PMS_STATE_WAKING_UP == g_pms_state)
  {
    if (time_now - g_pms_state_start
        >= (g_pms_warmup_period * 1000))
    {
      g_pms_state_start = time_now;
      g_pms_state = PMS_STATE_READY;
    }
  }

  // Put the most recent values into globals for reference elsewhere
  if (PMS_STATE_READY == g_pms_state)
  {
    //pms.requestRead();
    if (pms.readUntil(g_data))  // Use a blocking road to make sure we get values
    {
      g_pm1p0_sp_value   = g_data.PM_SP_UG_1_0;
      g_pm2p5_sp_value   = g_data.PM_SP_UG_2_5;
      g_pm10p0_sp_value  = g_data.PM_SP_UG_10_0;

      g_pm1p0_ae_value   = g_data.PM_AE_UG_1_0;
      g_pm2p5_ae_value   = g_data.PM_AE_UG_2_5;
      g_pm10p0_ae_value  = g_data.PM_AE_UG_10_0;

      g_pms_ae_readings_taken = true;

      // This condition below should NOT be required, but currently I get all
      // 0 values for the PPD results every second time. This check only updates
      // the global values if there is a non-zero result for any of the values:
      if (g_data.PM_TOTALPARTICLES_0_3 + g_data.PM_TOTALPARTICLES_0_5
          + g_data.PM_TOTALPARTICLES_1_0 + g_data.PM_TOTALPARTICLES_2_5
          + g_data.PM_TOTALPARTICLES_5_0 + g_data.PM_TOTALPARTICLES_10_0
          != 0)
      {
        g_pm0p3_ppd_value  = g_data.PM_TOTALPARTICLES_0_3;
        g_pm0p5_ppd_value  = g_data.PM_TOTALPARTICLES_0_5;
        g_pm1p0_ppd_value  = g_data.PM_TOTALPARTICLES_1_0;
        g_pm2p5_ppd_value  = g_data.PM_TOTALPARTICLES_2_5;
        g_pm5p0_ppd_value  = g_data.PM_TOTALPARTICLES_5_0;
        g_pm10p0_ppd_value = g_data.PM_TOTALPARTICLES_10_0;
        g_pms_ppd_readings_taken = true;
      }
      pms.sleep();

      // Calculate AQI values for the various reporting standards
      calculateEpaAqi();

      // Report the new values
      reportToMqtt();
      reportToSerial();

      g_pms_state_start = time_now;
      g_pms_state = PMS_STATE_ASLEEP;
    }
  }
}

/**
  Report the latest values to MQTT
*/
void reportToMqtt()
{
  String message_string;

#if REPORT_MQTT_SEPARATE
  if (true == g_pms_ae_readings_taken)
  {
    /* Report PM1.0 AE value */
    message_string = String(g_pm1p0_ae_value);
    message_string.toCharArray(g_mqtt_message_buffer, message_string.length() + 1);
    client.publish(g_pm1p0_ae_mqtt_topic, g_mqtt_message_buffer);

    /* Report PM2.5 AE value */
    message_string = String(g_pm2p5_ae_value);
    message_string.toCharArray(g_mqtt_message_buffer, message_string.length() + 1);
    client.publish(g_pm2p5_ae_mqtt_topic, g_mqtt_message_buffer);

    /* Report PM10.0 AE value */
    message_string = String(g_pm10p0_ae_value);
    message_string.toCharArray(g_mqtt_message_buffer, message_string.length() + 1);
    client.publish(g_pm10p0_ae_mqtt_topic, g_mqtt_message_buffer);
  }

  if (true == g_pms_ppd_readings_taken)
  {
    /* Report PM0.3 PPD value */
    message_string = String(g_pm0p3_ppd_value);
    message_string.toCharArray(g_mqtt_message_buffer, message_string.length() + 1);
    client.publish(g_pm0p3_ppd_mqtt_topic, g_mqtt_message_buffer);

    /* Report PM0.5 PPD value */
    message_string = String(g_pm0p5_ppd_value);
    message_string.toCharArray(g_mqtt_message_buffer, message_string.length() + 1);
    client.publish(g_pm0p5_ppd_mqtt_topic, g_mqtt_message_buffer);

    /* Report PM1.0 PPD value */
    message_string = String(g_pm1p0_ppd_value);
    message_string.toCharArray(g_mqtt_message_buffer, message_string.length() + 1);
    client.publish(g_pm1p0_ppd_mqtt_topic, g_mqtt_message_buffer);

    /* Report PM2.5 PPD value */
    message_string = String(g_pm2p5_ppd_value);
    message_string.toCharArray(g_mqtt_message_buffer, message_string.length() + 1);
    client.publish(g_pm2p5_ppd_mqtt_topic, g_mqtt_message_buffer);

    /* Report PM5.0 PPD value */
    message_string = String(g_pm5p0_ppd_value);
    message_string.toCharArray(g_mqtt_message_buffer, message_string.length() + 1);
    client.publish(g_pm5p0_ppd_mqtt_topic, g_mqtt_message_buffer);

    /* Report PM10.0 PPD value */
    message_string = String(g_pm10p0_ppd_value);
    message_string.toCharArray(g_mqtt_message_buffer, message_string.length() + 1);
    client.publish(g_pm10p0_ppd_mqtt_topic, g_mqtt_message_buffer);

    /* Report EPA AQI value */
    message_string = String(g_epa_aqi_value);
    message_string.toCharArray(g_mqtt_message_buffer, message_string.length() + 1);
    client.publish(g_epa_aqi_mqtt_topic, g_mqtt_message_buffer);
  }
#endif

#if REPORT_MQTT_JSON
  /* Report all values combined into one JSON message */
  // This is an example message generated by Tasmota, to match the format:
  // {"Time":"2020-02-27T03:27:22","PMS5003":{"CF1":0,"CF2.5":1,"CF10":1,"PM1":0,"PM2.5":1,"PM10":1,"PB0.3":0,"PB0.5":0,"PB1":0,"PB2.5":0,"PB5":0,"PB10":0}}
  // This is the source code from Tasmota:
  //ResponseAppend_P(PSTR(",\"PMS5003\":{\"CF1\":%d,\"CF2.5\":%d,\"CF10\":%d,\"PM1\":%d,\"PM2.5\":%d,\"PM10\":%d,\"PB0.3\":%d,\"PB0.5\":%d,\"PB1\":%d,\"PB2.5\":%d,\"PB5\":%d,\"PB10\":%d}"),
  //    pms_g_data.pm10_standard, pms_data.pm25_standard, pms_data.pm100_standard,
  //    pms_data.pm10_env, pms_data.pm25_env, pms_data.pm100_env,
  //    pms_data.particles_03um, pms_data.particles_05um, pms_data.particles_10um, pms_data.particles_25um, pms_data.particles_50um, pms_data.particles_100um);

  // Note: The PubSubClient library limits MQTT message size to 128 bytes. The long format
  // message below only works because the message buffer size has been increased to 255 bytes
  // in setup.

  // Format the message as JSON in the outgoing message buffer:
  if (true == g_pms_ppd_readings_taken)
  {
    sprintf(g_mqtt_message_buffer,  "{\"PMS5003\":{\"CF1\":%i,\"CF2.5\":%i,\"CF10\":%i,\"PM1\":%i,\"PM2.5\":%i,\"PM10\":%i,\"PB0.3\":%i,\"PB0.5\":%i,\"PB1\":%i,\"PB2.5\":%i,\"PB5\":%i,\"PB10\":%i,\"AQI\":%i}}",
            g_pm1p0_sp_value, g_pm2p5_sp_value, g_pm10p0_sp_value,
            g_pm1p0_ae_value, g_pm2p5_ae_value, g_pm10p0_ae_value,
            g_pm0p3_ppd_value, g_pm0p5_ppd_value, g_pm1p0_ppd_value,
            g_pm2p5_ppd_value, g_pm5p0_ppd_value, g_pm10p0_ppd_value,
            g_epa_aqi_value);
  } else {
    sprintf(g_mqtt_message_buffer,  "{\"PMS5003\":{\"CF1\":%i,\"CF2.5\":%i,\"CF10\":%i,\"PM1\":%i,\"PM2.5\":%i,\"PM10\":%i}}",
            g_pm1p0_sp_value, g_pm2p5_sp_value, g_pm10p0_sp_value,
            g_pm1p0_ae_value, g_pm2p5_ae_value, g_pm10p0_ae_value);
  }

  client.publish(g_mqtt_json_topic, g_mqtt_message_buffer);
#endif
}

/**
  Report the latest values to the serial console
*/
void reportToSerial()
{
  if (true == g_pms_ae_readings_taken)
  {
    /* Report PM1.0 AE value */
    Serial.print("PM1:");
    Serial.println(String(g_pm1p0_ae_value));

    /* Report PM2.5 AE value */
    Serial.print("PM2.5:");
    Serial.println(String(g_pm2p5_ae_value));

    /* Report PM10.0 AE value */
    Serial.print("PM10:");
    Serial.println(String(g_pm10p0_ae_value));
  }

  if (true == g_pms_ppd_readings_taken)
  {
    /* Report PM0.3 PPD value */
    Serial.print("PB0.3:");
    Serial.println(String(g_pm0p3_ppd_value));

    /* Report PM0.5 PPD value */
    Serial.print("PB0.5:");
    Serial.println(String(g_pm0p5_ppd_value));

    /* Report PM1.0 PPD value */
    Serial.print("PB1:");
    Serial.println(String(g_pm1p0_ppd_value));

    /* Report PM2.5 PPD value */
    Serial.print("PB2.5:");
    Serial.println(String(g_pm2p5_ppd_value));

    /* Report PM5.0 PPD value */
    Serial.print("PB5:");
    Serial.println(String(g_pm5p0_ppd_value));

    /* Report PM10.0 PPD value */
    Serial.print("PB10:");
    Serial.println(String(g_pm10p0_ppd_value));

    /* Report EPA AQI value */
    Serial.print("AQI:");
    Serial.println(String(g_epa_aqi_value));

    Serial.println(String("-------"));
  }
}

/**
  Connect to Wifi. Returns false if it can't connect.
*/
bool initWifi()
{
  // Clean up any old auto-connections
  if (WiFi.status() == WL_CONNECTED)
  {
    WiFi.disconnect();
  }
  WiFi.setAutoConnect(false);

  // RETURN: No SSID, so no wifi!
  if (sizeof(ssid) == 1)
  {
    return false;
  }

  // Connect to wifi
  WiFi.begin(ssid, password);

  // Wait for connection set amount of intervals
  int num_attempts = 0;
  while (WiFi.status() != WL_CONNECTED && num_attempts <= WIFI_CONNECT_MAX_ATTEMPTS)
  {
    delay(WIFI_CONNECT_INTERVAL);
    num_attempts++;
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    return false;
  } else {
    return true;
  }
}

/**
  Reconnect to MQTT broker, and publish a notification to the status topic
*/
void reconnectMqtt() {
  char mqtt_client_id[20];
  sprintf(mqtt_client_id, "esp8266-%x", ESP.getChipId());

  // Loop until we're reconnected
  while (!client.connected())
  {
    //Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(mqtt_client_id, mqtt_username, mqtt_password))
    {
      //Serial.println("connected");
      // Once connected, publish an announcement
      sprintf(g_mqtt_message_buffer, "Device %s starting up", mqtt_client_id);
      client.publish(status_topic, g_mqtt_message_buffer);
      // Resubscribe
      //client.subscribe(g_command_topic);
    } else {
      //Serial.print("failed, rc=");
      //Serial.print(client.state());
      //Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

/**
  This callback is invoked when an MQTT message is received. It's not important
  right now for this project because we don't receive commands via MQTT. You
  can modify this function to make the device act on commands that you send it.
*/
void mqttCallback(char* topic, byte* payload, uint8_t length)
{
  /*
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
    }
    Serial.println();
  */
}
