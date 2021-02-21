#ifndef AM43_H
#define AM43_H

#include <Arduino.h>

#define AM43_BAUD                 19200
#define AM43_UPDATE_DELAY_FAST_MS 1000
#define AM43_UPDATE_DELAY_SLOW_MS 15000
#define AM43_NO_ANSWER_RESET_T    32

#define AM43_PIN_RESET            5

//#define WEB_SOCKET_DEBUG

class AM43Class
{
public:
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
    Open  = 0xDD,
    Stop  = 0xCC
  };

  enum class Direction
  {
    Forward = 0x01,
    Reverse = 0x00
  };

  enum class OperationMode
  {
    Inching     = 0x01,
    Continuous  = 0x00
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
    SendAction      = 0x0A,     // ControlAction action
    SetPosition     = 0x0D,     // byte position
    SetSettings     = 0x11,     // AM43Settings settings
    
    ResetLimits     = 0x22,     // byte[] { 0, 0, 1 }    
    SetTime         = 0x14,     // AM43Time time
    SetSeason       = 0x16,     // SeasonInfo summer, SeasonInfo winter
    SetTiming       = 0x15,     // TimingInfo
    
    GetSettings     = 0xA7,     // byte 1
    GetLightLevel   = 0xAA,     // byte 1
    GetBatteryLevel = 0xA2,     // byte 1

    // Responce only commands
    Verification    = 0x00,     // ContentResult, CommandResult
    GetTiming       = 0xA8,     // TimingInfo
    GetSeason       = 0xA9,     // SeasonInfo summer, SeasonInfo winter
    GetPosition     = 0xA1,
    GetSpeed        = 0xA3,

    // Unknown, might be only used for BLE module
    Password        = 0x17,
    PasswordChange  = 0x18,
    SetName         = 0x35,     // char* name
  };
  
  AM43Class();

  void Init(Stream* output_stream);
  void Loop();

  void Update();
  
  void SendAction(ControlAction action);
 
  void SetPosition(uint8_t position_percent);
  uint8_t GetPosition() const { return m_position; }
  uint8_t GetBatteryLevel() const { return m_batteryLevel; }
  uint8_t GetLightLevel() const { return m_lightLevel; }
  bool IsInitialized() const { return m_initialized; }
  
protected:
  void DeviceReset();
  //void DeviceResetLimits();
  void DeviceSetSettings();
  //void DeviceSetTime();
  //void DeviceSetPassword();
  //void DeviceSetPasswordChange();
  void DeviceSetSeason();
  //void DeviceSetTiming();

  void DeviceGetSettings();
  void DeviceGetLightLevel();
  void DeviceGetBatteryLevel();
  
  #ifdef WEB_SOCKET_DEBUG
  void PrintData();
  #endif
  
  void SendRequest(const uint8_t* buff, unsigned int buff_n);
  // handle response from AM43 device
  // Returns response end offset
  int HandleResponse(const uint8_t* buff, unsigned int buff_n);

  // Build data payload for device SetSettings request
  // Returns size of payload in bytes
  int BuildSettingsData(uint8_t* buff, uint8_t buff_n);

  // Build request and store it to buff
  // Returns size of request in bytes
  int BuildRequest(uint8_t* buff, unsigned int buff_n, Command cmd, uint8_t data);
  
  // Build request and store it to buff
  // Returns size of request in bytes
  int BuildRequest(uint8_t* buff, unsigned int buff_n, Command cmd, const uint8_t* data, uint8_t data_n);
  
  Stream* m_stream;
  UpdateStep m_update_step;
  unsigned long m_last_update;
  unsigned long m_update_delay;
  int m_no_answer_reset_counter;
  int m_update_ticks;
  
  Direction m_direction;
  OperationMode m_operationMode;
  uint8_t m_deviceSpeed; // RPM
  uint16_t m_deviceLength; // mm
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

extern AM43Class AM43;

#endif
