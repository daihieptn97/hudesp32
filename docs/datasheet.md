# Hardware Datasheet — ESP32 HMI Board (1.9" ST7789)

> Tài liệu kỹ thuật cho board ESP32 HMI có sẵn màn TFT ST7789 1.9" 170x320.
> Dữ liệu trong file này được dump trực tiếp từ chip qua firmware Arduino/ESP-IDF, kết hợp với cấu hình `platformio.ini` đã verify chạy.
> Cập nhật lần cuối: 23/05/2026.

---

## 1. Tổng quan board

| Thuộc tính | Giá trị |
|---|---|
| Loại board | ESP32 HMI 1.9" với màn TFT tích hợp |
| MCU | ESP32-D0WDQ5 (Xtensa LX6 dual-core) |
| Bộ nhớ ngoài | 4 MB SPI Flash, không có PSRAM |
| Màn hình | TFT IPS 1.9" 170x320, driver ST7789, SPI |
| Kết nối | WiFi 2.4 GHz b/g/n, Bluetooth Classic, BLE |
| Cầu USB-Serial | CH340 (cổng hiện ra dưới dạng `/dev/cu.usbserial-*` trên macOS) |
| Tốc độ CPU | 240 MHz (max) |

**Lưu ý nhận diện:** chip này KHÔNG phải ESP32-S3. Mặc dù bộ pin TFT trùng với T-Display-S3, model chip thực tế đọc từ eFuse là `ESP32-D0WDQ5` (ESP32 cổ điển, Xtensa LX6). Các log có dòng "LilyGO T-Display-S3" là chuỗi hardcode trong code, không phải đọc từ phần cứng.

> **Về tên `ESP32-D0WDQ5`:** Tên này không còn xuất hiện trong datasheet ESP32 Series v5.2 hiện tại. Theo quy tắc đặt tên của Espressif (Figure 1-1 trong datasheet), `D0WDQ5` được giải mã là: **D**ual core + **0** in-package flash + **WD** (WiFi+BT/BLE dual mode) + **Q5** (QFN 5×5 package). Đây tương đương với biến thể **`ESP32-D0WD-V3`** trong datasheet hiện tại (revision v3, QFN 5×5, dual core, không có in-package flash). Khi tra cứu datasheet, sử dụng dòng `ESP32-D0WD-V3` trong Table 1-1 Comparison.

---

## 2. Thông số MCU

| Thông số | Giá trị |
|---|---|
| Model (eFuse readout) | ESP32-D0WDQ5 |
| Tương đương datasheet | ESP32-D0WD-V3 |
| Revision | 3 (v3.0/v3.1) |
| Package | QFN 5×5 (48 pins) |
| Architecture | Xtensa LX6 (32-bit) |
| Cores | 2 |
| CoreMark @ 240 MHz | 1079.96 (2 cores) / 4.50 CoreMark/MHz |
| CPU Frequency max | 240 MHz |
| XTAL Frequency | 40 MHz (yêu cầu cho WiFi/BT) |
| APB Frequency | 80 MHz |
| Internal ROM | 448 KB |
| Internal SRAM | 520 KB |
| RTC FAST Memory | 8 KB SRAM |
| RTC SLOW Memory | 8 KB SRAM (ULP coprocessor truy cập) |
| eFuse | 1024-bit (768-bit dành cho ứng dụng) |
| Tính năng RF | WiFi 802.11 b/g/n (2.4 GHz, up to 150 Mbps), Bluetooth 4.2 BR/EDR + BLE |
| Flash đóng gói | Không (external 4 MB) |
| PSRAM | Không có |
| VDD operating range | 2.3 V – 3.6 V (recommended 3.3 V) |
| VDD_SDIO Voltage | 1.8 V hoặc 3.3 V (configurable qua strapping MTDI) |
| Operating temperature | –40 °C ~ 125 °C |
| SDK | ESP-IDF v4.4.3 (Arduino core trên nền IDF) |
| Efuse MAC (raw) | `009CE53C1C78` |

### Crypto hardware acceleration

Chip có sẵn hardware accelerator cho: **AES** (FIPS PUB 197), **SHA-2** (FIPS PUB 180-4), **RSA** (lên đến 4096-bit), **RNG**. Hỗ trợ secure boot và flash encryption.

### Peripherals đầy đủ (chưa dùng hết trong project hiện tại)

- 34 GPIO programmable (6 input-only: GPIO34-39)
- 18 channel ADC 12-bit (SAR), 2 DAC 8-bit
- 10 capacitive touch sensors
- 4 SPI, 2 I2S, 2 I2C, 3 UART
- SD/eMMC/SDIO host, SDIO/SPI slave
- Ethernet MAC (cần PHY ngoài), TWAI (CAN 2.0)
- 16 channel LED PWM, Motor PWM
- 8 channel RMT (Remote Control), Pulse Counter
- ULP coprocessor (chạy trong Deep-sleep)



---

## 3. Bộ nhớ

### Flash
| Thông số | Giá trị |
|---|---|
| Dung lượng | 4 MB (4,194,304 bytes) |
| Tốc độ | 40 MHz |
| Mode | DIO |

### RAM (SRAM)
| Thông số | Giá trị |
|---|---|
| Heap total | ~361 KB |
| Heap free (lúc boot) | ~335 KB |
| DMA-capable | ~263 KB |
| PSRAM | Không có |

### Partition table
| Label | Type | Subtype | Address | Size |
|---|---|---|---|---|
| `nvs` | 0x01 | 0x02 | `0x00009000` | 20,480 (20 KB) |
| `otadata` | 0x01 | 0x00 | `0x0000E000` | 8,192 (8 KB) |
| `app0` | 0x00 | 0x10 | `0x00010000` | 3,145,728 (3 MB) |
| `spiffs` | 0x01 | 0x82 | `0x00310000` | 983,040 (960 KB) |

> Layout này là single-app (không hỗ trợ OTA). Nếu cần OTA, đổi sang partition table có cả `app0` + `app1`.

---

## 4. Network — Địa chỉ MAC

| Interface | MAC Address |
|---|---|
| WiFi STA | `78:1C:3C:E5:9C:00` |
| WiFi SoftAP | `78:1C:3C:E5:9C:01` |
| Bluetooth | `78:1C:3C:E5:9C:02` |
| Ethernet | `78:1C:3C:E5:9C:03` |

> Các MAC tăng tuần tự từ base MAC. Đây là hành vi tiêu chuẩn của ESP32.

---

## 5. Màn hình TFT

| Thông số | Giá trị |
|---|---|
| Driver IC | Sitronix ST7789 |
| Loại | TFT IPS |
| Kích thước | 1.9 inch |
| Độ phân giải | 170 × 320 pixel (portrait native) |
| Hiển thị mặc định | 320 × 170 (landscape, rotation = 1) |
| Color depth | 16-bit RGB565 |
| Giao tiếp | SPI (4-wire) |
| SPI Frequency | 40 MHz |
| RGB Order | BGR |
| Inversion | ON (panel IPS) |
| CGRAM offset | Bật (panel 170 lộ ra từ vùng RAM 240) |

### Pin mapping (board → ESP32 GPIO)

| Tín hiệu TFT | ESP32 GPIO |
|---|---|
| MOSI (SDA) | GPIO 13 |
| SCLK | GPIO 14 |
| CS | GPIO 15 |
| DC | GPIO 2 |
| RST | GPIO 4 |
| Backlight (BL) | GPIO 21 (active HIGH) |
| MISO | Không sử dụng |

---

## 6. Cấu hình PlatformIO (đã verify)

File `platformio.ini` đã test chạy ổn định:

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino

upload_port = /dev/cu.usbserial-1130
upload_speed = 115200
monitor_port = /dev/cu.usbserial-1130
monitor_speed = 115200
monitor_filters = esp32_exception_decoder, default

lib_deps =
    bodmer/TFT_eSPI@^2.5.43
    arduino-libraries/NTPClient@^3.2.1
    ESP32 BLE Arduino
    lennarthennigs/Button2@^2.4.1

board_build.partitions = partitions.csv
board_upload.flash_size = 4MB
board_upload.maximum_size = 3145728

build_flags =
    -D USER_SETUP_LOADED=1

    -D ST7789_DRIVER=1
    -D TFT_WIDTH=170
    -D TFT_HEIGHT=320
    -D CGRAM_OFFSET=1
    -D TFT_RGB_ORDER=TFT_BGR
    -D TFT_INVERSION_ON=1
    -D TFT_BACKLIGHT_ON=HIGH

    -D TFT_MOSI=13
    -D TFT_SCLK=14
    -D TFT_CS=15
    -D TFT_DC=2
    -D TFT_RST=4
    -D TFT_BL=21

    -D LOAD_GLCD=1
    -D LOAD_FONT2=1
    -D LOAD_FONT4=1
    -D LOAD_FONT6=1
    -D LOAD_FONT7=1
    -D LOAD_FONT8=1
    -D LOAD_GFXFF=1
    -D SMOOTH_FONT=1
    -D SPI_FREQUENCY=40000000
```

### Các flag bắt buộc cho màn 170x320

- `CGRAM_OFFSET=1` — Panel có RAM rộng 240px nhưng chỉ lộ 170px ở giữa. Thiếu flag này → vẽ ra vùng không hiển thị → màn đen.
- `TFT_INVERSION_ON=1` — Panel IPS cần đảo màu, không thì hiển thị âm bản.
- `TFT_RGB_ORDER=TFT_BGR` — Thứ tự kênh màu của panel này là BGR; nếu để RGB sẽ bị đổi đỏ ↔ xanh dương.

---

## 7. Toolchain & build

| Thành phần | Phiên bản |
|---|---|
| Platform | `espressif32` |
| Framework | Arduino |
| Arduino core | `framework-arduinoespressif32@3.20006.221224` |
| Toolchain | `toolchain-xtensa-esp32@~8.4.0` |
| ESP-IDF (qua Arduino core) | v4.4.3 |
| esptool | v4.11.0 |

---

## 8. Upload & flash

### Tham số upload chuẩn

| Thông số | Giá trị |
|---|---|
| Upload tool | esptool |
| Upload baud | 115200 (đã verify; KHÔNG dùng 460800/921600 với driver CH340 trên macOS) |
| Cổng macOS | `/dev/cu.usbserial-1130` (CH340) |

### Lưu ý môi trường macOS

1. Cần cài driver **WCH CH34xVCPDriver** (bản DriverKit `.dext`, không phải kext cũ). Có trên Mac App Store, miễn phí.
2. Sau khi cài driver, vào *System Settings → General → Login Items & Extensions → Driver Extensions* → bật `CH34xVCPDriver` → restart Mac.
3. Nếu PlatformIO auto-detect nhầm `/dev/cu.Bluetooth-Incoming-Port`, set thẳng `upload_port` và `monitor_port` trong `platformio.ini`.
4. Nếu lỗi `termios (22, 'Invalid argument')` → giảm `upload_speed` xuống `115200`.
5. Nếu chip không vào bootloader tự động: giữ **BOOT**, nhấn **RESET**, thả **RESET**, thả **BOOT**.

---

## 9. Sketch dump phần cứng (reference code)

Sketch này in toàn bộ thông tin phần cứng ra Serial và tóm tắt lên TFT. Dùng để verify board hoặc debug.

```cpp
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_mac.h"
#include "esp_partition.h"
#include "esp_heap_caps.h"

TFT_eSPI tft = TFT_eSPI();

void dumpChipInfo() {
    esp_chip_info_t info;
    esp_chip_info(&info);
    Serial.println("\n========== CHIP ==========");
    Serial.printf("Model       : %s\n", ESP.getChipModel());
    Serial.printf("Revision    : %d\n", ESP.getChipRevision());
    Serial.printf("Cores       : %d\n", info.cores);
    Serial.printf("CPU Freq    : %d MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("SDK Version : %s\n", ESP.getSdkVersion());
}

void dumpFlashInfo() {
    Serial.println("\n========== FLASH ==========");
    Serial.printf("Size  : %u bytes\n", ESP.getFlashChipSize());
    Serial.printf("Speed : %u Hz\n", ESP.getFlashChipSpeed());
    Serial.printf("Mode  : %d\n", ESP.getFlashChipMode());
}

void dumpMemoryInfo() {
    Serial.println("\n========== MEMORY ==========");
    Serial.printf("Heap Total : %u\n", ESP.getHeapSize());
    Serial.printf("Heap Free  : %u\n", ESP.getFreeHeap());
    Serial.printf("PSRAM      : %s\n", psramFound() ? "yes" : "no");
}

void dumpMacInfo() {
    Serial.println("\n========== MAC ==========");
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    Serial.printf("WiFi STA : %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    esp_read_mac(mac, ESP_MAC_BT);
    Serial.printf("BT       : %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
}

void dumpPartitions() {
    Serial.println("\n========== PARTITIONS ==========");
    auto it = esp_partition_find(ESP_PARTITION_TYPE_ANY,
                                 ESP_PARTITION_SUBTYPE_ANY, NULL);
    while (it) {
        const esp_partition_t* p = esp_partition_get(it);
        Serial.printf("%-16s 0x%02x 0x%02x @0x%08x size=%u\n",
                      p->label, p->type, p->subtype, p->address, p->size);
        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);
}

void setup() {
    Serial.begin(115200);
    delay(1500);

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);

    dumpChipInfo();
    dumpFlashInfo();
    dumpMemoryInfo();
    dumpMacInfo();
    dumpPartitions();

    // Hiển thị tóm tắt trên TFT
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("HARDWARE OK", 10, 10, 4);
    char buf[64];
    snprintf(buf, sizeof(buf), "%s r%d", ESP.getChipModel(), ESP.getChipRevision());
    tft.drawString(buf, 10, 50, 2);
    snprintf(buf, sizeof(buf), "Flash %dMB  Heap %dKB",
             ESP.getFlashChipSize()/(1024*1024), ESP.getFreeHeap()/1024);
    tft.drawString(buf, 10, 80, 2);
}

void loop() {
    delay(5000);
    Serial.printf("[heartbeat] uptime=%lus\n", millis()/1000);
}
```

---

## 10. Sơ đồ pin tham khảo

### TFT (đã chiếm dụng — không reuse)

```
GPIO  2  →  TFT_DC
GPIO  4  →  TFT_RST
GPIO 13  →  TFT_MOSI
GPIO 14  →  TFT_SCLK
GPIO 15  →  TFT_CS
GPIO 21  →  TFT_BL (backlight)
```

### Các pin GPIO ESP32 khác (chưa dùng, có thể dùng cho mở rộng)

> Một số GPIO trên ESP32 có hạn chế đặc biệt — bảng dưới là các pin **an toàn cho I/O thông thường**:

| GPIO | Vai trò mặc định | Ghi chú |
|---|---|---|
| 0 | BOOT button | Strapping pin, dùng làm input có pull-up |
| 5, 16, 17, 18, 19, 22, 23, 25, 26, 27 | GPIO tự do | Dùng được cho mọi chức năng |
| 32, 33 | GPIO + ADC + Touch | OK cho input analog |
| 34, 35, 36, 39 | **Input only** | Không có pull-up nội, không output được |
| 6–11 | **TRÁNH** | Nối tới flash SPI, dùng sẽ crash chip |

---

## 11. Troubleshooting tổng hợp (từ kinh nghiệm thực tế)

| Triệu chứng | Nguyên nhân | Khắc phục |
|---|---|---|
| `Failed to connect: No serial data received` | Auto-detect sang `/dev/cu.Bluetooth-Incoming-Port` | Set `upload_port` thẳng trong `platformio.ini` |
| `/dev/cu.*` không thấy cổng USB | Thiếu driver CH340, hoặc cáp USB chỉ có dây nguồn | Cài WCH CH34xVCPDriver; đổi cáp data |
| `termios.error: (22, 'Invalid argument')` | Driver CH340 cũ không xử lý được baud cao trên macOS | Đặt `upload_speed = 115200`; cài bản DriverKit mới |
| Màn đen sì sau khi upload | Thiếu `CGRAM_OFFSET=1` hoặc `TFT_INVERSION_ON=1` | Thêm 2 flag này vào `build_flags` |
| Màu hiển thị bị đảo (đỏ ↔ xanh dương) | `TFT_RGB_ORDER` sai | Đổi giữa `TFT_BGR` và `TFT_RGB` |
| Màn hiện âm bản (đen ↔ trắng) | `TFT_INVERSION_ON` ngược | Bật / tắt flag này |
| Serial monitor không hiện gì | Sai baud, hoặc thiếu `Serial.begin()` + `delay` | `monitor_speed = 115200`, thêm `delay(1500)` sau `Serial.begin()` |

---

## 12. Tham chiếu — Datasheet & tài liệu

### 12.1. Board (rất có thể là ideaspark ESP32 1.9" LCD board)

Dựa trên đặc điểm: ESP32-WROOM-32 + ST7789 170x320 1.9 inch TFT + CH340 + USB Type-C, board này khả năng cao là dòng **ideaspark ESP32 Development Board 1.9"** (hoặc clone tương đương). Tham khảo:

- **Trang sản phẩm gốc**: `https://www.amazon.com/ESP32-Development-1-9-inch-LCD/dp/B0D6QXC813`
- **Trang phân phối kỹ thuật**: `https://www.esp32s.com/product/esp32-wroom-32-development-board-1-9in-1-14in-st7789-170x320-tft-lcd-displaywifibl-module-ch340-type-c-interface-for-arduino/`

> Board này KHÔNG có schematic chính thức công khai (đây là sản phẩm clone từ Trung Quốc). Nếu cần sơ đồ chính xác, phải lật board lên đọc trực tiếp đường mạch, hoặc liên hệ người bán. Pin mapping đã được verify thực tế trong file này (mục 5).

### 12.2. Module RF — ESP32-WROOM-32

Đây là module SMD chứa chip ESP32 + flash 4MB + crystal 40MHz + antenna PCB. Là module thực tế hàn trên board của bạn.

- **Datasheet chính thức (Espressif)**: `https://www.espressif.com/sites/default/files/documentation/esp32-wroom-32_datasheet_en.pdf`
- **Mirror Mouser**: `https://www.mouser.com/datasheet/2/891/esp-wroom-32_datasheet_en-1223836.pdf`
- **Trang sản phẩm**: `https://documentation.espressif.com/esp32-wroom-32_datasheet_en.html`

Thông số chính của module: 38 pin, 4 MB SPI flash tích hợp, crystal oscillator 40 MHz, on-board PCB antenna, operating voltage 3.0-3.6 V, operating temperature −40 ~ 85 °C, kích thước module 18 × 25.5 × 3.1 mm.

> Lưu ý: ESP32-WROOM-32 tích hợp flash 4 MB SPI nối với GPIO6, GPIO7, GPIO8, GPIO9, GPIO10 và GPIO11. Sáu chân này không thể dùng làm GPIO thông thường. Đây cũng là lý do GPIO 6-11 bị cấm trong bảng pin tự do (mục 10 của tài liệu này).

### 12.3. Chip MCU bên trong module — ESP32 Series

Như đã verify ở mục 2, chip thực tế là **ESP32-D0WDQ5** (~tương đương `ESP32-D0WD-V3`).

- **ESP32 Series Datasheet v5.2** (Espressif, 2025-11): `https://www.espressif.com/documentation/esp32_datasheet_en.pdf`
  - Tra dòng `ESP32-D0WD-V3` trong Table 1-1
  - 78 trang, bao phủ đầy đủ pin layout, peripherals, electrical specs, RF specs
- **ESP32 Technical Reference Manual**: chi tiết register-level (~700 trang)
  - `https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf`
- **ESP32 Hardware Design Guidelines**: thiết kế schematic & layout
- **ESP32 Series SoC Errata**: lỗi đã biết của chip

### 12.4. Màn hình TFT — Sitronix ST7789V

Driver IC điều khiển màn hình. ST7789V là single-chip controller/driver cho TFT-LCD 262K màu, gồm 720 source line và 320 gate line driving circuits. Hỗ trợ phân giải tối đa 240RGB × 320 — phiên bản 170x320 trên board của bạn chỉ "lộ" 170 cột giữa, đó là lý do cần `CGRAM_OFFSET=1`.

- **ST7789V v1.6 datasheet (chính thức Sitronix)**: `https://newhavendisplay.com/content/datasheets/ST7789V.pdf` (316 trang)
- **Crystalfontz mirror** (3 phiên bản v0.1, v1.4, v1.6): `https://www.crystalfontz.com/controllers/Sitronix/ST7789V`
- **Orient Display mirror** (v1.3): `https://www.orientdisplay.com/controller-datasheets/sitronix/st7789v-lcd-controller-datasheet/`
- **Alldatasheet** (308 trang): `https://www.alldatasheet.com/datasheet-pdf/pdf/1132511/SITRONIX/ST7789V.html`

> Nếu module của bạn dùng biến thể ST7789V2 (phổ biến trên màn IPS 170x320 mới), pinout giống nhau, command set tương thích ngược. Có thể tra cùng datasheet này.

### 12.5. USB-to-Serial Bridge — WCH CH340

Chip cầu USB-Serial trên board. CH340C có built-in clock generator, không cần thạch anh ngoài. Hỗ trợ điện áp 5V và 3.3V; khi dùng 3.3V, nối V3 với VCC. CH340 chip hỗ trợ USB device suspend tự động để tiết kiệm điện.

- **Trang chủ WCH**: `https://www.wch-ic.com/products/CH340.html`
- **CH340 general datasheet** (6 trang, tổng quan): `https://www.alldatasheet.com/datasheet-pdf/pdf/1132602/WCH/CH340.html`
- **CH340C datasheet** (10 trang, chi tiết SOIC-8): `https://www.alldatasheet.com/datasheet-pdf/pdf/1817208/WCH/CH340C.html`
- **Driver macOS DriverKit** (cần cho board này): tải từ trang WCH hoặc Mac App Store ("CH34xVCPDriver")

Baud rates hỗ trợ: 50, 75, 100, 110, 134.5, 150, 300, 600, 900, 1200, 1800, 2400, 3600, 4800, 9600, 14400, 19200, 28800, 33600, 38400, 56000, 57600, 76800, 115200, 128000, 153600, 230400, 460800, 921600, 1500000, 2000000.

### 12.6. Flash chip (nằm trong module ESP32-WROOM-32)

Module đã đóng gói sẵn flash 4 MB SPI. Hãng và model cụ thể không xác định được từ firmware, thường là một trong các loại sau (datasheet tương thích lẫn nhau):

- **Winbond W25Q32**: `https://www.winbond.com/resource-files/w25q32jv%20revh%2002242023%20plus.pdf`
- **GigaDevice GD25Q32**: `https://www.gigadevice.com/datasheet/gd25q32c/`
- **XMC XM25QH32**: tra trên trang nhà sản xuất

Để biết chính xác hãng, cần bóc vỏ kim loại module ESP32-WROOM-32 đọc chữ trên IC flash 8 chân — không khuyến khích vì sẽ phá module.

### 12.7. Thư viện phần mềm

- **TFT_eSPI (Bodmer)**: `https://github.com/Bodmer/TFT_eSPI`
  - File `User_Setup.h` reference cho ST7789 170x320: search "Setup434_ST7789_170x320" trên GitHub
  - Repo tham khảo: `https://github.com/mboehmerm/Three-IPS-Displays-with-ST7789-170x320-240x280-240x320`
- **Adafruit_ST7789 / Adafruit_GFX**: thư viện thay thế nếu không muốn dùng TFT_eSPI
- **LVGL** (nếu làm UI phức tạp): `https://docs.lvgl.io/`
- **Arduino-ESP32 docs**: `https://docs.espressif.com/projects/arduino-esp32/`
- **ESP-IDF Programming Guide**: `https://docs.espressif.com/projects/esp-idf/`

### 12.8. Tổng kết — 4 file PDF cần tải

Để có **bộ tài liệu hoàn chỉnh** cho project này, tải 4 file:

| # | File | Trang | Mục đích |
|---|---|---|---|
| 1 | `esp32_datasheet_en.pdf` | 78 | Chip ESP32 (đã có) |
| 2 | `esp32-wroom-32_datasheet_en.pdf` | ~30 | Module RF + pinout 38-pin |
| 3 | `ST7789V_v1.6.pdf` | 316 | Driver màn hình, command set, gamma, timing |
| 4 | `CH340DS1.PDF` (CH340 datasheet) | 10 | Cầu USB-Serial |

4 file này tổng cộng dưới 15 MB, đủ thông tin để: thiết kế PCB mở rộng, viết driver mức thấp, đo dòng tiêu thụ, gỡ lỗi RF, tinh chỉnh hiển thị nâng cao.