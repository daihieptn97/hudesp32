# Car HUD ESP32 Firmware

Firmware nhận dữ liệu BLE từ điện thoại Android và hiển thị thông tin chỉ đường, tốc độ, cuộc gọi, tin nhắn lên màn TFT ST7789 170×320.

## Hardware
- ESP32-WROOM-32 board (ideaspark hoặc tương đương)
- ST7789 1.9" TFT 170×320
- CH340 USB-Serial

## Cấu trúc thư mục
```
firmware/
├── platformio.ini        # cấu hình PlatformIO + TFT_eSPI + BLE
├── huge_app.csv          # partition table 3MB cho app (BLE chiếm khá nhiều flash)
└── src/
    └── main.cpp          # toàn bộ firmware
```

## Build & Upload

```bash
cd firmware
pio run -t upload
pio device monitor
```

Lưu ý: trên macOS với CH340, đảm bảo `upload_port` trong `platformio.ini` đúng cổng (mặc định `/dev/cu.usbserial-1130`).

## Test BLE (không cần app Android)

1. Cài **nRF Connect** trên điện thoại Android
2. Scan → connect tới `CarHUD-ESP32`
3. Tìm service `6e400001-b5a3-f393-e0a9-e50e24dcca9e`
4. Vào characteristic `6e400002-...` (WRITE)
5. Tap upload icon → Send (chọn Text format) → paste 1 trong các JSON sau:

```json
{"t":"nav","arr":"right","d":200,"u":"m","s":"Le Loi"}
{"t":"nav","arr":"left","d":1,"u":"km","s":"Nguyen Hue"}
{"t":"spd","v":45}
{"t":"clk","h":14,"m":30}
{"t":"bat","p":75}
{"t":"call","n":"Mom","p":"+84901234567"}
{"t":"sms","f":"Anh","m":"Em o dau roi"}
{"t":"clr"}
```

Quan sát màn TFT thay đổi tương ứng.

## Đặc điểm kỹ thuật

- **BLE Service UUID**: `6e400001-b5a3-f393-e0a9-e50e24dcca9e` (Nordic UART)
- **MTU**: negotiate lên 247 byte để JSON dài vừa 1 packet
- **Font**: TFT_eSPI built-in (ASCII only) - phía Android phải strip dấu tiếng Việt trước khi gửi
- **Pages**: IDLE / NAV / CALL / SMS với auto-timeout (NAV 60s, CALL 30s, SMS 15s)
- **Bottom bar**: luôn hiện speed + clock + battery
- **Memory**: ~50% Flash, ~30% RAM với cấu hình hiện tại

## Troubleshooting

| Triệu chứng | Khắc phục |
|---|---|
| Build fail "section .bss overflow" | Tăng `huge_app` partition hoặc disable một số debug log |
| BLE không advertise | Check Serial log, có thể chip bị hư phần BT |
| Màn đen | Verify `CGRAM_OFFSET=1` và `TFT_INVERSION_ON=1` trong build_flags |
| Màu đảo (đỏ ↔ xanh dương) | Đổi `TFT_RGB_ORDER=TFT_BGR` thành `TFT_RGB` |
| Disconnect liên tục | Tăng `upload_speed` xuống thấp hơn, hoặc thay cáp USB |

Xem `ANDROID_TECH_SPEC.md` để triển khai app Android companion.