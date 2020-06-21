/*************************************************** 
    Copyright (C) 2016  Steffen Ochs
    Copyright (C) 2019  Martin Koerner

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
    
    HISTORY: Please refer Github History
    
****************************************************/
#include <esp_pm.h>
#include <OLEDDisplay.h>
#include "DisplayOledLink.h"
#include "DisplayOledIcons.h"
#include "Settings.h"
#include "Version.h"

#define MAXBATTERYBAR 13u
#define OLIMITMIN 35.0
#define OLIMITMAX 200.0
#define ULIMITMINF 50.0
#define ULIMITMAXF 302.0
#define OLIMITMINF 95.0
#define OLIMITMAXF 392.0
#define DISPLAY_I2C_ADDRESS 0x3cu
#define LBUTTON_IO 14u
#define RBUTTON_IO 12u
#define OLED_TASK_CYCLE_TIME 10u   // 10ms
#define OLED_BOOT_SCREEN_TIME 100u // 100 * 10ms = 1s
#define OLED_FLASH_INTERVAL 50u    // 50 * 10ms = 500ms
#define OLED_WIFI_AP_DELAY 10000u
#define OLED_BATTERY_PERCENTAGE_DELAY 10000u
#define BUTTON_DEBOUNCE_TICKS 10u
#define BUTTON_CLICK_TICKS 200u
#define BUTTON_PRESS_TICKS 600u

enum class Frames
{
  Temperature,
  TemperatureSettings,
  PitmasterSettings,
  SystemSettings,
  NumOfFrames,
};

float DisplayOledLink::currentData = 0; // Zwischenspeichervariable
//uint8_t DisplayOledLink::buttonMupi = 1u;
boolean DisplayOledLink::oledBlocked = false;
//String alarmname[4] = {"off", "push", "summer", "all"};

SystemBase *DisplayOledLink::system = gSystem;
SH1106Wire DisplayOledLink::oled = SH1106Wire(DISPLAY_I2C_ADDRESS, SDA, SCL);
OLEDDisplayUi DisplayOledLink::ui = OLEDDisplayUi(&DisplayOledLink::oled);
//FrameCallback DisplayOledLink::frames[] = {DisplayOledLink::drawTemp, DisplayOledLink::drawTempSettings, DisplayOledLink::drawPitmasterSettings, DisplayOledLink::drawSystemSettings};
FrameCallback DisplayOledLink::frames[] = {DisplayOledLink::drawTemp};
OverlayCallback DisplayOledLink::overlays[] = {drawOverlayBar};
//OneButton DisplayOledLink::lButton = OneButton(LBUTTON_IO, true, true);
//OneButton DisplayOledLink::rButton = OneButton(RBUTTON_IO, true, true);
MenuItem DisplayOledLink::menuItem = MenuItem::Boot;
//MenuMode DisplayOledLink::menuMode = MenuMode::Show;
uint8_t DisplayOledLink::currentChannel = 0u;
boolean DisplayOledLink::flashIndicator = false;


DisplayOledLink::DisplayOledLink()
{
}


void DisplayOledLink::init()
{
  xTaskCreatePinnedToCore(
      DisplayOledLink::task,   // Task function.
      "DisplayOledLink::task",      // String with name of task.
      10000,                    // Stack size in bytes.
      this,                     // Parameter passed as input of the task
      1,                        // Priority of the task.
      NULL,                     // Task handle.
      1);                       // CPU Core
}


boolean DisplayOledLink::initDisplay()
{
  this->loadConfig();

  ui.setTargetFPS(30);
  ui.setFrames(frames, (uint8_t)Frames::NumOfFrames);
  ui.setOverlays(overlays, 1u);
  ui.setTimePerFrame(10000);
  ui.setTimePerTransition(300);
  ui.disableAutoTransition();
  ui.disableAllIndicators();
  ui.init();

  oled.flipScreenVertically();
  oled.clear();
  oled.display();
  drawConnect();

  return true;
}

void DisplayOledLink::task(void *parameter)
{
  uint8_t flashTimeout = OLED_FLASH_INTERVAL;
  TickType_t xLastWakeTime = xTaskGetTickCount();
  uint8_t bootScreenTimeout = OLED_BOOT_SCREEN_TIME;
  DisplayOledLink *display = (DisplayOledLink *)parameter;

  display->system->wireLock();
  display->initDisplay();
  display->system->wireRelease();

  // show boot screen
  while (bootScreenTimeout || display->system->isInitDone() != true)
  {
    vTaskDelayUntil(&xLastWakeTime, OLED_TASK_CYCLE_TIME);
    if (bootScreenTimeout)
      bootScreenTimeout--;
  }

  for (;;)
  {
    vTaskDelayUntil(&xLastWakeTime, 10);

    display->system->wireLock();
    display->update();
    display->system->wireRelease();

    if (!flashTimeout--)
    {
      flashTimeout = OLED_FLASH_INTERVAL;
      display->flashIndicator = !display->flashIndicator;
    }
  }
}

void DisplayOledLink::saveConfig()
{
  DynamicJsonBuffer jsonBuffer(Settings::jsonBufferSize);
  JsonObject &json = jsonBuffer.createObject();
  Settings::write(kDisplay, json);
}

void DisplayOledLink::loadConfig()
{
  DynamicJsonBuffer jsonBuffer(Settings::jsonBufferSize);
  JsonObject &json = Settings::read(kDisplay, &jsonBuffer);

  if (json.success())
  {
  }
}

void DisplayOledLink::update()
{
  // check global block
  if (!blocked)
  {
    //check oled block
    if (!oledBlocked)
      ui.update();
  }
}


// ++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Frame while system start
void DisplayOledLink::drawConnect()
{

  oled.clear();
  oled.setColor(WHITE);

  // Draw Logo
  oled.drawXbm(7, 4, nano_width, nano_height, xbmnano);
  oled.display();
}


// ++++++++++++++++++++++++++++++++++++++++++++++++++++++
// STATUS ROW
void DisplayOledLink::drawOverlayBar(OLEDDisplay *display, OLEDDisplayUiState *state)
{
  Pitmaster *pit = system->pitmasters[0];
  //int battPixel = 0.5 + ((gSystem->battery->percentage * MAXBATTERYBAR) / 100.0);

  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->setFont(ArialMT_Plain_10);

  if (DisplayBase::debugString.length() != 0u)
  {
    display->drawString(0, 0, DisplayBase::debugString);
    return;
  }

  if (pit)
  {
    switch (pit->getType())
    {
    case pm_off:
      display->drawString(4, 0, "IP:");
      if (system->wlan.isAP())
        display->drawString(18, 0, WiFi.softAPIP().toString());
      else if (system->wlan.isConnected())
        display->drawString(18, 0, WiFi.localIP().toString());
      else
        display->drawString(18, 0, "");
      break;
    case pm_manual:
      display->drawString(33, 0, "M  " + String(pit->getValue(), 0) + "%");
      break;
    case pm_auto:
      display->drawString(33, 0, "P  " + String(pit->getTargetTemperature(), 1) + " / " + String(pit->getValue(), 0) + "%");
      break;

    }
  }

  display->setTextAlignment(TEXT_ALIGN_RIGHT);

  if (system->wlan.isConnected())
  {
    //display->drawString(128,0,String(wifi.rssi)+" dBm");
    display->fillRect(116, 8, 2, 1); //Draw ground line
    display->fillRect(120, 8, 2, 1); //Draw ground line
    display->fillRect(124, 8, 2, 1); //Draw ground line

    if (system->wlan.getRssi() > -105)
      display->fillRect(116, 5, 2, 3); //Draw 1 line
    if (system->wlan.getRssi() > -95)
      display->fillRect(120, 3, 2, 5); //Draw 2 line
    if (system->wlan.getRssi() > -80)
      display->fillRect(124, 1, 2, 7); //Draw 3 line
  }
  else if (system->wlan.isAP() && (millis() > OLED_WIFI_AP_DELAY))
  {
    display->drawString(128, 0, "AP");
  }
  else
  {
    display->drawString(128, 0, "");
  }

}

// ++++++++++++++++++++++++++++++++++++++++++++++++++++++
// MAIN TEMPERATURE FRAME
void DisplayOledLink::drawTemp(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
  menuItem = MenuItem::TempShow;
  int match = 0;
  TemperatureBase *temperature = system->temperatures[currentChannel];
  display->drawXbm(x + 19, 18 + y, 20, 36, xbmtemp); // Symbol

  // Show limits in OLED
  if ((temperature->getMaxValue() > temperature->getMinValue()) && temperature->isActive())
  {
    match = map((int)temperature->getValue(), temperature->getMinValue(), temperature->getMaxValue(), 3, 18);
    match = constrain(match, 0, 20);
  }

  display->fillRect(x + 27, y + 43 - match, 4, match); // Current level
  display->setTextAlignment(TEXT_ALIGN_RIGHT);
  display->setFont(ArialMT_Plain_10);
  display->drawString(19 + x, 20 + y, String(currentChannel + 1)); // Channel
  display->drawString(114 + x, 20 + y, temperature->getName());    // Channel Name //utf8ascii()
  display->setFont(ArialMT_Plain_16);
  if ((temperature->getAlarmStatus() != NoAlarm) && (true == flashIndicator))
  {
    if (temperature->getValue() != INACTIVEVALUE)
    {
      if (gSystem->temperatures.getUnit() == Fahrenheit)
        display->drawCircle(100, 41, 2); // Grad-Zeichen
      else
        display->drawCircle(99, 41, 2);                                                                                        // Grad-Zeichen
      display->drawString(114 + x, 36 + y, String(temperature->getValue(), 1) + "  " + (char)gSystem->temperatures.getUnit()); // Channel Temp
    }
    else
      display->drawString(114 + x, 36 + y, "OFF");
  }
  else if (temperature->getAlarmStatus() == NoAlarm)
  {
    if (temperature->getValue() != INACTIVEVALUE)
    {
      if (gSystem->temperatures.getUnit() == Fahrenheit)
        display->drawCircle(100, 41, 2); // Grad-Zeichen
      else
        display->drawCircle(99, 41, 2);                                                                                        // Grad-Zeichen
      display->drawString(114 + x, 36 + y, String(temperature->getValue(), 1) + "  " + (char)gSystem->temperatures.getUnit()); // Channel Temp
    }
    else
      display->drawString(114 + x, 36 + y, "OFF");
  }

  Pitmaster *pitmaster;

  for (int i = 0; i < system->pitmasters.count(); i++)
  {

    pitmaster = system->pitmasters[i];

    if (NULL == pitmaster)
      continue;

    temperature = pitmaster->getAssignedTemperature();

    if (NULL == temperature)
      continue;

    // Show Pitmaster Activity on Icon
    if (pm_auto == pitmaster->getType())
    {
      if (currentChannel == temperature->getGlobalIndex())
      {
        display->setFont(ArialMT_Plain_10);
        if (pitmaster->isAutoTuneRunning())
          display->drawString(44 + x, 31 + y, "A");
        else
          display->drawString(44 + x, 31 + y, "P");

        int _cur = temperature->getValue() * 10;
        int _set = pitmaster->getTargetTemperature() * 10;
        if (_cur > _set)
          display->drawXbm(x + 37, 24 + y, arrow_height, arrow_width, xbmarrow2);
        else if (_cur < _set)
          display->drawXbm(x + 37, 24 + y, arrow_height, arrow_width, xbmarrow1);
        else
          display->drawXbm(x + 37, 24 + y, arrow_width, arrow_height, xbmarrow);
      }
    }
  }
}



/*
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++
// TEMPERATURE CONTEXT -Page
void DisplayOled::drawTempSettings(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
  if ((true == flashIndicator) || MenuMode::Edit != menuMode)
  {
    display->drawXbm(x + 19, 18 + y, 20, 36, xbmtemp); // Symbol
    //display->fillRect(x + 27, y + 43 - ch[currentChannel].match, 4, ch[currentChannel].match); // Current level
  }
  TemperatureBase *temperature = gSystem->temperatures[currentChannel];

  char unit = (char)gSystem->temperatures.getUnit();

  display->setTextAlignment(TEXT_ALIGN_RIGHT);
  display->setFont(ArialMT_Plain_10);
  display->drawString(19 + x, 20 + y, String(currentChannel + 1)); // Channel
  //display->drawString(114, 20, menutextde[current_frame]);

  switch (menuItem)
  {

  case MenuItem::TempSettingsUpper: // UPPER LIMIT
    display->drawLine(33 + x, 25 + y, 50, 25);
    display->drawCircle(95, 23, 1); // Grad-Zeichen
    if (MenuMode::Show == menuMode)
    {
      currentData = temperature->getMaxValue();
    }
    else if (MenuMode::Set == menuMode)
    {
      temperature->setMaxValue(currentData);
      menuMode = MenuMode::Show;
      system->temperatures.saveConfig();
    }
    display->drawString(104 + x, 19 + y, String(currentData, 1) + "  " + unit); // Upper Limit
    break;

  case MenuItem::TempSettingsLower: // LOWER LIMIT
    display->drawLine(33 + x, 39 + y, 50, 39);
    display->drawCircle(95, 38, 1); // Grad-Zeichen
    if (MenuMode::Show == menuMode)
    {
      currentData = temperature->getMinValue();
    }
    else if (MenuMode::Set == menuMode)
    {
      temperature->setMinValue(currentData);
      menuMode = MenuMode::Show;
      system->temperatures.saveConfig();
    }
    display->drawString(104 + x, 34 + y, String(currentData, 1) + "  " + unit);
    break;

  case MenuItem::TempSettingsType: // TYP
    display->drawString(114, 20, "TYP");
    if (MenuMode::Show == menuMode)
    {
      currentData = temperature->getType();
    }
    else if (MenuMode::Edit == menuMode)
    {
      if (currentData < 0)
        currentData = temperature->getTypeCount() - 1;
      else if (currentData >= temperature->getTypeCount())
        currentData = 0;
    }
    else if (MenuMode::Set == menuMode)
    {
      temperature->setType((uint8_t)currentData);
      menuMode = MenuMode::Show;
      system->temperatures.saveConfig();
    }
    display->drawString(114 + x, 36 + y, temperature->getTypeName((uint8_t)currentData));
    break;

  case MenuItem::TempSettingsAlarm: // ALARM
    display->drawString(114, 20, "ALARM");
    if (MenuMode::Show == menuMode)
    {
      currentData = temperature->getAlarmSetting();
    }
    else if (MenuMode::Edit == menuMode)
    {
      if (currentData < AlarmMin)
        currentData = AlarmMax - 1;
      else if (currentData >= AlarmMax)
        currentData = AlarmMin;
    }
    else if (MenuMode::Set == menuMode)
    {
      temperature->setAlarmSetting((AlarmSetting)currentData);
      menuMode = MenuMode::Show;
      system->temperatures.saveConfig();
    }
    display->drawString(114 + x, 36 + y, alarmname[(int)currentData]);
    break;
  }
}

// ++++++++++++++++++++++++++++++++++++++++++++++++++++++
// PITMASTER -Page
void DisplayOled::drawPitmasterSettings(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
  if ((true == flashIndicator) || MenuMode::Edit != menuMode)
    display->drawXbm(x + 15, 20 + y, pit_width, pit_height, xbmpit); // Symbol

  display->setTextAlignment(TEXT_ALIGN_RIGHT);
  display->setFont(ArialMT_Plain_10);

  Pitmaster *pm = system->pitmasters[0];
  PitmasterProfile *profile = pm->getAssignedProfile();
  char unit = (char)gSystem->temperatures.getUnit();

  switch (menuItem)
  {

  case MenuItem::PitmasterSettingsProfile: // PID PROFIL
    display->drawString(116, 20, "PITMASTER:");
    if (MenuMode::Show == menuMode)
    {
      currentData = profile->id;
    }
    else if (MenuMode::Edit == menuMode)
    {
      if (currentData < 0)
        currentData = system->getPitmasterProfileCount() - 1;
      else if (currentData >= system->getPitmasterProfileCount())
        currentData = 0;
    }
    else if (MenuMode::Set == menuMode)
    {
      pm->assignProfile(system->getPitmasterProfile(currentData));
      menuMode = MenuMode::Show;
      system->pitmasters.saveConfig();
    }
    display->drawString(116 + x, 36 + y, system->getPitmasterProfile(currentData)->name);
    break;

  case MenuItem::PitmasterSettingsChannel: // PITMASTER CHANNEL
    display->drawString(116, 20, "CHANNEL:");
    if (MenuMode::Show == menuMode)
    {
      currentData = pm->getAssignedTemperature()->getGlobalIndex();
    }
    else if (MenuMode::Edit == menuMode)
    {
      if (currentData < 0)
        currentData = system->temperatures.count() - 1;
      else if (currentData >= system->temperatures.count())
        currentData = 0;
    }
    else if (MenuMode::Set == menuMode)
    {
      pm->assignTemperature(system->temperatures[(uint8_t)currentData]);
      menuMode = MenuMode::Show;
      system->pitmasters.saveConfig();
    }
    display->drawString(116 + x, 36 + y, String(system->temperatures[(uint8_t)currentData]->getGlobalIndex() + 1));
    break;

  case MenuItem::PitmasterSettingsTemperature: // SET TEMPERATUR
    display->drawString(116, 20, "SET:");
    if (MenuMode::Show == menuMode)
    {
      currentData = pm->getTargetTemperature();
    }
    else if (MenuMode::Set == menuMode)
    {
      pm->setTargetTemperature(currentData);
      menuMode = MenuMode::Show;
      system->pitmasters.saveConfig();
    }
    display->drawCircle(107, 40, 1); // Grad-Zeichen
    display->drawString(116 + x, 36 + y, String(currentData, 1) + "  " + unit);
    break;

  case MenuItem::PitmasterSettingsType: // PITMASTER TYP
    display->drawString(116, 20, "ACTIVE:");
    if (MenuMode::Show == menuMode)
    {
      currentData = pm->getType();
    }
    else if (MenuMode::Set == menuMode)
    {
      pm->setType((PitmasterType)currentData);
      menuMode = MenuMode::Show;
      system->pitmasters.saveConfig();
    }

    if ((PitmasterType)currentData == pm_auto)
      display->drawString(116 + x, 36 + y, "AUTO");
    else if ((PitmasterType)currentData == pm_manual)
      display->drawString(116 + x, 36 + y, "MANUAL");
    else if ((PitmasterType)currentData == pm_off)
      display->drawString(116 + x, 36 + y, "OFF");

    break;
  }
}

// ++++++++++++++++++++++++++++++++++++++++++++++++++++++
// SYSTEM -Page
void DisplayOled::drawSystemSettings(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{

  if ((true == flashIndicator) || MenuMode::Edit != menuMode)
    display->drawXbm(x + 5, 22 + y, sys_width, sys_height, xbmsys);
  display->setTextAlignment(TEXT_ALIGN_RIGHT);
  display->setFont(ArialMT_Plain_10);

  switch (menuItem)
  {

  case MenuItem::SystemSettingsSSID: // SSID
    display->drawString(120, 20, "SSID:");
    if (system->wlan.isAP())
      display->drawString(120, 36, system->wlan.getAccessPointName());
    else if (system->wlan.isConnected())
      display->drawString(120, 36, WiFi.SSID());
    else
      display->drawString(120, 36, "");
    break;

  case MenuItem::SystemSettingsIP: // IP
    display->drawString(120, 20, "IP:");
    if (system->wlan.isAP())
      display->drawString(120, 36, WiFi.softAPIP().toString());
    else if (system->wlan.isConnected())
      display->drawString(120, 36, WiFi.localIP().toString());
    else
      display->drawString(120, 36, "");
    break;

  case MenuItem::SystemSettingsHost: // HOST
    display->drawString(120, 20, "HOSTNAME:");
    display->drawString(120, 36, system->wlan.getHostName());
    break;

  case MenuItem::SystemSettingsUnit: // UNIT
    display->drawString(120, 20, "UNIT:");
    display->drawCircle(105, 40, 1); // Grad-Zeichen
    if (MenuMode::Show == menuMode)
    {
      currentData = system->temperatures.getUnit();
    }
    else if (MenuMode::Set == menuMode)
    {
      system->temperatures.setUnit((TemperatureUnit)currentData);
      menuMode = MenuMode::Show;
      system->temperatures.saveConfig();
    }
    display->drawString(114 + x, 36 + y, String((char)currentData));
    break;

  case MenuItem::SystemSettingsFirmwareVersion: // FIRMWARE VERSION
    display->drawString(120, 20, "FIRMWARE:");
    display->drawString(114 + x, 36 + y, FIRMWAREVERSION);
    break;
  }
}

// ++++++++++++++++++++++++++++++++++++++++++++++++++++++
// BACK -Page
void drawback(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
  display->drawXbm(x + 5, 22 + y, back_width, back_height, xbmback);
  display->setTextAlignment(TEXT_ALIGN_RIGHT);
  display->setFont(ArialMT_Plain_16);
  display->drawString(100 + x, 27 + y, "BACK");
}
*/
