#include "esphome.h"

#define AM43_UPDATE_DELAY_FAST_MS 1000
#define AM43_UPDATE_DELAY_SLOW_MS 15000
#define AM43_NO_ANSWER_RESET_T 32

#define AM43_PIN_RESET 5

namespace
{
  static uint8_t s_reqPrefix[] = {0x00, 0xFF, 0x00, 0x00};
  static uint8_t s_reqHeaderPrefix[] = {0x9a};
} // namespace

class AM43Component : public Component, public Cover, public UARTDevice
{
public:
// ESPHome sensors
  Sensor* m_sensor_battery = new Sensor();
  Sensor *m_sensor_light = new Sensor();

  enum class UpdateStep
  {
    Start,
    GetSettings,
    WaitForSettings,
    GetLightLevel,
    WaitForLightLevel,
    GetBatteryLevel,
    WaitForBatteryLevel,
    Finish
  };

  struct SeasonInfo
  {
    uint8_t SeasonState;
    uint8_t LightSeasonState;
    uint8_t LightLevel;
    uint8_t LightStartHour;
    uint8_t LightStartMinute;
    uint8_t LightEndHour;
    uint8_t LightEndMinute;
  };

  struct TimingInfo
  {
  };

  enum class DeviceType
  {
    Baiye = 1,
    Chuizhi = 2,
    Juanlian = 3,
    Fengchao = 4,
    Rousha = 5,
    Xianggelila = 8,

    Unknown = 0
  };

  enum class CommandResult
  {
    Success = 0x31,
    Failure = 0xCE
  };

  enum class ContentResult
  {
    Success = 0x5A,
    Failure = 0xA5
  };

  enum class ControlAction
  {
    Close = 0xEE,
    Open = 0xDD,
    Stop = 0xCC
  };

  enum class Direction
  {
    Forward = 0x01,
    Reverse = 0x00
  };

  enum class OperationMode
  {
    Inching = 0x01,
    Continuous = 0x00
  };

  // Content:
  // Failure = 0xA5
  // Succese = 0x5A
  // LimitSetExit = 0x5C
  // LimitSetFailure = 0xB5
  // LimitSetSuccess = 0x5B
  // LimitSetLogin = 0x
  // LimitSetTimeout = 0x
  // ResetSuccess = 0xC5
  // Season_AllClose = 0x00
  // Season_Open2Open_Close2Close = 0x10
  // Season_Open2Open_Close2Open = 0xA5
  // Season_Open2Stop_Close2Open = 0xA5

  enum class Command
  {
    // Command      // Value    // Data payload
    SendAction = 0x0A,  // ControlAction action
    SetPosition = 0x0D, // byte position
    SetSettings = 0x11, // AM43Settings settings

    ResetLimits = 0x22, // byte[] { 0, 0, 1 }
    SetTime = 0x14,     // AM43Time time
    SetSeason = 0x16,   // SeasonInfo summer, SeasonInfo winter
    SetTiming = 0x15,   // TimingInfo

    GetSettings = 0xA7,     // byte 1
    GetLightLevel = 0xAA,   // byte 1
    GetBatteryLevel = 0xA2, // byte 1

    // Responce only commands
    Verification = 0x00, // ContentResult, CommandResult
    GetTiming = 0xA8,    // TimingInfo
    GetSeason = 0xA9,    // SeasonInfo summer, SeasonInfo winter
    GetPosition = 0xA1,
    GetSpeed = 0xA3,

    // Unknown, might be only used for BLE module
    Password = 0x17,
    PasswordChange = 0x18,
    SetName = 0x35, // char* name
  };

  AM43Component(UARTComponent *parent) : UARTDevice(parent),
                                         m_update_step(UpdateStep::Start),
                                         m_update_delay(AM43_UPDATE_DELAY_FAST_MS),
                                         m_last_update(0),
                                         m_no_answer_reset_counter(0),
                                         m_update_ticks(0),
                                         m_direction(Direction::Forward),
                                         m_operationMode(OperationMode::Inching),
                                         m_deviceSpeed(0),
                                         m_deviceLength(0),
                                         m_deviceDiameter(0),
                                         m_deviceType(DeviceType::Unknown),
                                         m_topLimitSet(true),
                                         m_bottomLimitSet(true),
                                         m_hasLightSensor(true),
                                         m_position(0),
                                         m_lightLevel(0),
                                         m_batteryLevel(0),
                                         m_initialized(false)
  {
  }

  void PrintData()
  {
    ESP_LOGD("am43", "============================================");

    if (m_direction == Direction::Forward)
    {
      ESP_LOGD("am43", "Direction: Forward");
    }
    else if (m_direction == Direction::Reverse)
    {
      ESP_LOGD("am43", "Direction: Reverse");
    }
    else
    {
      ESP_LOGD("am43", "Direction: Unknown");
    }

    if (m_operationMode == OperationMode::Inching)
    {
      ESP_LOGD("am43", "OperationMode: Inching");
    }
    else if (m_operationMode == OperationMode::Continuous)
    {
      ESP_LOGD("am43", "OperationMode: Continuous");
    }
    else
    {
      ESP_LOGD("am43", "OperationMode: Unknown");
    }

    ESP_LOGD("am43", "Speed: %i", m_deviceSpeed);
    ESP_LOGD("am43", "Length: %i", m_deviceLength);
    ESP_LOGD("am43", "Diameter: %i", m_deviceDiameter);

    switch (m_deviceType)
    {
    case DeviceType::Baiye:
      ESP_LOGD("am43", "Type: Baiye");
      break;
    case DeviceType::Chuizhi:
      ESP_LOGD("am43", "Type: Chuizhi");
      break;
    case DeviceType::Juanlian:
      ESP_LOGD("am43", "Type: Juanlian");
      break;
    case DeviceType::Fengchao:
      ESP_LOGD("am43", "Type: Fengchao");
      break;
    case DeviceType::Rousha:
      ESP_LOGD("am43", "Type: Rousha");
      break;
    case DeviceType::Xianggelila:
      ESP_LOGD("am43", "Type: Xianggelila");
      break;
    default:
      ESP_LOGD("am43", "Type: %i", (int)m_deviceType);
    }

    ESP_LOGD("am43", "TopLimit: %i", (int)m_topLimitSet);
    ESP_LOGD("am43", "BottomLimit: %i", (int)m_bottomLimitSet);
    ESP_LOGD("am43", "HasLightSensor: %i", (int)m_hasLightSensor);

    ESP_LOGD("am43", "Position: %i", m_position);
    ESP_LOGD("am43", "LightLevel: %i", m_lightLevel);
    ESP_LOGD("am43", "BatteryLevel: %i", m_batteryLevel);

    SeasonInfo *si[] = {&m_summerSeason, &m_winterSeason};
    for (SeasonInfo *s : si)
    {
      String season;
      season += s->SeasonState ? String("On, ") : String("Off, ");
      season += String(s->LightSeasonState) + String(", ");
      season += String(s->LightLevel) + String(", ");
      season += String(s->LightStartHour) + String(":") + String(s->LightStartMinute) + String(", ");
      season += String(s->LightEndHour) + String(":") + String(s->LightEndMinute);
      ESP_LOGD("am43", "Season: %s", season.c_str());
    }
  }

  // ESPHome API
  void setup() override
  {
    pinMode(AM43_PIN_RESET, INPUT);
  }

  void loop() override
  {
    if (available() > 1) // At least we have header prefix and command
    {
      int buff_offset = 0;
      while (available())
      {
        m_aux_recv_buff[buff_offset++] = read();
        delay(100);
      }

      int resp_end = 0;
      uint8_t *buff = m_aux_recv_buff;
      int buff_n = buff_offset;
      do
      {
        resp_end = HandleResponse(buff, buff_n);
        buff += resp_end;
        buff_n -= resp_end;
      } while (resp_end > 0 && buff_n > 0);

      PrintData();
    }

    if (millis() - m_last_update >= m_update_delay)
    {
      Update();

      m_last_update = millis();
    }
  }

  CoverTraits get_traits() override
  {
    auto traits = CoverTraits();
    traits.set_is_assumed_state(false);
    traits.set_supports_position(true);
    traits.set_supports_tilt(false);
    return traits;
  }

  void control(const CoverCall &call) override
  {
    // This will be called every time the user requests a state change.
    if (call.get_position().has_value())
    {
      float pos = *call.get_position();
      // Write pos (range 0-1) to cover
      SetPosition(100 - (uint8_t)round(pos * 100));
    }

    if (call.get_stop())
    {
      // User requested cover stop
      SendAction(ControlAction::Stop);
    }
  }

  void Update()
  {
    ++m_no_answer_reset_counter;
    if (m_no_answer_reset_counter > AM43_NO_ANSWER_RESET_T)
    {
      DeviceReset();
      m_no_answer_reset_counter = 0;
    }

    if (++m_update_ticks > 10 || m_update_step == UpdateStep::Start)
    {
      m_update_delay = AM43_UPDATE_DELAY_FAST_MS;
      m_update_step = UpdateStep::GetSettings;
      m_update_ticks = 0;
    }
    else if (m_update_step == UpdateStep::Finish)
    {
      m_update_delay = AM43_UPDATE_DELAY_SLOW_MS;
      m_update_step = UpdateStep::Start;
      m_initialized = true;
      m_update_ticks = 0;
    }

    switch (m_update_step)
    {
    case UpdateStep::GetSettings:
    {
      m_update_step = UpdateStep::WaitForSettings;
      m_update_ticks = 0;
      DeviceGetSettings();
      break;
    }
    case UpdateStep::GetLightLevel:
    {
      m_update_step = UpdateStep::WaitForLightLevel;
      m_update_ticks = 0;
      DeviceGetLightLevel();
      break;
    }
    case UpdateStep::GetBatteryLevel:
    {
      m_update_step = UpdateStep::WaitForBatteryLevel;
      m_update_ticks = 0;
      DeviceGetBatteryLevel();
      break;
    }
    }
  }

  void SendAction(ControlAction action)
  {
    const int len = BuildRequest(m_aux_buff, sizeof(m_aux_buff), Command::SendAction, static_cast<uint8_t>(action));
    SendRequest(m_aux_buff, len);

    // Update position imadeitely to unlock home automation options
    // e.g. Home Assistant locks Close command if position is 100% or Open command if position is 0%
    switch (action)
    {
    case ControlAction::Close:
    {
      ++m_position;

      break;
    }
    case ControlAction::Open:
    {
      --m_position;

      break;
    }
    }

    m_position = constrain(m_position, 0, 100);
    position = (float)(100 - m_position) / 100.0f;
    publish_state();
  }

  void SetPosition(uint8_t position_percent)
  {
    m_position = constrain(position_percent, 0, 100);
    position = (float)(100 - m_position) / 100.0f;
    publish_state();

    const int len = BuildRequest(m_aux_buff, sizeof(m_aux_buff), Command::SetPosition, m_position);
    SendRequest(m_aux_buff, len);
  }

  uint8_t GetPosition() const
  {
    return m_position;
  }

  uint8_t GetBatteryLevel() const
  {
    return m_batteryLevel;
  }

  uint8_t GetLightLevel() const
  {
    return m_lightLevel;
  }

  bool IsInitialized() const
  {
    return m_initialized;
  }

protected:
  void DeviceReset()
  {
    // Switch pin mode to output and pull it low
    digitalWrite(AM43_PIN_RESET, LOW);
    pinMode(AM43_PIN_RESET, OUTPUT);

    delay(100);

    pinMode(AM43_PIN_RESET, INPUT);

    delay(100);
  }
  //void DeviceResetLimits();
  void DeviceSetSettings()
  {
    uint8_t data[64];
    const int data_n = BuildSettingsData(data, sizeof(data));
    const int len = BuildRequest(m_aux_buff, sizeof(m_aux_buff), Command::SetName, data, data_n);
    SendRequest(m_aux_buff, len);
  }
  //void DeviceSetTime();
  //void DeviceSetPassword();
  //void DeviceSetPasswordChange();
  void DeviceSetSeason();
  //void DeviceSetTiming();

  void DeviceGetSettings()
  {
    const int len = BuildRequest(m_aux_buff, sizeof(m_aux_buff), Command::GetSettings, 1);
    SendRequest(m_aux_buff, len);
  }
  void DeviceGetLightLevel()
  {
    const int len = BuildRequest(m_aux_buff, sizeof(m_aux_buff), Command::GetLightLevel, 1);
    SendRequest(m_aux_buff, len);
  }
  void DeviceGetBatteryLevel()
  {
    const int len = BuildRequest(m_aux_buff, sizeof(m_aux_buff), Command::GetBatteryLevel, 1);
    SendRequest(m_aux_buff, len);
  }

  void SendRequest(const uint8_t *buff, unsigned int buff_n)
  {
    if (buff_n > 0)
    {
      ESP_LOGD("am43", "SendRequest:");
      for (int i = 0; i < buff_n; ++i)
      {
        ESP_LOGD("am43", "0x%x", buff[i]);
      }

      write_array(buff, buff_n);
    }
  }
  // handle response from AM43 device
  // Returns response end offset
  int HandleResponse(const uint8_t *buff, unsigned int buff_n)
  {
    if (buff_n > 0)
    {
      ESP_LOGD("am43", "Processing:");
      for (int i = 0; i < buff_n; ++i)
      {
        ESP_LOGD("am43", "0x%x", buff[i]);
      }
    }

    int response_begin = -1;
    int req_herader_prefix_offset = 0;

    for (int i = 0; i < buff_n; ++i)
    {
      if (buff[i] == s_reqHeaderPrefix[req_herader_prefix_offset])
      {
        ++req_herader_prefix_offset;
        if (req_herader_prefix_offset == sizeof(s_reqHeaderPrefix))
        {
          response_begin = i - sizeof(s_reqHeaderPrefix) + 1;
          break;
        }
      }
      else
      {
        req_herader_prefix_offset = 0;
      }
    }

    if (response_begin == -1)
    {
      ESP_LOGD("am43", "Header not found");
      return 0;
    }

    m_no_answer_reset_counter = 0;

    int response_offset = response_begin + sizeof(s_reqHeaderPrefix);

    Command response_cmd = static_cast<Command>(buff[response_offset++]);
    uint8_t response_len = buff[response_offset++];
    uint8_t response_checksum = buff[response_offset + response_len];

    const int response_size = sizeof(s_reqHeaderPrefix) + response_len + 3;
    const int response_end = response_begin + response_size;

    uint8_t response_calculated_checksum = 0;
    for (int i = response_begin; i < response_end - 1; ++i)
    {
      response_calculated_checksum ^= buff[i];
    }

    const uint8_t *data = buff + response_offset;
    if (response_calculated_checksum == response_checksum)
    {
      switch (response_cmd)
      {
      case Command::GetSettings:
      {
        if (response_len >= 7)
        {
          uint8_t dat = data[0];
          m_direction = static_cast<Direction>(dat & 1);
          m_operationMode = static_cast<OperationMode>((dat >> 1) & 1);

          m_topLimitSet = (dat & 4) > 0;
          m_bottomLimitSet = (dat & 8) > 0;
          m_hasLightSensor = (dat & 16) > 0;

          m_deviceSpeed = data[1];
          m_position = data[2];
          
          m_deviceLength = (data[3] << 8) | data[4];
          m_deviceDiameter = data[5];

          m_deviceType = static_cast<DeviceType>(abs(data[6] >> 4));

          position = (float)(100 - m_position) / 100.0f;
          publish_state();
        }
        break;
      }
      case Command::GetLightLevel:
      {
        if (response_len >= 2)
        {
          m_lightLevel = data[1];
          m_sensor_light->publish_state(m_lightLevel);
        }

        if (m_update_step == UpdateStep::WaitForLightLevel)
        {
          m_update_step = UpdateStep::GetBatteryLevel;
        }

        break;
      }
      case Command::GetPosition:
      {
        if (response_len >= 2)
        {
          m_position = data[1];
          position = (float)(100 - m_position) / 100.0f;
          publish_state();
        }
        break;
      }
      case Command::GetBatteryLevel:
      {
        if (response_len >= 5)
        {
          m_batteryLevel = data[4];
          m_sensor_battery->publish_state(m_batteryLevel);
        }

        if (m_update_step == UpdateStep::WaitForBatteryLevel)
        {
          m_update_step = UpdateStep::Finish;
        }

        break;
      }
      case Command::GetSpeed:
      {
        if (response_len >= 2)
        {
          m_deviceSpeed = data[1];

          uint8_t dat = data[0];
          m_direction = static_cast<Direction>((dat >> 1) & 1);
          m_operationMode = static_cast<OperationMode>((dat >> 2) & 1);
          m_hasLightSensor = ((dat >> 3) & 1) > 0;
        }
        break;
      }
      case Command::GetSeason:
      {
        if (response_len >= sizeof(SeasonInfo) * 2 + 2)
        {
          memcpy(&m_summerSeason, data + 1, sizeof(SeasonInfo));
          memcpy(&m_winterSeason, data + sizeof(SeasonInfo) + 2, sizeof(SeasonInfo));
        }

        if (m_update_step == UpdateStep::WaitForSettings)
        {
          m_update_step = UpdateStep::GetLightLevel;
        }

        break;
      }
      }
    }

    ESP_LOGD("am43", "Response header found");
    ESP_LOGD("am43", "CMD: 0x%x", (int)response_cmd);
    ESP_LOGD("am43", "LEN: 0x%i", response_len);
    for (int i = 0; i < response_len; ++i)
    {
      ESP_LOGD("am43", "DAT[%i]: 0x%x", i, buff[response_offset + i]);
    }
    ESP_LOGD("am43", "CHK: 0x%x", response_checksum);
    ESP_LOGD("am43", "CCC: 0x%x", response_calculated_checksum);

    return response_end;
  }

  // Build data payload for device SetSettings request
  // Returns size of payload in bytes
  int BuildSettingsData(uint8_t *buff, uint8_t buff_n)
  {
    if (buff_n < 6)
    {
      return 0;
    }

    uint8_t dataHead = (static_cast<uint8_t>(m_direction) & 1) << 1;
    dataHead |= (static_cast<uint8_t>(m_operationMode) & 1) << 2;
    dataHead |= static_cast<uint8_t>(m_deviceType) << 4;

    int buff_offset = 0;
    buff[buff_offset++] = dataHead;
    buff[buff_offset++] = m_deviceSpeed;
    buff[buff_offset++] = 0;

    buff[buff_offset++] = static_cast<uint8_t>((m_deviceLength & 0xFF00) >> 8);
    buff[buff_offset++] = static_cast<uint8_t>(m_deviceLength & 0xFF);
    buff[buff_offset++] = m_deviceDiameter;

    return buff_offset;
  }

  // Build request and store it to buff
  // Returns size of request in bytes
  int BuildRequest(uint8_t *buff, unsigned int buff_n, Command cmd, uint8_t data)
  {
    return BuildRequest(buff, buff_n, cmd, &data, 1);
  }

  // Build request and store it to buff
  // Returns size of request in bytes
  int BuildRequest(uint8_t *buff, unsigned int buff_n, Command cmd, const uint8_t *data, uint8_t data_n)
  {
    if (buff == nullptr || buff_n < sizeof(s_reqPrefix) + sizeof(s_reqHeaderPrefix) + data_n + 3)
    {
      return 0;
    }

    // AM43 Request example
    // 00 ff 00 00 9a 17 02 22 b8 15
    // 0x00, 0xFF, 0x00, 0x00   REQUEST PREFIX
    // 0x9a                     HEADER PREFIX
    // 0x00                     HEADER(CMD)
    // 0x01                     DATA LENGTH
    // 0x00                     DATA
    // 0x00                     REQUEST CHECKSUM (HEADER PREFIX xor HEADER xor DATA LENGTH xor DATA)

    int buff_offset = 0;

    // Request prefix
    memcpy(buff + buff_offset, s_reqPrefix, sizeof(s_reqPrefix));
    buff_offset += sizeof(s_reqPrefix);

    // Header
    // Header prefix
    memcpy(buff + buff_offset, s_reqHeaderPrefix, sizeof(s_reqHeaderPrefix));
    buff_offset += sizeof(s_reqHeaderPrefix);
    // Header(command)
    buff[buff_offset++] = static_cast<uint8_t>(cmd);

    // Data
    // Data length
    buff[buff_offset++] = data_n;
    for (int i = 0; i < data_n; ++i)
    {
      buff[buff_offset++] = data[i];
    }

    uint8_t checksum = 0;
    for (int i = sizeof(s_reqPrefix); i < buff_offset; ++i)
    {
      checksum ^= buff[i];
    }
    buff[buff_offset++] = checksum;

    return buff_offset;
  }

  UpdateStep m_update_step;
  unsigned long m_last_update;
  unsigned long m_update_delay;
  int m_no_answer_reset_counter;
  int m_update_ticks;

  Direction m_direction;
  OperationMode m_operationMode;
  uint8_t m_deviceSpeed;    // RPM
  uint16_t m_deviceLength;  // mm
  uint8_t m_deviceDiameter; // mm
  DeviceType m_deviceType;
  bool m_topLimitSet;
  bool m_bottomLimitSet;
  bool m_hasLightSensor;
  uint8_t m_position;
  uint8_t m_lightLevel;
  uint8_t m_batteryLevel;
  SeasonInfo m_summerSeason;
  SeasonInfo m_winterSeason;

private:
  byte m_aux_recv_buff[256];
  byte m_aux_buff[128];
  bool m_initialized;
};