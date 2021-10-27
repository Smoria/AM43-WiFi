#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <FS.h>
#include <ESP8266mDNS.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Ticker.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <WiFiUdp.h>

#include "mqtt.h"
#include "am43.h"

#define PIN_LED           2
#define CONFIG_VER        1       // Change this if there is changes in parameters and config must be refreshed

Ticker ticker;
WiFiManager wifi_manager;
  
const char* config_filename = "/config.json";

// !!!!!!!!!!!!!!!!!!!!!!! CHANGE THIS PASSWORD !!!!!!!!!!!!!!!!!!!!!!!
// !!!!!!!!!!!!!!!!!!!!!!! CHANGE THIS PASSWORD !!!!!!!!!!!!!!!!!!!!!!!
const char* passwd = " bigjigglypanda";
const char* esp_ssid_fmt = "ESP-AM43-%s";
char esp_ssid[40] = {0};

char mqtt_server[40] = {0};
char mqtt_port[6] = "8080";
char mqtt_topic[32] = "am43-default";
char mqtt_user[16] = {0};
char mqtt_pass[24] = {0};

bool should_save_settings = false;
void SaveConfigCallback()
{
  should_save_settings = true;
}

void Tick()
{
  int state = digitalRead(PIN_LED);
  digitalWrite(PIN_LED, !state);
}

void ConfigModeCallback(WiFiManager *myWiFiManager)
{
  // Start blinker
  ticker.attach(0.2, Tick);
}

void setup()
{
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
  
  Serial.begin(AM43_BAUD);

  // Try to load config
  bool config_ok = LoadSettings();

  wifi_manager.setDebugOutput(false);
  wifi_manager.setSaveConfigCallback(SaveConfigCallback);
  wifi_manager.setAPCallback(ConfigModeCallback);
  wifi_manager.setTimeout(180);

  // Add custom parameters
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);
  WiFiManagerParameter custom_mqtt_topic("topic", "mqtt topic", mqtt_topic, 32);
  WiFiManagerParameter custom_mqtt_user("user", "mqtt user", mqtt_user, 16);
  WiFiManagerParameter custom_mqtt_pass("pass", "mqtt pass", mqtt_pass, 24);
  wifi_manager.addParameter(&custom_mqtt_server);
  wifi_manager.addParameter(&custom_mqtt_port);
  wifi_manager.addParameter(&custom_mqtt_user);
  wifi_manager.addParameter(&custom_mqtt_pass);
  wifi_manager.addParameter(&custom_mqtt_topic);
  
  // If wifi fails to connect it will create an esp_ssid access point
  // Fill esp_ssid if it's not set with esp_ssid_fmt and MAC address
  if(esp_ssid[0] == 0)
  {
    snprintf(esp_ssid, sizeof(esp_ssid), esp_ssid_fmt, WiFi.macAddress().c_str());
  }

  bool wifi_result = false;
  if(config_ok)
  {
    // Try to connect and start config portal if failed
    wifi_result = wifi_manager.autoConnect(esp_ssid, passwd);
  }
  else
  {
    // Start config portal if there is an error with config
    wifi_result = wifi_manager.startConfigPortal(esp_ssid, passwd);
  }

  // Reset if exceeded config timeout
  if(!wifi_result)
  {
    ESP.reset();
    delay(10000);
  }

  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_topic, custom_mqtt_topic.getValue());
  strcpy(mqtt_user, custom_mqtt_user.getValue());
  strcpy(mqtt_pass, custom_mqtt_pass.getValue());
  if(should_save_settings)
  {
    SaveSettings();
  }

  ticker.detach();
  
  // Wait for wifi connection
  while(WiFi.status() != WL_CONNECTED)
  {
    delay(500);
  }

  // Setup OTA
  ArduinoOTA.setHostname(esp_ssid);
  ArduinoOTA.setPassword(passwd);
  ArduinoOTA.begin();

  // Init AM43 comms
  AM43.Init(&Serial);

  Mqtt.Init(esp_ssid, mqtt_user, mqtt_pass, mqtt_server, atoi(mqtt_port), mqtt_topic);
  
  digitalWrite(PIN_LED, HIGH);
}

void loop()
{
  ArduinoOTA.handle();

  AM43.Loop();

  Mqtt.Loop();
}

bool LoadSettings()
{
  if(ESP.getFlashChipRealSize() == ESP.getFlashChipSize() && SPIFFS.begin())
  {
    if(SPIFFS.exists(config_filename))
    {
      // Parse json config file
      File jsonFile = SPIFFS.open(config_filename, "r");
      if(jsonFile)
      {
        size_t size = jsonFile.size();
        
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> jsonBuf(new char[size]);
        jsonFile.readBytes(jsonBuf.get(), size);
        DynamicJsonDocument jsonDoc(1024);
        DeserializationError error = deserializeJson(jsonDoc, jsonBuf.get());
        if(!error)
        {
          int config_ver = jsonDoc["config_ver"];
          
          strcpy(mqtt_server, jsonDoc["mqtt_server"]);
          strcpy(mqtt_port, jsonDoc["mqtt_port"]);
            
          if(config_ver == CONFIG_VER)
          {
            strcpy(mqtt_topic, jsonDoc["mqtt_topic"]);
            strcpy(mqtt_user, jsonDoc["mqtt_user"]);
            strcpy(mqtt_pass, jsonDoc["mqtt_pass"]);
          }
          
          jsonFile.close();

          return config_ver == CONFIG_VER;
        }
        
        jsonFile.close();
      }
    }
  }

  return false;
}

void SaveSettings()
{
  if(ESP.getFlashChipRealSize() == ESP.getFlashChipSize() && SPIFFS.begin())
  {   
    DynamicJsonDocument jsonDoc(1024);
    jsonDoc["mqtt_server"] = mqtt_server;
    jsonDoc["mqtt_port"] = mqtt_port;
    jsonDoc["mqtt_user"] = mqtt_user;
    jsonDoc["mqtt_pass"] = mqtt_pass;
    jsonDoc["mqtt_topic"] = mqtt_topic;
    jsonDoc["config_ver"] = CONFIG_VER;
      
    File configFile = SPIFFS.open(config_filename, "w");
    if(configFile)
    {
      serializeJson(jsonDoc, configFile);
      should_save_settings = false;
      configFile.close();
    }
  }
}
