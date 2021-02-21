#include "am43.h"

#ifdef WEB_SOCKET_DEBUG
// http://tzapu.github.io/WebSocketSerialMonitor/
#include <WebSocketsServer.h>   //https://github.com/Links2004/arduinoWebSockets/tree/async
#include <Hash.h>
WebSocketsServer webSocket = WebSocketsServer(81);
String log_txt;
#endif

AM43Class AM43;
namespace {
static uint8_t s_reqPrefix[] = { 0x00, 0xFF, 0x00, 0x00 };
static uint8_t s_reqHeaderPrefix[] = { 0x9a };
}

#ifdef WEB_SOCKET_DEBUG
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t lenght)
{
  switch (type)
  {
    case WStype_TEXT:
      String txt((char*)payload);
      if(payload[0] == 'C')
      {
        AM43.SendAction(AM43Class::ControlAction::Close);
      }
      else if(payload[0] == 'O')
      {
        AM43.SendAction(AM43Class::ControlAction::Open);
      }
      else if(payload[0] == 'S')
      {
        AM43.SendAction(AM43Class::ControlAction::Stop);
      }
      else if(payload[0] == 'U')
      {
        AM43.Update();
      }
      else if(payload[0] == 'L')
      {
        webSocket.broadcastTXT(log_txt + "\n");
      }
      else
      {
        uint8_t pos = txt.toInt();
        AM43.SetPosition(pos);
      }
      break;
  }
}
#endif

AM43Class::AM43Class() :
m_stream(nullptr),
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

#ifdef WEB_SOCKET_DEBUG
void AM43Class::PrintData()
{
  String txt("============================================\n");
  
  if(m_direction == Direction::Forward)
  {
    txt += String("Direction: Forward\n");
  }
  else if(m_direction == Direction::Reverse)
  {
    txt += String("Direction: Reverse\n");
  }
  else
  {
    txt += String("Direction: Unknown\n");
  }

  if(m_operationMode == OperationMode::Inching)
  {
    txt += String("OperationMode: Inching\n");
  }
  else if(m_operationMode == OperationMode::Continuous)
  {
    txt += String("OperationMode: Continuous\n");
  }
  else
  {
    txt += String("OperationMode: Unknown\n");
  }

  txt += String("Speed: ") + String(m_deviceSpeed);
  txt += String("\nLength: ") + String(m_deviceLength);
  txt += String("\nDiameter: ") + String(m_deviceDiameter);
  
  switch(m_deviceType)
  {
    case DeviceType::Baiye: txt += String("\nType: Baiye"); break;
    case DeviceType::Chuizhi: txt += String("\nType: Chuizhi"); break;
    case DeviceType::Juanlian: txt += String("\nType: Juanlian"); break;
    case DeviceType::Fengchao: txt += String("\nType: Fengchao"); break;
    case DeviceType::Rousha: txt += String("\nType: Rousha"); break;
    case DeviceType::Xianggelila: txt += String("\nType: Xianggelila"); break;
    default: txt += String("\nType: ") + String((int)m_deviceType);
  }  
  
  txt += m_topLimitSet ? String("\nTopLimit: Set") : String("\nTopLimit: Not Set");
  txt += m_bottomLimitSet ? String("\nBottomLimit: Set") : String("\nBottomLimit: Not Set");

  txt += m_hasLightSensor ? String("\nHasLightSensor: Yes") : String("\nHasLightSensor: No");
  
  txt += String("\nPosition: ") + String(m_position);
  txt += String("\nLightLevel: ") + String(m_lightLevel);
  txt += String("\nBatteryLevel: ") + String(m_batteryLevel) + String("\n");

  SeasonInfo* si[] = {&m_summerSeason, &m_winterSeason};
  for(SeasonInfo* s : si)
  {
    String season;
    season += s->SeasonState ? String("On, ") : String("Off, ");
    season += String(s->LightSeasonState) + String(", ");
    season += String(s->LightLevel) + String(", ");
    season += String(s->LightStartHour) + String(":") + String(s->LightStartMinute) + String(", ");
    season += String(s->LightEndHour) + String(":") + String(s->LightEndMinute) + String("\n");
    txt += season;
  }
  
  webSocket.broadcastTXT(txt + "\n");
}
#endif

void AM43Class::Init(Stream* output_stream)
{
  pinMode(AM43_PIN_RESET, INPUT);
  
  m_stream = output_stream;
  #ifdef WEB_SOCKET_DEBUG
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  #endif
}

void AM43Class::Loop()
{
  if(m_stream != nullptr && m_stream->available() > 1) // At least we have header prefix and command
  {
    int buff_offset = 0;
    while(m_stream->available())
    {
      m_aux_recv_buff[buff_offset++] = m_stream->read();
      delay(100);
    }

    int resp_end = 0;
    uint8_t* buff = m_aux_recv_buff;
    int buff_n = buff_offset;
    do
    {
      resp_end = HandleResponse(buff, buff_n);
      buff += resp_end;
      buff_n -= resp_end;
    } while(resp_end > 0 && buff_n > 0);

    #ifdef WEB_SOCKET_DEBUG
    PrintData();
    #endif
  }

  if(millis() - m_last_update >= m_update_delay)
  {    
    Update();
    
    m_last_update = millis();
  }

  #ifdef WEB_SOCKET_DEBUG
  webSocket.loop();
  #endif
}

void AM43Class::DeviceReset()
{  
  // Switch pin mode to output and pull it low
  digitalWrite(AM43_PIN_RESET, LOW);
  pinMode(AM43_PIN_RESET, OUTPUT);
  
  delay(100);
  
  pinMode(AM43_PIN_RESET, INPUT);

  delay(100);
}

void AM43Class::Update()
{
  #ifdef WEB_SOCKET_DEBUG
  if(log_txt.length() > 2000)
  {
    log_txt = "";
  }
  #endif
  
  ++m_no_answer_reset_counter;
  if(m_no_answer_reset_counter > AM43_NO_ANSWER_RESET_T)
  {
    DeviceReset();
    m_no_answer_reset_counter = 0;
  }
  
  if(++m_update_ticks > 10 || m_update_step == UpdateStep::Start)
  {
      m_update_delay = AM43_UPDATE_DELAY_FAST_MS;
      m_update_step = UpdateStep::GetSettings;
      m_update_ticks = 0;
  }
  else if(m_update_step == UpdateStep::Finish)
  {
      m_update_delay = AM43_UPDATE_DELAY_SLOW_MS;
      m_update_step = UpdateStep::Start;
      m_initialized = true;
      m_update_ticks = 0;
  }
  
  switch(m_update_step)
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

void AM43Class::SendAction(ControlAction action)
{
  const int len = BuildRequest(m_aux_buff, sizeof(m_aux_buff), Command::SendAction, static_cast<uint8_t>(action));
  SendRequest(m_aux_buff, len);

  // Update position imadeitely to unlock home automation options
  // e.g. Home Assistant locks Close command if position is 100% or Open command if position is 0%
  switch(action)
  {
    case ControlAction::Close:
    {
      ++m_position;
      #ifdef WEB_SOCKET_DEBUG
      log_txt += " +:" + String(m_position);
      #endif
      break;
    }
    case ControlAction::Open:
    {
      --m_position;
      #ifdef WEB_SOCKET_DEBUG
      log_txt += " -:" + String(m_position);
      #endif
      break;
    }
  }
  
  m_position = constrain(m_position, 0, 100);
}

void AM43Class::SetPosition(uint8_t position_percent)
{
  m_position = constrain(position_percent, 0, 100);
  #ifdef WEB_SOCKET_DEBUG
  log_txt += " =:" + String(m_position);
  #endif
  
  const int len = BuildRequest(m_aux_buff, sizeof(m_aux_buff), Command::SetPosition, m_position);
  SendRequest(m_aux_buff, len);
}

void AM43Class::DeviceSetSettings()
{
  uint8_t data[64];
  const int data_n = BuildSettingsData(data, sizeof(data));
  const int len = BuildRequest(m_aux_buff, sizeof(m_aux_buff), Command::SetName, data, data_n);
  SendRequest(m_aux_buff, len);
}

void AM43Class::DeviceGetSettings()
{
  const int len = BuildRequest(m_aux_buff, sizeof(m_aux_buff), Command::GetSettings, 1);
  SendRequest(m_aux_buff, len);
}

void AM43Class::DeviceGetLightLevel()
{
  const int len = BuildRequest(m_aux_buff, sizeof(m_aux_buff), Command::GetLightLevel, 1);
  SendRequest(m_aux_buff, len);
}

void AM43Class::DeviceGetBatteryLevel()
{
  const int len = BuildRequest(m_aux_buff, sizeof(m_aux_buff), Command::GetBatteryLevel, 1);
  SendRequest(m_aux_buff, len);
}

void AM43Class::SendRequest(const uint8_t* buff, unsigned int buff_n)
{
  #ifdef WEB_SOCKET_DEBUG
  String txt = "SendRequest:";
  for(int i = 0; i < buff_n; ++i)
  {
    txt += String(" 0x") + String(buff[i], HEX);
  }
  webSocket.broadcastTXT(txt + "\n");
  #endif
  
  if(m_stream != nullptr && buff_n > 0)
  {
    m_stream->write(buff, buff_n);
  }
}

int AM43Class::HandleResponse(const uint8_t* buff, unsigned int buff_n)
{
  #ifdef WEB_SOCKET_DEBUG
  String txt = "Processing:";
  for(int i = 0; i < buff_n; ++i)
  {
    txt += String(" 0x") + String(buff[i], HEX);
  }
  webSocket.broadcastTXT(txt + "\n");
  #endif
  
  int response_begin = -1;
  int req_herader_prefix_offset = 0;
  
  for(int i = 0; i < buff_n; ++i)
  {
    if(buff[i] == s_reqHeaderPrefix[req_herader_prefix_offset])
    {
      ++req_herader_prefix_offset;
      if(req_herader_prefix_offset == sizeof(s_reqHeaderPrefix))
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

  if(response_begin == -1)
  {
    #ifdef WEB_SOCKET_DEBUG
    webSocket.broadcastTXT("Header not found\n");
    #endif
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
  for(int i = response_begin; i < response_end - 1; ++i)
  {
    response_calculated_checksum ^= buff[i];
  }

  const uint8_t* data = buff + response_offset;
  if(response_calculated_checksum == response_checksum)
  {
    switch(response_cmd)
    {
      case Command::GetSettings:
      {
        if(response_len >= 7)
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
          #ifdef WEB_SOCKET_DEBUG
          log_txt += " *:" + String(m_position);
          #endif
        }
        break;
      }
      case Command::GetLightLevel:
      {
        if(response_len >= 2)
        {
          m_lightLevel = data[1];
        }
        
        if(m_update_step == UpdateStep::WaitForLightLevel)
        {
          m_update_step = UpdateStep::GetBatteryLevel;
        }
        
        break;
      }
      case Command::GetPosition:
      {
        if(response_len >= 2)
        {
          m_position = data[1];
          #ifdef WEB_SOCKET_DEBUG
          log_txt += " /:" + String(m_position);
          #endif
        }
        break;
      }
      case Command::GetBatteryLevel:
      {
        if(response_len >= 5)
        {
          m_batteryLevel = data[4];
        }
        
        if(m_update_step == UpdateStep::WaitForBatteryLevel)
        {
          m_update_step = UpdateStep::Finish;
        }
        
        break;
      }
      case Command::GetSpeed:
      {
        if(response_len >= 2)
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
        if(response_len >= sizeof(SeasonInfo) * 2 + 2)
        {
          memcpy(&m_summerSeason, data + 1, sizeof(SeasonInfo));
          memcpy(&m_winterSeason, data + sizeof(SeasonInfo) + 2, sizeof(SeasonInfo));
        }
        
        if(m_update_step == UpdateStep::WaitForSettings)
        {
          m_update_step = UpdateStep::GetLightLevel;
        }
        
        break;
      }
    }
  }
  
  #ifdef WEB_SOCKET_DEBUG
  txt = "Response header found";
  txt += String("\nCMD: 0x") + String((int)response_cmd, HEX);
  txt += String("\nLEN: ") + String(response_len);
  for(int i = 0; i < response_len; ++i)
  {
    txt += String("\nDAT[") + String(i) + String("]: 0x") + String(buff[response_offset + i], HEX);
  }
  txt += String("\nCHK: 0x") + String(response_checksum, HEX);
  txt += String("\nCCC: 0x") + String(response_calculated_checksum, HEX);
  
  webSocket.broadcastTXT(txt + "\n");
  #endif

  return response_end;
}

int AM43Class::BuildSettingsData(uint8_t* buff, uint8_t buff_n)
{
  if(buff_n < 6)
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

int AM43Class::BuildRequest(uint8_t* buff, unsigned int buff_n, Command cmd, uint8_t data)
{
  return BuildRequest(buff, buff_n, cmd, &data, 1);
}

int AM43Class::BuildRequest(uint8_t* buff, unsigned int buff_n, Command cmd, const uint8_t* data, uint8_t data_n)
{
  if(buff == nullptr || buff_n < sizeof(s_reqPrefix) + sizeof(s_reqHeaderPrefix) + data_n + 3)
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
  for(int i = 0; i < data_n; ++i)
  {
    buff[buff_offset++] = data[i];
  }

  uint8_t checksum = 0;
  for(int i = sizeof(s_reqPrefix); i < buff_offset; ++i)
  {
    checksum ^= buff[i];
  }
  buff[buff_offset++] = checksum;
  
  return buff_offset;
}
