// PlatformIO required include
#include <Arduino.h>

#include <TFT_eSPI.h>
#include <NTPClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>

// Prepare WiFi
const char* ssid = "RD_Hunonic_Mesh";                                         // Replace with your
const char* password = "66668888";                                          // WiFi credentials

// Search your next airport here and get the ICAO code
// https://en.wikipedia.org/wiki/ICAO_airport_code
const char* metar = "https://aviationweather.gov/api/data/metar?ids=KDEN&format=json"; // KDEN = Denver
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
int wifiTimeOutCounter = 0;
#define TIMEOFFSET -3600 * 5  // no daylight saving time

// Init parameter for universe simulation - giảm độ phức tạp để tăng hiệu suất
int const n = 3, m = 100; // Giảm số lượng điểm để tránh làm chậm và che mất văn bản
float const r = 0.1;
float x = 0, v = 0, t = 0;  // v is used in the loop animation

// Definition for the screen
#define MAX_Y 170
#define MAX_X 320
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);  // Declare Sprite object "spr" with pointer to "tft" object
uint16_t palette[16] = { TFT_GREENYELLOW, TFT_NAVY, TFT_ORANGE, TFT_DARKCYAN, TFT_MAROON,
                         TFT_PURPLE, TFT_PINK, TFT_LIGHTGREY, TFT_YELLOW, TFT_BLUE,
                         TFT_GREEN, TFT_CYAN, TFT_RED, TFT_MAGENTA, TFT_BLUE, TFT_WHITE };

// Set global variables for the weather information
int temperature = 0;  // °C
int dew_point = 0;    // °C
int wind_speed_knots = 0;
int pressure = 0;           // hPa
int relative_humidity = 0;  // %
int wind_speed_kmh = 0;
int data_age_min = 0;
unsigned long epochTime = 0;
unsigned long obsTime = 0;

// Update parameter for the weather data
unsigned long previousMillis = 0;
#define INTERVAL 60000 * 5   // 5 min

// Connect to weather server and get data
void weatherData() {
  // Thay vì lấy dữ liệu từ API, sử dụng các giá trị cố định
  // Các giá trị được cung cấp sẵn
  temperature = 21;        // °C
  relative_humidity = 49;  // %
  wind_speed_kmh = 22;     // km/h
  pressure = 1023;         // hPa
  data_age_min = 42359135; // min

  // Hiển thị thông tin trên Serial
  Serial.print("Temperature: ");
  Serial.print(temperature, 0);
  Serial.println();
  Serial.print("Relative Humidity: ");
  Serial.print(relative_humidity, 0);
  Serial.println("%");
  Serial.print("Wind Speed: ");
  Serial.print(wind_speed_kmh, 0);
  Serial.println("km/h");
  Serial.print("Pressure: ");
  Serial.print(pressure, 0);
  Serial.println("hPa");
  Serial.print("Data age: ");
  Serial.print(data_age_min);
  Serial.println("min");
}

// This is missing in the library
String getFormattedDate() {
#define LEAP_YEAR(Y) ((Y > 0) && !(Y % 4) && ((Y % 100) || !(Y % 400)))
  unsigned long rawTime = timeClient.getEpochTime() / 86400L;
  unsigned long days = 0, year = 1970;
  uint8_t month;
  static const uint8_t monthDays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  while ((days += (LEAP_YEAR(year) ? 366 : 365)) <= rawTime) year++;
  rawTime -= days - (LEAP_YEAR(year) ? 366 : 365);
  for (month = 0; month < 12; month++) {
    uint8_t monthLength;
    if (month == 1) monthLength = LEAP_YEAR(year) ? 29 : 28;
    else monthLength = monthDays[month];
    if (rawTime < monthLength) break;
    rawTime -= monthLength;
  }
  String monthStr = ++month < 10 ? "0" + String(month) : String(month);
  String dayStr = ++rawTime < 10 ? "0" + String(rawTime) : String(rawTime);
  return String(dayStr) + "-" + monthStr + "-" + year;
}

void setup() {
  // Entry point
  Serial.begin(115200);
  while (!Serial) delay(10);
  Serial.println("Booting...");
  
  // Print debug infos on serial
  uint32_t chipId = 0;
  for (int i = 0; i < 17; i = i + 8) chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  Serial.printf("ESP32 Chip model = %s Rev %d\n", ESP.getChipModel(), ESP.getChipRevision());
  Serial.printf("This chip has %d cores\n", ESP.getChipCores());
  Serial.print("Chip ID: ");
  Serial.println(chipId);
  Serial.print("Connecting to ");
  Serial.print(ssid);
  Serial.print(" ");
  
  // Connect to router
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    wifiTimeOutCounter++;
    if (wifiTimeOutCounter >= 60) ESP.restart();
  }
  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  uint8_t mac[6];
  WiFi.macAddress(mac);
  Serial.print("MAC address: ");
  for (int i = 0; i < 6; i++) {
    Serial.print(mac[i], HEX);
    if (i < 5) Serial.print(":");
  }
  Serial.println("");
  
  // Convert MAC address to unique identifier (UID)
  uint64_t uid = ((uint64_t)mac[0] << 40) | ((uint64_t)mac[1] << 32) | ((uint64_t)mac[2] << 24) | 
                ((uint64_t)mac[3] << 16) | ((uint64_t)mac[4] << 8) | ((uint64_t)mac[5]);
  Serial.print("MAC unique identifier: ");
  Serial.println(uid);
  
  // Init time system
  timeClient.begin();
  timeClient.setTimeOffset(TIMEOFFSET);
  
  // Init TFT
  tft.init();
  tft.setRotation(1);  // Đặt rotation trước khi tạo sprite
  tft.fillScreen(TFT_BLACK); // Xóa toàn bộ màn hình
  
  spr.setColorDepth(4);
  spr.createSprite(MAX_X, MAX_Y);

  // Load first data to start with
  timeClient.update();
  weatherData();
}

void loop() {
  spr.fillSprite(1);  // Init sprite with dark color (1 = dark blue) thay vì 0 (đen)

  // Hiệu ứng nền - vẽ với độ sáng thấp hơn để không làm lóa văn bản
  // Draw universe simulation in empty sprite - giảm độ sáng
  for (int i = 0; i <= n; i++)
    for (int j = 0; j <= m; j++) {
      float u = sin(i + v) + sin(r * i + x);
      float new_v = cos(i + v) + cos(r * i + x); // Store in temporary variable first
      x = u + t;
      int px = u * MAX_X / 4 + MAX_X / 2;
      int py = new_v * MAX_Y / 4 + MAX_Y / 2; // Use new_v here
      v = new_v; // Update v after calculations
      uint16_t color = (i * 255) % 16;
      // Giảm số lượng pixel vẽ - chỉ vẽ 1 pixel thay vì 4
      spr.drawPixel(px, py, color);
    }
  t += 0.01;

  // Không cần cập nhật thông tin thời tiết vì đã có giá trị cố định

  // Xóa một phần màn hình để hiển thị văn bản rõ hơn
  // Tạo một nền bán trong suốt để văn bản dễ đọc hơn
  spr.fillRect(0, 30, MAX_X, 120, 0);  // Xóa khu vực trung tâm để hiển thị thời gian và ngày

  // Đặt màu cho văn bản - màu sáng hơn để dễ nhìn
  spr.setTextColor(15); // Dùng màu trắng (15) cho văn bản
  
  timeClient.update();
  String strtmp = timeClient.getFormattedTime() + "  " + getFormattedDate();
  
  // Hiển thị thông tin ngay cả khi thời gian chưa được đặt
  // Thời gian và ngày tháng
  spr.drawString(timeClient.getFormattedTime(), 50, 40, 6);
  spr.drawString(getFormattedDate(), 40, 80, 4);
  
  // Tạo một nền cho thông tin thời tiết
  spr.fillRect(0, 120, MAX_X, 50, 0);
  
  // Hiển thị thông tin thời tiết với font size lớn hơn và cách trình bày rõ ràng hơn
  String outputString = String(temperature) + "C " + String(relative_humidity) + "% " + 
                        String(wind_speed_kmh) + "km/h " + String(pressure) + "hPa";
                        
  // Thêm tiêu đề cho thông tin thời tiết
  spr.drawString("Weather Info:", 20, 125, 2);
  spr.drawString(outputString, 10, 145, 4);
  
  // Push sprite để cập nhật màn hình
  spr.pushSprite(0, 0);
}
