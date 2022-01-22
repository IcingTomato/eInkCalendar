#define ENABLE_GxEPD2_GFX 0

#include <GxEPD2_BW.h>
#include <GxEPD2_3C.h>
#include <GxEPD2_7C.h>
#include <U8g2_for_Adafruit_GFX.h>

#if defined(ESP32)
//GxEPD2_BW<GxEPD2_583_T8, GxEPD2_583_T8::HEIGHT> display(GxEPD2_583_T8(/*CS=5*/ 15, /*DC=*/27, /*RST=*/26, /*BUSY=*/25));
//GxEPD2_3C<GxEPD2_750c, GxEPD2_750c::HEIGHT> display(GxEPD2_750c(/*CS=5*/ 15, /*DC=*/ 27, /*RST=*/ 26, /*BUSY=*/ 25));
GxEPD2_BW<GxEPD2_750_T7, GxEPD2_750_T7::HEIGHT> display(GxEPD2_750_T7(/*CS=5*/ 15, /*DC=*/ 27, /*RST=*/ 26, /*BUSY=*/ 25)); // GDEW075T7 800x480
//GxEPD2_3C<GxEPD2_750c_Z90, GxEPD2_750c_Z90::HEIGHT / 2> display(GxEPD2_750c_Z90(/*CS=5*/ 15, /*DC=*/27, /*RST=*/26, /*BUSY=*/25)); // 
#endif

#include <ArduinoJson.h>
#include "config.h"
#include "SmartConfigManager.h"
//#include "MyIP.h"
#include "QWeather.h"
#include "esp_bt.h"
#include "esp_wifi.h"
#if defined(ESP32)
#include "SPIFFS.h"
#endif

#include <FS.h>
#define FileClass fs::File
#define EPD_CS SS

#include "u8g2_mfxinran_92_number.h"
#include "u8g2_deng_56_temperature.h"
#include "u8g2_mfyuehei_18_gb2312.h"
#include "u8g2_mfyuehei_14_gb2312.h"
#include "u8g2_mfyuehei_12_gb2312.h"
#include "u8g2_mfyuanhei_16_gb2312.h"

U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;
//#include "temwordlist.h"
//#include "toxicsoul.h"
#include <ESPDateTime.h>
//#include "IPAPI.h"  //IPAPI 测不准
#include "MyIP.h"

int led = 2; //LED

const char *WEEKDAY_CN[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
const char *WEEKDAY_EN[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char *MONTH_CN[] = {"一月", "二月", "三月", "四月", "五月", "六月", "七月", "八月", "九月", "十月", "十一月", "十二月"};
const char *MONTH_EN[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
// const uint16_t SMARTCONFIG_IMAGE_WIDTH = 120;
// const uint16_t SMARTCONFIG_IMAGE_HEIGHT = 120;
const uint16_t SMARTCONFIG_IMAGE_WIDTH = 64;
const uint16_t SMARTCONFIG_IMAGE_HEIGHT = 64;
GeoInfo gi;
int16_t DISPLAY_WIDTH;
int16_t DISPLAY_HEIGHT;
u8_t pageIndex = 0;
QWeather qwAPI;
CurrentWeather cw;
CurrentAirQuality caq;
vector<DailyWeather> dws;
static const uint16_t input_buffer_pixels = 800; // may affect performance

static const uint16_t max_row_width = 800;      // for up to 7.5" display 800x480
static const uint16_t max_palette_pixels = 256; // for depth <= 8

uint8_t input_buffer[3 * input_buffer_pixels];        // up to depth 24
uint8_t output_row_mono_buffer[max_row_width / 8];    // buffer for at least one row of b/w bits
uint8_t output_row_color_buffer[max_row_width / 8];   // buffer for at least one row of color bits
uint8_t mono_palette_buffer[max_palette_pixels / 8];  // palette buffer for depth <= 8 b/w
uint8_t color_palette_buffer[max_palette_pixels / 8]; // palette buffer for depth <= 8 c/w
uint16_t rgb_palette_buffer[max_palette_pixels];      // palette buffer for depth <= 8 for buffered graphics, needed for 7-color display

// 根据 https://randomnerdtutorials.com/esp32-deep-sleep-arduino-ide-wake-up-sources/ 的评论中所描述，
// 在偶然的电源消耗问题造成的重启过程中，可能会导致数据丢失。所以他用了RTC_NOINIT_ATTR 代替了 RTC_DATA_ATTR
RTC_NOINIT_ATTR u8_t LASTPAGE = -1;

#define uS_TO_S_FACTOR 1000000 /* Conversion factor for micro seconds to seconds */
//#define TIME_TO_SLEEP 60 * 60 * 3  /* 改变这里的值来调整你需要的休眠时间，这里最终的值为秒。 例如 60*60*3 最终会休眠3个小时 */ 
#define TIME_TO_SLEEP 60 * 10

//一言
typedef struct {
  char hitokoto[1024];//一言正文。编码方式 unicode。使用 utf-8。
  char from[60];//一言的出处
} HitokotoData_t;
HitokotoData_t Hitokoto;
//DynamicJsonBuffer jsonBuffer;

void print_wakeup_reason()
{
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason)
  {
  case ESP_SLEEP_WAKEUP_EXT0:
    Serial.println("Wakeup caused by external signal using RTC_IO");
    break;
  case ESP_SLEEP_WAKEUP_EXT1:
    Serial.println("Wakeup caused by external signal using RTC_CNTL");
    break;
  case ESP_SLEEP_WAKEUP_TIMER:
    Serial.println("Wakeup caused by timer");
    break;
  case ESP_SLEEP_WAKEUP_TOUCHPAD:
    Serial.println("Wakeup caused by touchpad");
    break;
  case ESP_SLEEP_WAKEUP_ULP:
    Serial.println("Wakeup caused by ULP program");
    break;
  default:
    Serial.printf("Wakeup was not caused by deep sleep: %d\n", wakeup_reason);
    break;
  }
}

uint16_t read16(fs::File &f)
{
  // BMP data is stored little-endian, same as Arduino.
  uint16_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read(); // MSB
  return result;
}

uint32_t read32(fs::File &f)
{
  // BMP data is stored little-endian, same as Arduino.
  uint32_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read();
  ((uint8_t *)&result)[2] = f.read();
  ((uint8_t *)&result)[3] = f.read(); // MSB
  return result;
}

void setupDateTime()
{
  DateTime.setTimeZone(8);
  DateTime.begin(/* timeout param */);
  if (!DateTime.isTimeValid())
  {
    Serial.println("Failed to get time from server.");
  }
}

void drawBitmapFromSpiffs_Buffered(const char *filename, int16_t x, int16_t y, bool with_color, bool partial_update, bool overwrite)
{
  fs::File file;
  bool valid = false; // valid format to be handled
  bool flip = true;   // bitmap is stored bottom-to-top
  bool has_multicolors = display.epd2.panel == GxEPD2::ACeP565;
  uint32_t startTime = millis();
  if ((x >= DISPLAY_WIDTH) || (y >= DISPLAY_HEIGHT))
    return;
  Serial.println();
  Serial.print("Loading image '");
  Serial.print(filename);
  Serial.println('\'');
#if defined(ESP32)
  file = SPIFFS.open(String("/") + filename, "r");
#else
  file = SPIFFS.open(filename, "r");
#endif
  if (!file)
  {
    Serial.print("File not found");
    return;
  }
  // Parse BMP header
  if (read16(file) == 0x4D42) // BMP signature
  {
    uint32_t fileSize = read32(file);
    uint32_t creatorBytes = read32(file);
    uint32_t imageOffset = read32(file); // Start of image data
    uint32_t headerSize = read32(file);
    uint32_t width = read32(file);
    uint32_t height = read32(file);
    uint16_t planes = read16(file);
    uint16_t depth = read16(file); // bits per pixel
    uint32_t format = read32(file);
    if ((planes == 1) && ((format == 0) || (format == 3))) // uncompressed is handled, 565 also
    {
      Serial.print("File size: ");
      Serial.println(fileSize);
      Serial.print("Image Offset: ");
      Serial.println(imageOffset);
      Serial.print("Header size: ");
      Serial.println(headerSize);
      Serial.print("Bit Depth: ");
      Serial.println(depth);
      Serial.print("Image size: ");
      Serial.print(width);
      Serial.print('x');
      Serial.println(height);
      // BMP rows are padded (if needed) to 4-byte boundary
      uint32_t rowSize = (width * depth / 8 + 3) & ~3;
      if (depth < 8)
        rowSize = ((width * depth + 8 - depth) / 8 + 3) & ~3;
      if (height < 0)
      {
        height = -height;
        flip = false;
      }
      uint16_t w = width;
      uint16_t h = height;
      if ((x + w - 1) >= DISPLAY_WIDTH)
        w = DISPLAY_WIDTH - x;
      if ((y + h - 1) >= DISPLAY_HEIGHT)
        h = DISPLAY_HEIGHT - y;
      //if (w <= max_row_width) // handle with direct drawing
      {
        valid = true;
        uint8_t bitmask = 0xFF;
        uint8_t bitshift = 8 - depth;
        uint16_t red, green, blue;
        bool whitish, colored;
        if (depth == 1)
          with_color = false;
        if (depth <= 8)
        {
          if (depth < 8)
            bitmask >>= depth;
          //file.seek(54); //palette is always @ 54
          file.seek(imageOffset - (4 << depth)); // 54 for regular, diff for colorsimportant
          for (uint16_t pn = 0; pn < (1 << depth); pn++)
          {
            blue = file.read();
            green = file.read();
            red = file.read();
            file.read();
            whitish = with_color ? ((red > 0x80) && (green > 0x80) && (blue > 0x80)) : ((red + green + blue) > 3 * 0x80); // whitish
            colored = (red > 0xF0) || ((green > 0xF0) && (blue > 0xF0));                                                  // reddish or yellowish?
            if (0 == pn % 8)
              mono_palette_buffer[pn / 8] = 0;
            mono_palette_buffer[pn / 8] |= whitish << pn % 8;
            if (0 == pn % 8)
              color_palette_buffer[pn / 8] = 0;
            color_palette_buffer[pn / 8] |= colored << pn % 8;
            rgb_palette_buffer[pn] = ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | ((blue & 0xF8) >> 3);
          }
        }
        if (partial_update)
          display.setPartialWindow(x, y, w, h);
        else
          display.setFullWindow();
        display.firstPage();
        do
        {
          if (!overwrite)
            display.fillScreen(GxEPD_WHITE);
          uint32_t rowPosition = flip ? imageOffset + (height - h) * rowSize : imageOffset;
          for (uint16_t row = 0; row < h; row++, rowPosition += rowSize) // for each line
          {
            uint32_t in_remain = rowSize;
            uint32_t in_idx = 0;
            uint32_t in_bytes = 0;
            uint8_t in_byte = 0; // for depth <= 8
            uint8_t in_bits = 0; // for depth <= 8
            uint16_t color = GxEPD_WHITE;
            file.seek(rowPosition);
            for (uint16_t col = 0; col < w; col++) // for each pixel
            {
              // Time to read more pixel data?
              if (in_idx >= in_bytes) // ok, exact match for 24bit also (size IS multiple of 3)
              {
                in_bytes = file.read(input_buffer, in_remain > sizeof(input_buffer) ? sizeof(input_buffer) : in_remain);
                in_remain -= in_bytes;
                in_idx = 0;
              }
              switch (depth)
              {
              case 24:
                blue = input_buffer[in_idx++];
                green = input_buffer[in_idx++];
                red = input_buffer[in_idx++];
                whitish = with_color ? ((red > 0x80) && (green > 0x80) && (blue > 0x80)) : ((red + green + blue) > 3 * 0x80); // whitish
                colored = (red > 0xF0) || ((green > 0xF0) && (blue > 0xF0));                                                  // reddish or yellowish?
                color = ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | ((blue & 0xF8) >> 3);
                break;
              case 16:
              {
                uint8_t lsb = input_buffer[in_idx++];
                uint8_t msb = input_buffer[in_idx++];
                if (format == 0) // 555
                {
                  blue = (lsb & 0x1F) << 3;
                  green = ((msb & 0x03) << 6) | ((lsb & 0xE0) >> 2);
                  red = (msb & 0x7C) << 1;
                  color = ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | ((blue & 0xF8) >> 3);
                }
                else // 565
                {
                  blue = (lsb & 0x1F) << 3;
                  green = ((msb & 0x07) << 5) | ((lsb & 0xE0) >> 3);
                  red = (msb & 0xF8);
                  color = (msb << 8) | lsb;
                }
                whitish = with_color ? ((red > 0x80) && (green > 0x80) && (blue > 0x80)) : ((red + green + blue) > 3 * 0x80); // whitish
                colored = (red > 0xF0) || ((green > 0xF0) && (blue > 0xF0));                                                  // reddish or yellowish?
              }
              break;
              case 1:
              case 4:
              case 8:
              {
                if (0 == in_bits)
                {
                  in_byte = input_buffer[in_idx++];
                  in_bits = 8;
                }
                uint16_t pn = (in_byte >> bitshift) & bitmask;
                whitish = mono_palette_buffer[pn / 8] & (0x1 << pn % 8);
                colored = color_palette_buffer[pn / 8] & (0x1 << pn % 8);
                in_byte <<= depth;
                in_bits -= depth;
                color = rgb_palette_buffer[pn];
              }
              break;
              }
              if (with_color && has_multicolors)
              {
                // keep color
              }
              else if (whitish)
              {
                color = GxEPD_WHITE;
              }
              else if (colored && with_color)
              {
                color = GxEPD_COLORED;
              }
              else
              {
                color = GxEPD_BLACK;
              }
              uint16_t yrow = y + (flip ? h - row - 1 : row);
              display.drawPixel(x + col, yrow, color);
            } // end pixel
          }   // end line
        } while (display.nextPage());
        Serial.print("loaded in ");
        Serial.print(millis() - startTime);
        Serial.println(" ms");
      }
    }
  }
  file.close();
  if (!valid)
  {
    Serial.println("bitmap format not handled.");
  }
}

void DrawMultiLineString(string content, uint16_t x, uint16_t y, uint16_t contentAreaWidthWithMargin, uint16_t lineHeight)
{
  string ch;
  vector<string> contentArray;
  for (size_t i = 0, len = 0; i != content.length(); i += len)
  {
    unsigned char byte = (unsigned)content[i];
    if (byte >= 0xFC)
      len = 6;
    else if (byte >= 0xF8)
      len = 5;
    else if (byte >= 0xF0)
      len = 4;
    else if (byte >= 0xE0)
      len = 3;
    else if (byte >= 0xC0)
      len = 2;
    else
      len = 1;
    ch = content.substr(i, len);
    contentArray.push_back(ch);
  }

  string outputContent;
  for (size_t j = 0; j < contentArray.size(); j++)
  {
    outputContent += contentArray[j];
    int16_t wTemp = u8g2Fonts.getUTF8Width(outputContent.c_str());
    if (wTemp >= contentAreaWidthWithMargin || j == (contentArray.size() - 1))
    {
      u8g2Fonts.drawUTF8(x, y, outputContent.c_str());
      y += lineHeight;
      outputContent = "";
    }
  }
}

// void ShowStartUpImg()
// {
//   //display.clearScreen(GxEPD_WHITE);
//   display.fillScreen(GxEPD_WHITE);

//   display.firstPage();
//   do
//   {
//     display.fillScreen(GxEPD_WHITE);
//   } while (display.nextPage());
//   drawBitmapFromSpiffs_Buffered("output.bmp", 0, 0, false, true, false);
//   delay(2000);
//   display.fillScreen(GxEPD_WHITE);
// }

void ShowWiFiSmartConfig()
{
  //display.clearScreen(GxEPD_WHITE);
  display.fillScreen(GxEPD_WHITE);

  const uint16_t x = (DISPLAY_WIDTH - SMARTCONFIG_IMAGE_WIDTH) / 2;
  const uint16_t y = (DISPLAY_HEIGHT - SMARTCONFIG_IMAGE_HEIGHT) / 2;

  u8g2Fonts.setFont(u8g2_mfyuehei_18_gb2312);
  uint16_t tipsY = y + SMARTCONFIG_IMAGE_HEIGHT + 40;

  display.firstPage();
  do
  {
    //DrawMultiLineString("请用微信扫描二维码或使用 ESPTouch 配置网络。", 90, tipsY, 300, 30);
    //DrawMultiLineString("请用微信扫描二维码或使用 ESPTouch 配置网络", 90, tipsY, 300, 30);
    DrawMultiLineString("请使用 ESPTouch 配置网络", 90, tipsY, 300, 30);
  } while (display.nextPage());
  //drawBitmapFromSpiffs_Buffered("smartconfig.bmp", x, y, false, true, false);
  drawBitmapFromSpiffs_Buffered("64/100.bmp", x, y, false, true, false);
}

enum PageContent : u8_t
{
  CALENDAR = false,
  WEATHER = true
};

void ShowLunar()
{
  HTTPClient http;
  http.begin("https://api.muxiaoguo.cn/api/yinlongli");//Specify the URL
  int httpCode = http.GET();            //Make the request
  if (httpCode > 0) 
  { //Check for the returning code
    String payload = http.getString();
    //Serial.println(httpCode);
    Serial.println(payload);

    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      Serial.print("deserializeJson() failed: ");
      Serial.println(error.c_str());
      return;
    }

    const char* code = doc["code"]; // "200"
    const char* msg = doc["msg"]; // "success"

    JsonObject data = doc["data"];
    const char* data_lunar = data["lunar"]; // "辛丑年腊月二十"
    const char* data_solar_terms = data["solar_terms"]; // "大寒后"
    u8g2Fonts.setFont(u8g2_mfyuehei_12_gb2312);
    String LunarDate = data_lunar;
    String SolarTerms = data_solar_terms;
    String lunarDate = LunarDate + " " + SolarTerms;
    int16_t lunarDateWidth = u8g2Fonts.getUTF8Width(lunarDate.c_str());
    u8g2Fonts.drawUTF8((DISPLAY_WIDTH - lunarDateWidth - 80), 64 + 24, lunarDate.c_str());
  }
}

void ShowPageHeader()
{
  u8g2Fonts.setFont(u8g2_mfyuanhei_16_gb2312);
  u8g2Fonts.drawUTF8(48, 64, DateTime.format(DateFormatter::DATE_ONLY).c_str());

  int16_t cityNameWidth = u8g2Fonts.getUTF8Width(gi.name.c_str());
  //u8g2Fonts.drawUTF8((DISPLAY_WIDTH - cityNameWidth - 48), 64, gi.name.c_str());
  u8g2Fonts.drawUTF8((DISPLAY_WIDTH - cityNameWidth - 80), 64, gi.name.c_str());

  u8g2Fonts.setFont(u8g2_mfyuehei_14_gb2312);
  u8g2Fonts.drawUTF8(48, 64 + 24, WEEKDAY_EN[DateTime.getParts().getWeekDay()]);
}

void ShowCurrentDate()
{
  String dateInCenter = String(DateTime.getParts().getMonthDay());
  int m = DateTime.getParts().getMonth();

  u8g2Fonts.setFont(u8g2_mfxinran_92_number);
  int16_t dateWidth = u8g2Fonts.getUTF8Width(dateInCenter.c_str());
  u8g2Fonts.drawUTF8((DISPLAY_WIDTH - dateWidth) / 2, 300, dateInCenter.c_str());

  u8g2Fonts.setFont(u8g2_mfyuehei_14_gb2312);
  int16_t monthWidth = u8g2Fonts.getUTF8Width(MONTH_EN[m]);
  u8g2Fonts.drawUTF8((DISPLAY_WIDTH - monthWidth) / 2, 340, MONTH_EN[m]);
}

// void ShowTEMWord()
// {
//   u16_t r = random(TEMWordCount);
//   const char *word = TEMWord[r];
//   Serial.printf("pick %u: %s\n", r, word);
//   u8g2Fonts.setFont(u8g2_mfyuehei_18_gb2312);
//   //DrawMultiLineString(string(word), 80, 420, 300, 36);
//   DrawMultiLineString(string(word), 50, 410, 340, 36);
// }

// void ShowToxicSoul()
// {
//   u16_t r = random(ToxicSoulCount);
//   const char *soul = ToxicSoul[r];
//   Serial.printf("pick %u: %s\n", r, soul);
//   u8g2Fonts.setFont(u8g2_mfyuehei_18_gb2312);
//   DrawMultiLineString(string(soul), 80, 420, 300, 36);
// }

void ShowHitokoto()
{
  HTTPClient http;
  http.begin("https://v1.hitokoto.cn/");//Specify the URL
  int httpCode = http.GET();            //Make the request
  if (httpCode > 0) { //Check for the returning code

    String payload = http.getString();
    //Serial.println(httpCode);
    Serial.println(payload);

    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      Serial.println("JSON parsing failed!");
    } else {
      if (strlen(doc["hitokoto"]) > sizeof(Hitokoto.hitokoto)) {
        http.end();
        return;
      }
      strcpy(Hitokoto.hitokoto, doc["hitokoto"]);
      strcpy(Hitokoto.from, doc["from"]);
    }
  } else {
    Serial.println("Error on HTTP request");
  }
  http.end(); //Free the resources
  
  //Serial.printf("%s from: %s\n", Hitokoto.hitokoto, Hitokoto.from); 
  String headHitokoto = Hitokoto.hitokoto;
  String tailHitokoto = " ---- ";
  tailHitokoto.concat(Hitokoto.from);
  headHitokoto.concat(tailHitokoto);
  Serial.println(headHitokoto);
  u8g2Fonts.setFont(u8g2_mfyuehei_18_gb2312);
  char allHitokoto[512];
  headHitokoto.toCharArray(allHitokoto, 512);
  DrawMultiLineString(string(allHitokoto), 80, 420, 300, 36);
}

void ShowTodoist()
{
  HTTPClient http;
  http.begin("https://api.todoist.com/rest/v1/tasks?token=" + TODOIST_ACCESS_TOKEN);//Specify the URL
  int httpCode = http.GET();            //Make the request
  if (httpCode > 0) { //Check for the returning code
    String payload = http.getString();
    //Serial.println(httpCode);
    Serial.println(payload);
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
      Serial.print("deserializeJson() failed: ");
      Serial.println(error.c_str());
      return;
    }
    u8g2Fonts.setFont(u8g2_mfyuehei_14_gb2312);
    String toDoInCenter = "**待办事项**";
    int16_t toDoWidth = u8g2Fonts.getUTF8Width(toDoInCenter.c_str());
    u8g2Fonts.drawUTF8((DISPLAY_WIDTH - toDoWidth) / 2, 540, toDoInCenter.c_str());
    //u8g2Fonts.drawUTF8(80, 540, "**待办事项**");
    int sizeY = 570;
    for (JsonObject item : doc.as<JsonArray>()) 
    {
      const char* content = item["content"]; 
      Serial.println(content);
      u8g2Fonts.setFont(u8g2_mfyuehei_12_gb2312);
      String headerTodoist = "@_@ ";
      headerTodoist.concat(content);
      char allTodoist[2048];
      headerTodoist.toCharArray(allTodoist, 2048);
      DrawMultiLineString(string(allTodoist), 80, sizeY, 300, 36);
      sizeY = sizeY + 25;
    }
  }
}

void ShowWeatherFoot()
{
  String foot = cw.text;
  foot.concat(" / 空气质量等级-");
  foot.concat(caq.category);
  foot.concat(" / 体感温度 ");
  foot.concat(cw.feelsLike);
  foot.concat("°C");

  u8g2Fonts.setFont(u8g2_mfyuehei_14_gb2312);
  /*显示在底部*/
  u8g2Fonts.drawUTF8(88, DISPLAY_HEIGHT - 24, foot.c_str()); 
  /*显示在日期上面*/
  //u8g2Fonts.drawUTF8(88, DISPLAY_HEIGHT - 500, foot.c_str()); 
}

void ShowWeatherContent()
{

  u8g2Fonts.setFont(u8g2_deng_56_temperature);
  String currentAQIString = caq.aqi;
  int16_t tempWidth = u8g2Fonts.getUTF8Width(currentAQIString.c_str());
  u8g2Fonts.drawUTF8(((DISPLAY_WIDTH / 2) - tempWidth + DISPLAY_WIDTH) / 2, 190, currentAQIString.c_str());

  u8g2Fonts.setFont(u8g2_mfyuehei_14_gb2312);
  String currentAQICategoryString = "空气质量:";
  currentAQICategoryString.concat(caq.category);
  int16_t categoryWidth = u8g2Fonts.getUTF8Width(currentAQICategoryString.c_str());
  u8g2Fonts.drawUTF8(((DISPLAY_WIDTH / 2) - categoryWidth + DISPLAY_WIDTH) / 2, 245, currentAQICategoryString.c_str());

  String currentPM25 = "PM2.5: ";
  currentPM25.concat(caq.pm2p5);
  int16_t pm25Width = u8g2Fonts.getUTF8Width(currentPM25.c_str());
  u8g2Fonts.drawUTF8(((DISPLAY_WIDTH / 2) - pm25Width + DISPLAY_WIDTH) / 2, 275, currentPM25.c_str());

  String currentPM10 = "PM10: ";
  currentPM10.concat(caq.pm10);
  int16_t pm10Width = u8g2Fonts.getUTF8Width(currentPM10.c_str());
  u8g2Fonts.drawUTF8(((DISPLAY_WIDTH / 2) - pm10Width + DISPLAY_WIDTH) / 2, 305, currentPM10.c_str());

  String l1 = cw.text;
  int16_t l1W = u8g2Fonts.getUTF8Width(l1.c_str());
  u8g2Fonts.drawUTF8(((DISPLAY_WIDTH / 2) - l1W) / 2, 230, l1.c_str());

  String l2 = cw.windDir;
  l2.concat(cw.windScale);
  l2.concat("级");
  int16_t l2W = u8g2Fonts.getUTF8Width(l2.c_str());
  u8g2Fonts.drawUTF8(((DISPLAY_WIDTH / 2) - l2W) / 2, 260, l2.c_str());

  String qwfw = dws[0].tempMin;
  qwfw.concat("° ~ ");
  qwfw.concat(dws[0].tempMax);
  qwfw.concat("°");
  int16_t dqwdWidth = u8g2Fonts.getUTF8Width(qwfw.c_str());
  u8g2Fonts.drawUTF8(((DISPLAY_WIDTH / 2) - dqwdWidth) / 2, 290, qwfw.c_str());

  String tempCurrent = "当前气温";
  tempCurrent.concat(cw.temp);
  tempCurrent.concat("°");
  int16_t tempCurrentWidth = u8g2Fonts.getUTF8Width(tempCurrent.c_str());
  u8g2Fonts.drawUTF8(((DISPLAY_WIDTH / 2) - tempCurrentWidth) / 2, 320, tempCurrent.c_str());

  display.drawLine(DISPLAY_WIDTH / 2, 140, DISPLAY_WIDTH / 2, 330, GxEPD_BLACK);

  u8g2Fonts.setFont(u8g2_mfyuehei_12_gb2312);
  String test = "-22° ~ -88°";
  Serial.printf("每天需要的气温宽度:%u\n", u8g2Fonts.getUTF8Width(test.c_str()));
}

void ShowPage(PageContent pageContent)
{
  // TODO: 应该判断下咋决定是否刷新。例如距离上次请求超过多少小时再请求。
  cw = qwAPI.GetCurrentWeather(gi.id);
  caq = qwAPI.GetCurrentAirQuality(gi.id);
  dws = qwAPI.GetDailyWeather(gi.id);

  display.setFullWindow();
  //display.clearScreen(GxEPD_WHITE);
  display.fillScreen(GxEPD_WHITE);
  display.setRotation(3);
  u8g2Fonts.setFontMode(1);                  // use u8g2 transparent mode (this is default)
  u8g2Fonts.setFontDirection(0);             // left to right (this is default)
  u8g2Fonts.setForegroundColor(GxEPD_BLACK); // apply Adafruit GFX color
  u8g2Fonts.setBackgroundColor(GxEPD_WHITE); // apply Adafruit GFX color

  String iconFileSmall = "32/";
  iconFileSmall.concat(cw.icon);
  iconFileSmall.concat(".bmp");
  String iconFileBig = "64/";
  iconFileBig.concat(cw.icon);
  iconFileBig.concat(".bmp");
  /**
   * @brief 先写文字
   * 
   */
  display.firstPage();
  do
  {
    /**
     * @brief 头部都用一样的吧
     * 
     */
    ShowPageHeader();
    ShowLunar();

    switch (pageContent)
    {
    case PageContent::CALENDAR:

      ShowCurrentDate();
      break;
    case PageContent::WEATHER:
      ShowWeatherContent();
      break;
    }

    ShowHitokoto();
    ShowTodoist();
    ShowWeatherFoot();

  } while (display.nextPage());

  /**
   * @brief 貌似没法让u8g2和平共处，所以只能先输出文字再输出需要显示的图片，导致整体刷新时间太长。
   * 
   */
  //显示天气图标
  switch (pageContent)
  {
  case PageContent::CALENDAR:
    drawBitmapFromSpiffs_Buffered(iconFileSmall.c_str(), 48, DISPLAY_HEIGHT - 48, false, true, false);
    break;
  case PageContent::WEATHER:
    drawBitmapFromSpiffs_Buffered(iconFileSmall.c_str(), 48, DISPLAY_HEIGHT - 48, false, true, false);
    //drawBitmapFromSpiffs_Buffered(iconFileSmall.c_str(), 48, DISPLAY_HEIGHT - 500, false, true, false);
    drawBitmapFromSpiffs_Buffered(iconFileBig.c_str(), 88, 140, false, true, false);
    break;
  }
}

void setup()
{

  Serial.begin(115200);
  Serial.println();
  Serial.println("setup");

  pinMode(led, OUTPUT);
  digitalWrite(led, HIGH);   //开灯

  //Print the wakeup reason for ESP32
  print_wakeup_reason();

  SPI.end();
  SPI.begin(13, 12, 14, 15);

  // Initialise SPIFFS
  if (!SPIFFS.begin(true))
  {
    Serial.println("SPIFFS initialisation failed!");
    while (1)
      yield(); // Stay here twiddling thumbs waiting
  }
  Serial.println("\r\nSPIFFS Initialisation done.");

  display.init();
  display.setRotation(3);
  DISPLAY_WIDTH = display.width();
  DISPLAY_HEIGHT = display.height();

  u8g2Fonts.begin(display);                  // connect u8g2 procedures to Adafruit GFX
  u8g2Fonts.setFontMode(1);                  // use u8g2 transparent mode (this is default)
  u8g2Fonts.setFontDirection(0);             // left to right (this is default)
  u8g2Fonts.setForegroundColor(GxEPD_BLACK); // apply Adafruit GFX color
  u8g2Fonts.setBackgroundColor(GxEPD_WHITE); // apply Adafruit GFX color

  SmartConfigManager scm;
  scm.initWiFi(ShowWiFiSmartConfig);



  //ShowStartUpImg();
  //开机动画

  qwAPI.Config(QWEATHER_API_KEY);


  // IPAPIResponse ipAPIResponse = GetIPInfomation(); //IPAPI
  // if(ipAPIResponse.status != "success"){
  //   Serial.printf("Get ip information failed:%s\n Restarting......\n",ipAPIResponse.message);
  //   esp_restart();
  // }
  // Serial.printf("IP: %s\n", ipAPIResponse.query.c_str());
  // Serial.printf("City: %s\n", ipAPIResponse.city.c_str());


  MyIP myIP(Language::CHINESE); //MyIP
  Serial.printf("IP: %s\n", myIP.IP.c_str());
  Serial.printf("City: %s\n", myIP.City.c_str());

  //gi = qwAPI.GetGeoInfo(ipAPIResponse.city, ipAPIResponse.regionName);
  gi = qwAPI.GetGeoInfo(myIP.City, myIP.Province);
  Serial.printf("从和风天气中取到匹配城市: %s\n", gi.name.c_str());

  setupDateTime();

  ++LASTPAGE;
  if (LASTPAGE > PageContent::WEATHER)
    LASTPAGE = PageContent::CALENDAR;

  ShowPage((PageContent)LASTPAGE);
  display.hibernate();

  Serial.println("-------  SETUP FINISHED  -----------");
  Serial.println("睡吧。。。睡吧。。。zzzzzZZZZZZZ~~ ~~ ~~");
  Serial.flush();

  digitalWrite(led, LOW);  //关灯

  /**
   * @brief 关掉蓝牙和WiFi,进入Deep sleep模式
   * 
   */
  esp_bt_controller_disable();
  esp_wifi_stop();
  esp_sleep_enable_timer_wakeup((uint64_t)TIME_TO_SLEEP * uS_TO_S_FACTOR);
  Serial.println("Setup ESP32 to sleep for every " + String(TIME_TO_SLEEP) + " Seconds");
  esp_deep_sleep_start();
}

void loop()
{
}
