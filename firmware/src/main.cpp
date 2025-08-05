#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

// Clockface
#include <Clockface.h>
// Commons
#include <WiFiController.h>
#include <CWDateTime.h>
#include <CWPreferences.h>
#include <CWWebServer.h>
#include <StatusController.h>

#define MIN_BRIGHT_DISPLAY_ON 4
#define MIN_BRIGHT_DISPLAY_OFF 0

#define ESP32_LED_BUILTIN 2

MatrixPanel_I2S_DMA *dma_display = nullptr;

Clockface *clockface;

WiFiController wifi;
CWDateTime cwDateTime;

bool autoBrightEnabled;
long autoBrightMillis = 0;
long timeControlMillis = 0;

uint8_t currentBrightSlot = -1;

void displaySetup(bool swapBlueGreen, uint8_t displayBright, uint8_t displayRotation)
{
  HUB75_I2S_CFG mxconfig(64, 64, 1);

  // 自定义引脚修改
  mxconfig.gpio.r1 = 32;
  mxconfig.gpio.b1 = 33;
  mxconfig.gpio.r2 = 25;
  mxconfig.gpio.b2 = 26;
  mxconfig.gpio.a = 27;
  mxconfig.gpio.c = 14;
  mxconfig.gpio.clk = 12;
  mxconfig.gpio.oe = 13;

  mxconfig.gpio.g1 = 22;
  //mxconfig.gpio.oe=21;
  mxconfig.gpio.g2 = 19;
  mxconfig.gpio.e = 18;
  mxconfig.gpio.b = 5;
  mxconfig.gpio.d = 4;
  mxconfig.gpio.lat = 15;

  if (swapBlueGreen)
  {
    // Swap Blue and Green pins because the panel is RBG instead of RGB.
    mxconfig.gpio.b1 = 22;
    mxconfig.gpio.b2 = 19;
    mxconfig.gpio.g1 = 33;
    mxconfig.gpio.g2 = 26;
  }

  // mxconfig.gpio.e = 18;
  // mxconfig.clkphase = false;

  // Display Setup
  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();
  dma_display->setBrightness8(displayBright);
  dma_display->clearScreen();
  dma_display->setRotation(displayRotation);
}

void automaticBrightControl()
{
  if (autoBrightEnabled) {
    if (millis() - autoBrightMillis > 3000)
    {
      int16_t currentValue = analogRead(ClockwiseParams::getInstance()->ldrPin);

      uint16_t ldrMin = ClockwiseParams::getInstance()->autoBrightMin;
      uint16_t ldrMax = ClockwiseParams::getInstance()->autoBrightMax;

      const uint8_t minBright = (currentValue < ldrMin ? MIN_BRIGHT_DISPLAY_OFF : MIN_BRIGHT_DISPLAY_ON);
      uint8_t maxBright = ClockwiseParams::getInstance()->displayBright;

      uint8_t slots = 10; //10 slots
      uint8_t mapLDR = map(currentValue > ldrMax ? ldrMax : currentValue, ldrMin, ldrMax, 1, slots);
      uint8_t mapBright = map(mapLDR, 1, slots, minBright, maxBright);

      // Serial.printf("LDR: %d, mapLDR: %d, Bright: %d\n", currentValue, mapLDR, mapBright);
      if(abs(currentBrightSlot - mapLDR ) >= 2 || mapBright == 0){
           dma_display->setBrightness8(mapBright);
           currentBrightSlot=mapLDR;
          //  Serial.printf("setBrightness: %d , Update currentBrightSlot to %d\n", mapBright, mapLDR);
      }
      autoBrightMillis = millis();
    }
  }
}

void automaticTimeControl()
{
  if (!ClockwiseParams::getInstance()->enableTimeControl) return;

  if (millis() - timeControlMillis > 3000) // 每3秒检查一次
  {
    uint8_t hourNow = clockface->getCurrentHour();
    uint8_t hourFrom = ClockwiseParams::getInstance()->activeHourStart;
    uint8_t hourTo   = ClockwiseParams::getInstance()->activeHourEnd;

    bool inTimeRange = (hourFrom < hourTo)
        ? (hourNow >= hourFrom && hourNow < hourTo)
        : (hourNow >= hourFrom || hourNow < hourTo); // 跨午夜

    uint8_t targetBrightness = inTimeRange
        ? ClockwiseParams::getInstance()->displayBright
        : ClockwiseParams::getInstance()->nightBright;

    if (abs(currentBrightSlot - targetBrightness) >= 2 || targetBrightness == 0) {
      dma_display->setBrightness8(targetBrightness);
      currentBrightSlot = targetBrightness;
    }

    timeControlMillis = millis();
  }
}


void setup()
{
  Serial.begin(115200);
  pinMode(ESP32_LED_BUILTIN, OUTPUT);

  StatusController::getInstance()->blink_led(5, 100);

  ClockwiseParams::getInstance()->load();

  pinMode(ClockwiseParams::getInstance()->ldrPin, INPUT);

  displaySetup(ClockwiseParams::getInstance()->swapBlueGreen, ClockwiseParams::getInstance()->displayBright, ClockwiseParams::getInstance()->displayRotation);
  clockface = new Clockface(dma_display);

  autoBrightEnabled = (ClockwiseParams::getInstance()->autoBrightMax > 0);

  StatusController::getInstance()->clockwiseLogo();
  delay(1000);

  StatusController::getInstance()->wifiConnecting();
  if (wifi.begin())
  {
    StatusController::getInstance()->ntpConnecting();
    cwDateTime.begin(ClockwiseParams::getInstance()->timeZone.c_str(), 
        ClockwiseParams::getInstance()->use24hFormat, 
        ClockwiseParams::getInstance()->ntpServer.c_str(),
        ClockwiseParams::getInstance()->manualPosix.c_str());
    clockface->setup(&cwDateTime);
  }
}

void loop()
{
  wifi.handleImprovWiFi();

  if (wifi.isConnected())
  {
    ClockwiseWebServer::getInstance()->handleHttpRequest();
    ezt::events();
  }

  if (wifi.connectionSucessfulOnce)
  {
    clockface->update();
  }

  automaticBrightControl();
  automaticTimeControl();
}
