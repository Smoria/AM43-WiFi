#ifndef MQTT_H
#define MQTT_H

#include <PubSubClient.h>
#include <WiFiClient.h>

#define MQTT_MSG_BUFFER_SIZE  (64)   // MQTT message buffer size
#define MQTT_RECONN_MS        5000
#define MQTT_PUBLISH_FAST_MS  500
#define MQTT_PUBLISH_MS       60000

class MqttClass
{
  public:
    MqttClass();

    void Init(char* name, char* user, char* pass, const char* server, int port, const char* topic);
    void Loop();
    void UpdateServerValue();

    bool IsOk();

  private:
    bool Reconnect();
    void Callback(char* topic, byte* payload, unsigned int length);

    //WiFiClientSecure m_espClient;
    WiFiClient m_espClient;
    PubSubClient m_client;
    char* m_name;
    char* m_user;
    char* m_pass;
    unsigned long m_lastMsg;
    unsigned long m_lastReconnectAttempt;
    char m_msg[MQTT_MSG_BUFFER_SIZE];
    
    // Must be at least mqtt_topic size + sub topic size
    char m_topic_status[48];
    char m_topic_cmd_cmd[48];
    char m_topic_pos_cmd[48];
    char m_topic_pos_status[48];
    char m_topic_json[48];

    // Last AM43 status
    uint8_t m_posLast;
    uint8_t m_batLast;
    uint8_t m_lightLast;
};

extern MqttClass Mqtt;

#endif
