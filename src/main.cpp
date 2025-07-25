// Thêm dòng này khi dùng PlatformIO
#include <Arduino.h> 

#include <lvgl.h>
#include <TFT_eSPI.h>

/* --- Cấu hình LVGL --- */
// Kích thước màn hình
static const uint16_t screenWidth  = 170;
static const uint16_t screenHeight = 320;

// Bộ đệm cho LVGL
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[ screenWidth * 20 ]; // Tăng nhẹ bộ đệm để xử lý các đối tượng phức tạp hơn

// Khởi tạo đối tượng màn hình từ thư viện TFT_eSPI
TFT_eSPI tft = TFT_eSPI();

// Khai báo các đối tượng giao diện ở phạm vi toàn cục (global) để có thể truy cập từ các hàm khác
static lv_obj_t *main_label;
static int click_count = 0;

/* --- Hàm kết nối LVGL và TFT_eSPI --- */
void my_disp_flush( lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p )
{
    uint32_t w = ( area->x2 - area->x1 + 1 );
    uint32_t h = ( area->y2 - area->y1 + 1 );

    tft.startWrite();
    tft.setAddrWindow( area->x1, area->y1, w, h );
    tft.pushColors( ( uint16_t * )&color_p->full, w * h, true );
    tft.endWrite();

    lv_disp_flush_ready( disp );
}

/* --- Hàm xử lý sự kiện cho nút bấm --- */
static void button_event_handler(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if(code == LV_EVENT_CLICKED) {
        click_count++;
        // Cập nhật nội dung của main_label khi nút được nhấn
        lv_label_set_text_fmt(main_label, "Button clicked: %d times!", click_count);
        // Căn chỉnh lại label sau khi text thay đổi
        lv_obj_align(main_label, LV_ALIGN_CENTER, 0, -40);
        Serial.printf("Button clicked! Count: %d\n", click_count);
    }
}


void setup() {
    Serial.begin(115200);
    Serial.println("LVGL ESP32 with Interactive Button Example");

    // 1. Khởi tạo LVGL
    lv_init();

    // 2. Khởi tạo màn hình TFT
    tft.begin();
    tft.setRotation(0); 

    // 3. Khởi tạo bộ đệm cho LVGL
    lv_disp_draw_buf_init( &draw_buf, buf, NULL, screenWidth * 20 );

    // 4. Khởi tạo driver hiển thị cho LVGL
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init( &disp_drv );
    disp_drv.hor_res = screenWidth;
    disp_drv.ver_res = screenHeight;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register( &disp_drv );

    // --- 5. Đặt màu nền cho màn hình ---
    // Đặt màu nền dạng gradient (chuyển màu) từ trên xuống dưới cho màn hình chính
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x2c3e50), LV_PART_MAIN); // Màu bắt đầu (xanh đậm)
    // lv_obj_set_style_bg_grad_color(lv_scr_act(), lv_color_hex(0x3498db), LV_PART_MAIN); // Màu kết thúc (xanh nhạt)
    // lv_obj_set_style_bg_grad_dir(lv_scr_act(), LV_GRAD_DIR_VER, LV_PART_MAIN); // Hướng chuyển màu: dọc

    // --- 6. Bắt đầu tạo giao diện ---
    
    // Tạo nhãn chính (main_label)
    main_label = lv_label_create( lv_scr_act() );
    lv_label_set_text( main_label, "Hello from\nVS Code!\n\nXin chao PlatformIO!" );
    lv_label_set_long_mode(main_label, LV_LABEL_LONG_WRAP);     /*Wrap the long lines*/
    lv_obj_set_width(main_label, screenWidth - 20);                  /*Set smaller width to make the lines wrap*/
    lv_obj_set_style_text_align(main_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(main_label, lv_color_white(), 0); // Đặt màu chữ thành trắng để nổi bật trên nền
    lv_obj_align( main_label, LV_ALIGN_CENTER, 0, -40 ); // Di chuyển lên trên một chút để nhường chỗ cho nút

    // Tạo một nút bấm (button)
    lv_obj_t * button = lv_btn_create(lv_scr_act());
    // Gắn hàm xử lý sự kiện cho nút bấm
    lv_obj_add_event_cb(button, button_event_handler, LV_EVENT_ALL, NULL);
    // Căn chỉnh nút bấm nằm dưới nhãn chính
    lv_obj_align(button, LV_ALIGN_CENTER, 0, 40);

    // Tạo một nhãn (label) cho nút bấm
    lv_obj_t * btn_label = lv_label_create(button);
    lv_label_set_text(btn_label, "Click Me!");
    lv_obj_center(btn_label); // Căn giữa nhãn bên trong nút


    Serial.println( "Setup completed" );
}

void loop() {
    lv_timer_handler(); 
    delay(5);
}
