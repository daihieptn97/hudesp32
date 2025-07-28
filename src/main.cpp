#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <Button2.h>
#include <TFT_eSPI.h>
#include <SPI.h>

// Basic Latin + Vietnamese characters (Unicode U+00C0 to U+1EF9)
const uint8_t vietnameseCharMap[] = {
  // Space for ASCII characters (0-127)
  32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
  48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
  64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
  80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95,
  96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111,
  112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127,
  // Extended Vietnamese characters
  'A', 'a', 'A', 'a', 'A', 'a', 'A', 'a', 'A', 'a', 'A', 'a', 'A', 'a', 'C',
  'c', 'E', 'e', 'E', 'e', 'E', 'e', 'E', 'e', 'I', 'i', 'I', 'i', 'I', 'i',
  'I', 'i', 'D', 'd', 'N', 'n', 'O', 'o', 'O', 'o', 'O', 'o', 'O', 'o', 'O',
  'o', 'O', 'o', 'U', 'u', 'U', 'u', 'U', 'u', 'U', 'u', 'Y', 'y'
};

// Special mapping for Vietnamese diacritical marks
struct VietnameseChar {
  uint8_t utf8[4]; // UTF-8 sequence
  int len;         // Length of UTF-8 sequence
  char base;       // Base character to display
};

// Common Vietnamese characters
const VietnameseChar vnChars[] = {
  // a, ă, â với các dấu
  {{0xC3, 0xA1}, 2, 'a'}, // á
  {{0xC3, 0xA0}, 2, 'a'}, // à
  {{0xE1, 0xBA, 0xA3}, 3, 'a'}, // ả
  {{0xC3, 0xA3}, 2, 'a'}, // ã
  {{0xE1, 0xBA, 0xA1}, 3, 'a'}, // ạ
  
  {{0xC4, 0x83}, 2, 'a'}, // ă
  {{0xE1, 0xBA, 0xAF}, 3, 'a'}, // ắ
  {{0xE1, 0xBA, 0xAD}, 3, 'a'}, // ặ
  {{0xE1, 0xBA, 0xAB}, 3, 'a'}, // ằ
  {{0xE1, 0xBA, 0xB1}, 3, 'a'}, // ằ
  {{0xE1, 0xBA, 0xB3}, 3, 'a'}, // ẳ
  
  {{0xC3, 0xA2}, 2, 'a'}, // â
  {{0xE1, 0xBA, 0xA5}, 3, 'a'}, // ấ
  {{0xE1, 0xBA, 0xA7}, 3, 'a'}, // ầ
  {{0xE1, 0xBA, 0xA9}, 3, 'a'}, // ẩ
  {{0xE1, 0xBA, 0xAB}, 3, 'a'}, // ẫ
  {{0xE1, 0xBA, 0xAD}, 3, 'a'}, // ậ
  
  // e, ê với các dấu
  {{0xC3, 0xA9}, 2, 'e'}, // é
  {{0xC3, 0xA8}, 2, 'e'}, // è
  {{0xE1, 0xBA, 0xBB}, 3, 'e'}, // ẻ
  {{0xE1, 0xBA, 0xBD}, 3, 'e'}, // ẽ
  {{0xE1, 0xBA, 0xB9}, 3, 'e'}, // ẹ
  
  {{0xC3, 0xAA}, 2, 'e'}, // ê
  {{0xE1, 0xBA, 0xBF}, 3, 'e'}, // ế
  {{0xE1, 0xBB, 0x81}, 3, 'e'}, // ề
  {{0xE1, 0xBB, 0x83}, 3, 'e'}, // ể
  {{0xE1, 0xBB, 0x85}, 3, 'e'}, // ễ
  {{0xE1, 0xBB, 0x87}, 3, 'e'}, // ệ
  
  // i với các dấu
  {{0xC3, 0xAD}, 2, 'i'}, // í
  {{0xC3, 0xAC}, 2, 'i'}, // ì
  {{0xE1, 0xBB, 0x89}, 3, 'i'}, // ỉ
  {{0xC4, 0xA9}, 2, 'i'}, // ĩ
  {{0xE1, 0xBB, 0x8B}, 3, 'i'}, // ị
  
  // o, ô, ơ với các dấu
  {{0xC3, 0xB3}, 2, 'o'}, // ó
  {{0xC3, 0xB2}, 2, 'o'}, // ò
  {{0xE1, 0xBB, 0x8F}, 3, 'o'}, // ỏ
  {{0xC3, 0xB5}, 2, 'o'}, // õ
  {{0xE1, 0xBB, 0x8D}, 3, 'o'}, // ọ
  
  {{0xC3, 0xB4}, 2, 'o'}, // ô
  {{0xE1, 0xBB, 0x91}, 3, 'o'}, // ố
  {{0xE1, 0xBB, 0x93}, 3, 'o'}, // ồ
  {{0xE1, 0xBB, 0x95}, 3, 'o'}, // ổ
  {{0xE1, 0xBB, 0x97}, 3, 'o'}, // ỗ
  {{0xE1, 0xBB, 0x99}, 3, 'o'}, // ộ
  
  {{0xC6, 0xA1}, 2, 'o'}, // ơ
  {{0xE1, 0xBB, 0x9B}, 3, 'o'}, // ớ
  {{0xE1, 0xBB, 0x9D}, 3, 'o'}, // ờ
  {{0xE1, 0xBB, 0x9F}, 3, 'o'}, // ở
  {{0xE1, 0xBB, 0xA1}, 3, 'o'}, // ỡ
  {{0xE1, 0xBB, 0xA3}, 3, 'o'}, // ợ
  
  // u, ư với các dấu
  {{0xC3, 0xBA}, 2, 'u'}, // ú
  {{0xC3, 0xB9}, 2, 'u'}, // ù
  {{0xE1, 0xBB, 0xA7}, 3, 'u'}, // ủ
  {{0xC5, 0xA9}, 2, 'u'}, // ũ
  {{0xE1, 0xBB, 0xA5}, 3, 'u'}, // ụ
  
  {{0xC6, 0xB0}, 2, 'u'}, // ư
  {{0xE1, 0xBB, 0xA9}, 3, 'u'}, // ứ
  {{0xE1, 0xBB, 0xAB}, 3, 'u'}, // ừ
  {{0xE1, 0xBB, 0xAD}, 3, 'u'}, // ử
  {{0xE1, 0xBB, 0xAF}, 3, 'u'}, // ữ
  {{0xE1, 0xBB, 0xB1}, 3, 'u'}, // ự
  
  // y với các dấu
  {{0xC3, 0xBD}, 2, 'y'}, // ý
  {{0xE1, 0xBB, 0xB3}, 3, 'y'}, // ỳ
  {{0xE1, 0xBB, 0xB7}, 3, 'y'}, // ỷ
  {{0xE1, 0xBB, 0xB9}, 3, 'y'}, // ỹ
  {{0xE1, 0xBB, 0xB5}, 3, 'y'}, // ỵ
  
  // đ
  {{0xC4, 0x91}, 2, 'd'}, // đ
  
  // Các ký tự hoa
  {{0xC3, 0x81}, 2, 'A'}, // Á
  {{0xC3, 0x80}, 2, 'A'}, // À
  {{0xC3, 0x82}, 2, 'A'}, // Â
  {{0xC4, 0x82}, 2, 'A'}, // Ă
  {{0xC4, 0x90}, 2, 'D'}, // Đ
  {{0xC3, 0x89}, 2, 'E'}, // É
  {{0xC3, 0x88}, 2, 'E'}, // È
  {{0xC3, 0x8A}, 2, 'E'}, // Ê
  {{0xC3, 0x8D}, 2, 'I'}, // Í
  {{0xC3, 0x8C}, 2, 'I'}, // Ì
  {{0xC3, 0x93}, 2, 'O'}, // Ó
  {{0xC3, 0x92}, 2, 'O'}, // Ò
  {{0xC3, 0x94}, 2, 'O'}, // Ô
  {{0xC6, 0xA0}, 2, 'O'}, // Ơ
  {{0xC3, 0x9A}, 2, 'U'}, // Ú
  {{0xC3, 0x99}, 2, 'U'}, // Ù
  {{0xC6, 0xAF}, 2, 'U'}, // Ư
  {{0xC3, 0x9D}, 2, 'Y'}, // Ý
};

/**
 * Hàm chuyển đổi văn bản tiếng Việt có dấu sang tiếng Việt không dấu
 * @param text Chuỗi văn bản tiếng Việt có dấu cần chuyển đổi
 * @return Chuỗi văn bản tiếng Việt không dấu
 */
String convertToNonAccentVietnamese(const String& text) {
  String result = "";
  int index = 0;
  
  while (index < text.length()) {
    uint8_t c = text[index];
    
    // Ký tự ASCII thông thường (không cần xử lý)
    if (c < 128) {
      result += (char)c;
      index++;
      continue;
    }
    
    // Tìm kiếm ký tự tiếng Việt có dấu trong danh sách
    bool found = false;
    for (int i = 0; i < sizeof(vnChars)/sizeof(vnChars[0]); i++) {
      const VietnameseChar &vc = vnChars[i];
      
      // Kiểm tra xem có đủ ký tự còn lại để khớp không
      if (index + vc.len <= text.length()) {
        bool match = true;
        for (int j = 0; j < vc.len; j++) {
          if ((uint8_t)text[index + j] != vc.utf8[j]) {
            match = false;
            break;
          }
        }
        
        if (match) {
          // Thêm ký tự không dấu tương ứng vào kết quả
          result += vc.base;
          index += vc.len;
          found = true;
          break;
        }
      }
    }
    
    // Nếu không tìm thấy, giữ nguyên ký tự và chuyển tới byte UTF-8 hợp lệ tiếp theo
    if (!found) {
      result += '?'; // Thay thế bằng dấu hỏi cho các ký tự không xác định được
      do {
        index++;
      } while (index < text.length() && (text[index] & 0xC0) == 0x80);
    }
  }
  
  return result;
}

// Function to print special Vietnamese characters
// Phương pháp 1: Hiển thị có dấu gạch chân để chỉ ra đây là ký tự có dấu
void printVietnameseText(TFT_eSPI &tft, int x, int y, const String &text) {
  int currentX = x;
  int index = 0;
  
  while (index < text.length()) {
    uint8_t c = text[index];
    
    // Regular ASCII character
    if (c < 128) {
      tft.drawChar(c, currentX, y, 2);
      currentX += tft.textWidth(String((char)c));
      index++;
      continue;
    }
    
    // Try to find a matching Vietnamese character
    bool found = false;
    for (int i = 0; i < sizeof(vnChars)/sizeof(vnChars[0]); i++) {
      const VietnameseChar &vc = vnChars[i];
      
      // Check if there's enough characters left to match
      if (index + vc.len <= text.length()) {
        bool match = true;
        for (int j = 0; j < vc.len; j++) {
          if ((uint8_t)text[index + j] != vc.utf8[j]) {
            match = false;
            break;
          }
        }
        
        if (match) {
          // Draw base character with a marker to indicate diacritical marks
          tft.drawChar(vc.base, currentX, y, 2);
          // Add underline to indicate this is a character with diacritics
          int charWidth = tft.textWidth(String(vc.base));
          tft.drawLine(currentX, y+14, currentX+charWidth, y+14, TFT_RED);
          currentX += charWidth;
          index += vc.len;
          found = true;
          break;
        }
      }
    }
    
    // Unknown UTF-8 character - just skip it
    if (!found) {
      tft.drawChar('?', currentX, y, 2);
      currentX += tft.textWidth("?");
      // Skip to next valid UTF-8 byte
      do {
        index++;
      } while (index < text.length() && (text[index] & 0xC0) == 0x80);
    }
  }
}

// Phương pháp 2: Hiển thị trực tiếp text không dấu
void printNonAccentVietnamese(TFT_eSPI &tft, int x, int y, const String &text) {
  // Chuyển đổi sang tiếng Việt không dấu
  String nonAccentText = convertToNonAccentVietnamese(text);
  
  // Hiển thị text không dấu
  tft.setCursor(x, y);
  tft.setTextSize(1);
  tft.print(nonAccentText);
}

// LCD Display
TFT_eSPI tft = TFT_eSPI();

// BLE Server Name
#define DEVICE_NAME "ESP32_BT_Display"

// Define UUIDs for service and characteristics (using standard UART UUIDs)
// Standard UART service UUID that most BLE terminals recognize
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

BLEServer* pServer = NULL;
BLECharacteristic* pTxCharacteristic = NULL;  // For sending data to phone
BLECharacteristic* pRxCharacteristic = NULL;  // For receiving data from phone
bool deviceConnected = false;
bool oldDeviceConnected = false;
String receivedData = "";

// Biến cờ để quản lý truy cập an toàn vào màn hình TFT từ callback BLE
volatile bool hasNewMessage = false;
String displayMessage = "";
volatile bool isTFTBusy = false;

// Callback for when a device connects or disconnects
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("Device connected");
      
      // Đặt cờ để vòng lặp chính cập nhật màn hình
      displayMessage = "Connected to phone";
      hasNewMessage = true;
    }

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("Device disconnected");
      
      // Đặt cờ để vòng lặp chính cập nhật màn hình
      displayMessage = "Disconnected";
      hasNewMessage = true;
      
      // Restart advertising to allow new connections
      pServer->getAdvertising()->start();
    }
};

// Debug function to show raw bytes of a string
void debugUTF8String(const String& str) {
  Serial.print("String length: ");
  Serial.println(str.length());
  Serial.print("UTF-8 Bytes: ");
  
  for (int i = 0; i < str.length(); i++) {
    Serial.printf("%02X ", (uint8_t)str[i]);
  }
  Serial.println();
  
  // Print the byte sequence analysis for each character
  int index = 0;
  while (index < str.length()) {
    uint8_t c = str[index];
    if (c < 128) {
      // ASCII
      Serial.printf("ASCII char '%c' (0x%02X) at position %d\n", c, c, index);
      index++;
    } else {
      // UTF-8 sequence
      int seqLen = 0;
      if ((c & 0xE0) == 0xC0) seqLen = 2;      // 2-byte sequence
      else if ((c & 0xF0) == 0xE0) seqLen = 3; // 3-byte sequence
      else if ((c & 0xF8) == 0xF0) seqLen = 4; // 4-byte sequence
      else {
        Serial.printf("Invalid UTF-8 start byte 0x%02X at position %d\n", c, index);
        index++;
        continue;
      }
      
      Serial.printf("UTF-8 sequence at position %d (", index);
      for (int i = 0; i < seqLen && (index + i) < str.length(); i++) {
        Serial.printf("0x%02X ", (uint8_t)str[index + i]);
      }
      Serial.println(")");
      
      index += seqLen;
    }
  }
}

// Callback when characteristic is written to by the client (phone)
class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      std::string value = pCharacteristic->getValue();
      
      if (value.length() > 0) {
        // Sao chép dữ liệu vào một biến tạm thời
        String tempData = "";
        
        // Get raw data bytes
        uint8_t* bytes = pCharacteristic->getData();
        for (int i = 0; i < value.length(); i++) {
          tempData += (char)bytes[i];
        }
        
        // Log the received data
        Serial.print("Received: ");
        Serial.println(tempData);
        
        // Detailed UTF-8 debugging
        debugUTF8String(tempData);
        
        // Cập nhật biến toàn cục một cách an toàn
        // Đợi cho đến khi vòng lặp chính hoàn thành việc cập nhật màn hình trước đó
        while (isTFTBusy) {
          delay(10);
        }
        
        // Cập nhật dữ liệu và đặt cờ để vòng lặp chính xử lý việc hiển thị
        receivedData = tempData;
        hasNewMessage = true;
      }
    }
};

void setup() {
  // Initialize Serial communication
  Serial.begin(115200);
  Serial.println("Starting BLE Server Application...");
  
  // Initialize LCD
  tft.init();
  tft.setRotation(3); // Adjust based on your display orientation
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  
  // Use built-in font (will be used for our Vietnamese text rendering)
  tft.setTextFont(2);
  
  // Display welcome message and connection status
  tft.setCursor(10, 10);
  tft.println("ESP32 BT Display");
  tft.setCursor(10, 40);
  tft.println("Waiting for connection...");
  
  // Test Vietnamese display
  tft.fillRect(0, 260, tft.width(), 60, TFT_NAVY);
  tft.setTextColor(TFT_YELLOW);
  printVietnameseText(tft, 5, 270, "Hỗ trợ tiếng Việt");
  tft.setTextColor(TFT_GREEN);
  printNonAccentVietnamese(tft, 5, 300, "Hỗ trợ tiếng Việt");
  
  // Initialize BLE Device
  BLEDevice::init(DEVICE_NAME);
  
  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  
  // Create the BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);
  
  // Create TX characteristic (for notifications to the phone)
  pTxCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID_TX,
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
  
  pTxCharacteristic->addDescriptor(new BLE2902());
  
  // Create RX characteristic (for receiving data from the phone)
  pRxCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID_RX,
                      BLECharacteristic::PROPERTY_WRITE
                    );
  
  pRxCharacteristic->setCallbacks(new MyCallbacks());
  
  // Start the service
  pService->start();
  
  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // functions to help with iPhone connections issue
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  
  Serial.println("BLE server is ready. Waiting for connections...");
}

void updateTFTDisplay() {
  // Đánh dấu màn hình TFT đang bận
  isTFTBusy = true;
  
  // Kiểm tra trạng thái kết nối và cập nhật thanh trạng thái
  if (deviceConnected) {
    tft.fillRect(0, 0, tft.width(), 30, TFT_GREEN);
    tft.setTextColor(TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(5, 10);
    tft.println("Connected to phone");
  } else {
    tft.fillRect(0, 0, tft.width(), 30, TFT_RED);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.setCursor(5, 10);
    tft.println("Disconnected");
  }
  
  // Hiển thị tin nhắn mới nếu có
  if (hasNewMessage && receivedData.length() > 0) {
    // Hiển thị tin nhắn nhận được
    tft.fillRect(0, 40, tft.width(), tft.height()-40, TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.setCursor(5, 50);
    tft.println("Data received:");
    
    // Hiển thị bằng hai cách khác nhau
    
    // // Cách 1: Sử dụng hàm hiển thị tiếng Việt có gạch chân đỏ
    // printVietnameseText(tft, 5, 80, receivedData);
    
    // Cách 2: Sử dụng hàm hiển thị tiếng Việt không dấu
    tft.setTextColor(TFT_CYAN);
    printNonAccentVietnamese(tft, 5, 60, receivedData);
    
    // Hiển thị chú thích
    // tft.setTextColor(TFT_YELLOW);
    // tft.setTextSize(1);
    // tft.setCursor(5, 160);
    // tft.println("Above: Non-accent version");
    
    // Đặt lại cờ sau khi hiển thị
    hasNewMessage = false;
  }
  
  // Đánh dấu màn hình TFT không còn bận
  isTFTBusy = false;
}

void loop() {
  // Xử lý thay đổi trạng thái kết nối
  if (deviceConnected && !oldDeviceConnected) {
    // Mới kết nối
    oldDeviceConnected = deviceConnected;
    Serial.println("Connected to central device");
    updateTFTDisplay();
  }
  
  if (!deviceConnected && oldDeviceConnected) {
    // Mới ngắt kết nối
    oldDeviceConnected = deviceConnected;
    Serial.println("Disconnected from central device");
    delay(500); // Cho Bluetooth stack thời gian chuẩn bị
    
    // Khởi động lại quảng cáo để có thể nhìn thấy và kết nối lại
    pServer->startAdvertising();
    Serial.println("Restarting advertising");
    updateTFTDisplay();
  }
  
  // Nếu có tin nhắn mới, cập nhật màn hình
  if (hasNewMessage) {
    updateTFTDisplay();
  }
  
  // Nếu được kết nối, chúng ta có thể thực hiện các tác vụ định kỳ ở đây
  if (deviceConnected) {
    // Xử lý gửi phản hồi nếu có dữ liệu mới
    if (receivedData.length() > 0) {
      static unsigned long lastResponseTime = 0;
      unsigned long currentTime = millis();
      
      // Gửi phản hồi chỉ một lần cho mỗi tin nhắn nhận được (với độ trễ nhỏ)
      if (currentTime - lastResponseTime > 2000) { // 2 giây độ trễ
        lastResponseTime = currentTime;
        
        // Phản hồi tiếng Việt UTF-8 với các byte chính xác cho "Đã nhận tin nhắn của bạn"
        uint8_t vietnameseResponse[] = {
            0xC4, 0x90, 0xC3, 0xA3, 0x20, 0x6E, 0x68, 0xE1, 0xBA, 0xAD, 0x6E, 0x20, 
            0x74, 0x69, 0x6E, 0x20, 0x6E, 0x68, 0xE1, 0xBA, 0xAF, 0x6E, 0x20, 0x63, 
            0xE1, 0xBB, 0xA7, 0x61, 0x20, 0x62, 0xE1, 0xBA, 0xA1, 0x6E
        };
        
        // Gửi phản hồi dưới dạng thông báo với các byte chính xác
        pTxCharacteristic->setValue(vietnameseResponse, sizeof(vietnameseResponse));
        pTxCharacteristic->notify();
        
        // Ghi lại những gì đã được gửi
        Serial.println("Sent confirmation message in Vietnamese");
        
        // Giá trị debug hex
        Serial.print("Sent bytes: ");
        for(int i=0; i<sizeof(vietnameseResponse); i++) {
          Serial.printf("%02X ", vietnameseResponse[i]);
        }
        Serial.println();
        
        // An toàn cập nhật màn hình để hiển thị thông báo đã gửi
        // Chờ cho đến khi màn hình TFT không bận
        while (isTFTBusy) {
          delay(10);
        }
        
        isTFTBusy = true;
        tft.fillRect(0, 200, tft.width(), 60, TFT_NAVY);
        
        // Hiển thị cả hai phiên bản
        tft.setTextColor(TFT_YELLOW);
        printVietnameseText(tft, 5, 210, "Đã gửi: Đã nhận tin nhắn của bạn");
        
        tft.setTextColor(TFT_GREEN);
        printNonAccentVietnamese(tft, 5, 240, "Đã gửi: Đã nhận tin nhắn của bạn");
        isTFTBusy = false;
        
        // Xóa dữ liệu đã nhận sau khi phản hồi - thực hiện một cách an toàn
        receivedData = "";
      }
    }
    
    delay(50); // Độ trễ nhỏ để ngăn vòng lặp bận rộn
  } else {
    // Khi không kết nối, chúng ta có thể tiết kiệm điện bằng cách chờ lâu hơn giữa các vòng lặp
    delay(200);
  }
}
