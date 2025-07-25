https://www.buydisplay.com/download/ic/ST7789.pdf?srsltid=AfmBOopOp3yn4AT6CUAn_qLZOxd7k_pOgOJ81IzojiYKoJF0CWmmQKCW

Phân Tích và Xác Định
Chip (Vi điều khiển):

platform = espressif32: Nền tảng là chip của Espressif.

board = esp32dev: Cấu hình sử dụng một bo mạch phát triển ESP32 chung.

Tuy nhiên, các chân GPIO được định nghĩa trong build_flags lại là manh mối quan trọng nhất. Sự kết hợp giữa màn hình ST7789, độ phân giải 170x320, và đặc biệt là bộ chân GPIO (MOSI=13, SCLK=14, CS=15, DC=2, RST=4, BL=21) hoàn toàn trùng khớp với bo mạch LilyGO T-Display-S3.

Vì vậy, chip cụ thể trên bo mạch của bạn là ESP32-S3.

Màn hình:

ST7789_DRIVER=1: Driver màn hình là ST7789.

TFT_WIDTH=170, TFT_HEIGHT=320: Màn hình TFT có độ phân giải 170x320 pixels.

Kết luận: Cấu hình này dành cho bo mạch phát triển LilyGO T-Display-S3.


ESP32 HMI Arduino LVGL WIFI & Bluetooth 1.9 "170 * 320 500K RAM, 4M FLASH Arduino LVGL WIFI & Bluetooth 1.9" 170 * 320