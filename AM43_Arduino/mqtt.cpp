#include "am43.h"

#include "mqtt.h"

MqttClass Mqtt;

const char* s_topic_status_fmt = "%s/status";
const char* s_topic_cmd_cmd_fmt = "%s/command";
const char* s_topic_pos_cmd_fmt = "%s/position/set";
const char* s_topic_pos_status_fmt = "%s/position";
const char* s_topic_json_fmt = "%s/sensor";

const char* s_status_msg = "online";
const char* s_json_fmt = "{\"batt\":%i,\"light\":%i}";

// Fingerprint if WiFiClientSecure is used for MQTT
//static const char * s_fingerprint PROGMEM = "59 3C 48 0A B1 8B 39 4E 0D 58 50 47 9A 13 55 60 CC A0 1D AF";

// Compare n first chars of payload with string, ignoring case
bool ComparePayloadN(const byte* a, const char* b, int n)
{
  for(int i = 0; i < n; ++i, ++a, ++b)
  {
    if(tolower(*a) != tolower(*b))
    {
      return false;
    }
  }
  
  return true;
}

MqttClass::MqttClass():
m_client(m_espClient),
m_lastMsg(0),
m_lastReconnectAttempt(0),
m_posLast(0),
m_batLast(0),
m_lightLast(0),
m_retain_recv(false)
{
  // Set fingerprint if WiFiClientSecure is used
  //m_espClient.setFingerprint(s_fingerprint);
}

void MqttClass::Init(char* name, char* user, char* pass, const char* server, int port, const char* topic)
{
  snprintf(m_topic_status, sizeof(m_topic_status), s_topic_status_fmt, topic);
  snprintf(m_topic_cmd_cmd, sizeof(m_topic_cmd_cmd), s_topic_cmd_cmd_fmt, topic);
  snprintf(m_topic_pos_cmd, sizeof(m_topic_pos_cmd), s_topic_pos_cmd_fmt, topic);
  snprintf(m_topic_pos_status, sizeof(m_topic_pos_status), s_topic_pos_status_fmt, topic);
  snprintf(m_topic_json, sizeof(m_topic_json), s_topic_json_fmt, topic);

  m_name = name;
  m_user = user;
  m_pass = pass;
  
  m_client.setServer(server, port);
  m_client.setCallback([this](char* topic, byte* payload, unsigned int length)
  {
    Callback(topic, payload, length);
  });
}

void MqttClass::Loop()
{
  if(!m_client.connected())
  {
    if(millis() - m_lastReconnectAttempt >= MQTT_RECONN_MS)
    {
      m_lastReconnectAttempt = millis();
      
      // Attempt to reconnect
      if(Reconnect())
      {
        m_lastReconnectAttempt = 0;
      }
    }
  }
  else
  {
    m_client.loop();

    if(millis() - m_lastMsg >= MQTT_PUBLISH_FAST_MS)
    {
      if(m_posLast != AM43.GetPosition() ||
        m_batLast != AM43.GetBatteryLevel() ||
        m_lightLast != AM43.GetLightLevel() ||
        (millis() - m_lastMsg >= MQTT_PUBLISH_MS))
      {
        UpdateServerValue();      
        m_lastMsg = millis();
      }
    }
  }
}

bool MqttClass::IsOk()
{
  return m_client.connected();
}

bool MqttClass::Reconnect()
{
  if(m_client.connect(m_name, m_user, m_pass))
  {
    m_retain_recv = false;
    m_client.subscribe(m_topic_cmd_cmd);
    m_client.subscribe(m_topic_pos_cmd);
  }
  
  return m_client.connected();
}

void MqttClass::Callback(char* topic, byte* payload, unsigned int length)
{
  if(strcmp(topic, m_topic_cmd_cmd) == 0)
  {
    if(ComparePayloadN(payload, "OPEN", length) ||
      ComparePayloadN(payload, "ON", length) ||
      ComparePayloadN(payload, "UP", length))
    {
      AM43.SendAction(AM43Class::ControlAction::Open);
    }
    else if(ComparePayloadN(payload, "CLOSE", length) ||
      ComparePayloadN(payload, "OFF", length) ||
      ComparePayloadN(payload, "DOWN", length))
    {
      AM43.SendAction(AM43Class::ControlAction::Close);
    }
    else if(ComparePayloadN(payload, "STOP", length))
    {
      AM43.SendAction(AM43Class::ControlAction::Stop);
    }
  }
  else if(strcmp(topic, m_topic_pos_cmd) == 0)
  {
    if(m_retain_recv)
    {
      AM43.SetPosition(atoi((const char*)payload));
    }
    else
    {
      m_retain_recv = true;
    }
  }
}

void MqttClass::UpdateServerValue()
{
  if(!IsOk())
  {
    return;
  }

  m_client.publish(m_topic_status, s_status_msg);

  if(AM43.IsInitialized())
  {
    m_posLast = AM43.GetPosition();
    m_batLast = AM43.GetBatteryLevel();
    m_lightLast = AM43.GetLightLevel();
    
    snprintf(m_msg, MQTT_MSG_BUFFER_SIZE, "%i", m_posLast);
    m_client.publish(m_topic_pos_status, m_msg);
    
    snprintf(m_msg, MQTT_MSG_BUFFER_SIZE, s_json_fmt, m_batLast, m_lightLast);
    m_client.publish(m_topic_json, m_msg);
  }
}
