# Car HUD ESP32 — README

HUD (Heads-Up Display) chạy trên ESP32 + màn ST7789 (320×170), nhận dữ liệu navigation / cuộc gọi / SMS / tốc độ từ điện thoại qua Bluetooth Low Energy (BLE) và hiển thị ngay trên xe.

---

## 1. Tổng quan phần cứng

| Thành phần      | Chi tiết                                                |
|-----------------|---------------------------------------------------------|
| MCU             | ESP32 (bất kỳ biến thể nào hỗ trợ BLE — ESP32, S3, C3)  |
| Màn hình        | ST7789 320×170 IPS (loại LilyGO T-Display S3 hoặc tương đương) |
| Giao thức kết nối | BLE GATT (NimBLE stack)                               |
| Nguồn           | USB-C 5 V hoặc lấy từ cổng OBD-II của xe                |

Pin TFT được khai báo qua `User_Setup.h` của thư viện `TFT_eSPI` — file đó cần khớp với board bạn dùng. Không sửa trong `.ino`.

---

## 2. Thư viện cần cài

| Thư viện        | Version đã test | Cài qua                                 |
|-----------------|-----------------|------------------------------------------|
| Arduino-ESP32   | 2.0.x           | Boards Manager                           |
| TFT_eSPI        | 2.5.x           | Library Manager (cần config `User_Setup.h`) |
| NimBLE-Arduino  | 1.4.x           | Library Manager                          |
| ArduinoJson     | 6.x             | Library Manager                          |

> Nếu nâng NimBLE lên 2.x, callback signature đổi (`onConnect(NimBLEConnInfo&)` thay vì `ble_gap_conn_desc*`) — file `.ino` này dùng API 1.x.

---

## 3. Luồng chính (main flow)

```
        ┌──────────────────────┐
        │   Điện thoại (App)   │
        │  - Đọc navigation    │
        │  - Nghe cuộc gọi/SMS │
        │  - GPS speed         │
        └──────────┬───────────┘
                   │ BLE GATT WRITE
                   │ (JSON, mỗi gói < 200 byte)
                   ▼
        ┌──────────────────────┐
        │       ESP32          │
        │                      │
        │  RxCallbacks         │
        │     ↓                │
        │  handleJsonMessage() │  ← parse "t" field, route theo loại
        │     ↓                │
        │  Cập nhật StateData  │  ← nav / spd / call / sms / clk / bat
        │     ↓                │
        │  needsRedraw = true  │
        └──────────┬───────────┘
                   │ loop() @ 20 Hz
                   ▼
        ┌──────────────────────┐
        │  checkPageExpiration │  ← NAV 60s, CALL 30s, SMS 15s
        │       ↓              │
        │      render()        │  ← vẽ top bar + page + bottom bar
        │       ↓              │
        │   ST7789 320×170     │
        └──────────────────────┘
```

### 3.1. Vòng đời một lệnh

1. App điện thoại nối tới ESP32 (advertising name `CarHUD-ESP32`).
2. App ghi (`WRITE` / `WRITE_NO_RESPONSE`) một chuỗi JSON vào `RX characteristic`.
3. `RxCallbacks::onWrite()` được gọi → đọc giá trị → gọi `handleJsonMessage()`.
4. `handleJsonMessage()` parse JSON, đọc field `"t"` để biết loại lệnh, cập nhật `StateData st`, đặt `currentPage` tương ứng và bật cờ `needsRedraw`.
5. `loop()` chạy mỗi 50 ms: kiểm tra timeout (xem bảng dưới), nếu state thay đổi thì vẽ lại màn hình.
6. Khi page hết hạn (60 s không có nav mới, v.v.), HUD tự quay về `PAGE_IDLE` hoặc về page khác đang còn hiệu lực.

### 3.2. State machine

| Page        | Khi nào vào              | Khi nào thoát                                  |
|-------------|--------------------------|------------------------------------------------|
| `PAGE_IDLE` | Khởi động, hoặc `clr`    | Khi nhận `nav` / `call` / `sms`                |
| `PAGE_NAV`  | Nhận `nav`               | Sau 60 s không nhận `nav` mới                  |
| `PAGE_CALL` | Nhận `call`              | Sau 30 s, hoặc bị `nav` mới ghi đè (call ưu tiên thấp hơn nav khi cùng tồn tại — nav vẫn quay lại sau khi call hết hạn) |
| `PAGE_SMS`  | Nhận `sms`               | Sau 15 s                                       |

Ở `normal mode`, bottom bar (speed / clock / battery) hiển thị **trên mọi page**, độc lập với state machine, và tự đánh dấu "stale" (xám đi) sau:
- Speed: 10 s
- Clock: 120 s
- Battery: 120 s

Ở `HUD mode`, firmware bỏ top bar / bottom bar để giảm nhiễu khi phản chiếu lên kính. NAV chỉ còn tốc độ + mũi tên + khoảng cách; CALL/SMS vẫn hiện bằng layout tối giản.

---

## 4. BLE GATT specification

### 4.1. Service & Characteristics

| Mục                        | UUID                                       | Properties               |
|----------------------------|---------------------------------------------|--------------------------|
| **Service**                | `6e400001-b5a3-f393-e0a9-e50e24dcca9e`      | (Nordic UART Service)    |
| **RX** (phone → ESP32)     | `6e400002-b5a3-f393-e0a9-e50e24dcca9e`      | `WRITE`, `WRITE_NR`      |
| **TX** (ESP32 → phone)     | `6e400003-b5a3-f393-e0a9-e50e24dcca9e`      | `NOTIFY`, `READ`         |

- Device name khi quảng bá: `CarHUD-ESP32`
- MTU được negotiate lên **247 byte** → payload thực tế tối đa ~244 byte mỗi gói WRITE.
- Format payload: **JSON UTF-8**, một object, không xuống dòng. Mọi gói đều phải có field `"t"` (type).

### 4.2. Bảng lệnh — Phone → ESP32 (ghi vào RX)

Tất cả gói đều là JSON. Field `"t"` quyết định loại; các field khác phụ thuộc loại.

| `t`     | Ý nghĩa            | Fields                                                                                   | Page hiển thị | Timeout |
|---------|--------------------|------------------------------------------------------------------------------------------|---------------|---------|
| `nav`   | Chỉ dẫn turn-by-turn | `arr` *(string)* — loại mũi tên (xem 4.3)<br>`d` *(int)* — khoảng cách<br>`u` *(string)* — đơn vị (`"m"` hoặc `"km"`)<br>`s` *(string)* — tên đường / chỉ dẫn | `PAGE_NAV`    | 60 s    |
| `spd`   | Tốc độ hiện tại    | `v` *(int)* — km/h                                                                       | bottom bar    | 10 s    |
| `call`  | Cuộc gọi đến       | `n` *(string)* — tên<br>`p` *(string)* — số điện thoại                                   | `PAGE_CALL`   | 30 s    |
| `sms`   | Tin nhắn / noti    | `f` *(string)* — người gửi<br>`m` *(string)* — nội dung                                  | `PAGE_SMS`    | 15 s    |
| `clk`   | Đồng hồ            | `h` *(int 0–23)*<br>`m` *(int 0–59)*                                                     | bottom bar    | 120 s   |
| `bat`   | Pin điện thoại     | `p` *(int 0–100)*                                                                        | bottom bar    | 120 s   |
| `cfg` / `hud` | Cấu hình màn hình | `mode`, `hud`, `flip`, `br`, `brightness`, `save` — xem 4.4                              | không đổi page | —       |
| `clr`   | Xóa, về IDLE       | (không có field nào khác)                                                                | `PAGE_IDLE`   | —       |

### 4.3. Bảng giá trị `arr` (arrow type)

ESP32 chuẩn hóa chuỗi: bỏ ký tự `-`, `_`, khoảng trắng, đổi sang chữ thường. Vì vậy `"turn-right"`, `"TURN_RIGHT"`, `"turn right"` đều khớp `"turnright"`.

| Giá trị được chấp nhận                                          | Hiển thị                |
|-----------------------------------------------------------------|--------------------------|
| `straight`, `up`, `continue`, `forward`, `depart`               | Mũi tên thẳng            |
| `right`, `turn-right`, `r`                                      | Mũi tên rẽ phải          |
| `left`, `turn-left`, `l`                                        | Mũi tên rẽ trái          |
| `slight-right`, `bear-right`, `keep-right`                      | Chếch phải               |
| `slight-left`, `bear-left`, `keep-left`                         | Chếch trái               |
| `sharp-right`                                                   | Rẽ gắt phải              |
| `sharp-left`                                                    | Rẽ gắt trái              |
| `uturn`, `uturn-left`, `make-uturn`                             | Quay đầu (sang trái)     |
| `uturn-right`                                                   | Quay đầu (sang phải)     |
| `arrive`, `arrived`, `destination`, `end`                       | Cờ đích                  |
| *(bất kỳ giá trị nào khác)*                                     | Badge đỏ `?` + log Serial — đây là dấu hiệu app gửi sai tên |

### 4.4. Cấu hình HUD / màn hình

Gửi vào RX characteristic:

```json
{"t":"cfg","mode":"hud","flip":"v","br":255}
{"t":"cfg","mode":"normal","br":180}
{"t":"hud","hud":true,"flip":"v","brightness":220,"save":true}
```

Fields:

| Field | Type | Mô tả |
|---|---|---|
| `mode` | string | `"hud"` để hắt kính, `"normal"` / `"direct"` / `"screen"` để xem trực tiếp |
| `hud` | bool | Alias bật/tắt nhanh: `true` = HUD mode, `false` = normal mode |
| `flip` | string | Chỉ áp dụng khi HUD mode bật. Giá trị: `"v"`/`"vertical"` lật trên-dưới; `"h"`/`"horizontal"` lật trái-phải; `"r180"` xoay 180; `"none"` không lật |
| `br` / `brightness` | int | Độ sáng backlight `0..255`, `255` là sáng nhất |
| `save` | bool | Mặc định `true`. Khi `true`, ESP32 lưu vào NVS và tự dùng lại sau reboot |

Khuyến nghị cho cách đặt màn hình hiện tại: bắt đầu bằng:

```json
{"t":"cfg","mode":"hud","flip":"v","br":255}
```

Nếu nhìn qua kính vẫn sai chiều, Android nên cho user thử nhanh các giá trị `flip` theo thứ tự: `"v"`, `"h"`, `"r180"`, `"none"`. Việc này không cần flash firmware lại.

Khi nhận lệnh cấu hình, ESP32 cập nhật TX characteristic và notify nếu Android đã bật notification:

```json
{"t":"cfg","mode":"hud","flip":"v","br":255}
```

### 4.5. Ví dụ payload

```json
{"t":"nav","arr":"right","d":350,"u":"m","s":"Nguyen Trai"}
{"t":"nav","arr":"uturn","d":1,"u":"km","s":"Quay dau tai vong xuyen"}
{"t":"nav","arr":"arrive","d":0,"u":"m","s":"Den noi luc 09:35"}
{"t":"spd","v":48}
{"t":"cfg","mode":"hud","flip":"v","br":255}
{"t":"clk","h":9,"m":27}
{"t":"bat","p":83}
{"t":"call","n":"Mom","p":"+84 912 345 678"}
{"t":"sms","f":"Boss","m":"Hop luc 10h, dung tre nhe"}
{"t":"clr"}
```

### 4.6. TX characteristic (ESP32 → phone)

Firmware hiện notify ACK cho lệnh `cfg` / `hud`, ví dụ:

```json
{"t":"cfg","mode":"hud","flip":"v","br":255}
```

Phone vẫn có thể `READ` characteristic này nếu muốn lấy giá trị cấu hình cuối cùng. Các lệnh dữ liệu như `nav`, `spd`, `call`, `sms` hiện chưa ACK để giảm traffic BLE.

Sẵn sàng cho mở rộng:
- ACK mỗi command đã xử lý
- Gửi trạng thái nút bấm (nếu thêm nút điều khiển trên HUD)
- Báo lỗi (`{"err":"json","raw":"..."}`)

---

## 5. Bố cục màn hình

### 5.1. Normal mode

Normal mode giữ giao diện xem trực tiếp trên màn hình. NAV chia 40/60:

```
 ┌─────────────────────────────────────────────────────┐
 │ BLE OK                                  NAVIGATION  │  top bar  (h=18)
 ├─────────────────────────────────────────────────────┤
 │                                                     │
 │           320 m                                     │
 │   ▶       ────────────────                          │  content  (h=130)
 │           Den noi luc 09:35                         │
 │           Nguyen Trai                               │
 │                                                     │
 ├─────────────────────────────────────────────────────┤
 │                  09:27              Bat 83%        │  bottom   (h=22)
 └─────────────────────────────────────────────────────┘
        ←─ speed 40% ─→ ←──── nav 60% ────────→
```

Quy tắc layout trong `drawPageNav()`:

- Số khoảng cách dùng **font 6** (digits-only font, lớn) → chỉ vẽ chữ số.
- Đơn vị `m` / `km` dùng **font 4** màu cam, vẽ ngay sau số → fix lỗi v1 (font 6 không render được chữ cái nên đơn vị bị mất).
- Tên đường dùng **font 2**, tự xuống tối đa 2 dòng. Nếu vẫn không vừa, áp dụng bảng viết tắt:

| Bản gốc            | Viết tắt |
|--------------------|----------|
| Den noi luc        | ETA      |
| Re phai vao        | R:       |
| Re trai vao        | L:       |
| Di thang           | Fwd      |
| Quay dau           | U-turn   |
| Duong / Pho / Quan | D. / P. / Q. |
| Continue on        | Cont.    |
| Turn right onto    | R:       |
| Turn left onto     | L:       |
| Street / Avenue / Boulevard / Road | St / Ave / Blvd / Rd |

Nếu sau khi viết tắt vẫn dài, dòng thứ 2 sẽ bị cắt và thay 2 ký tự cuối bằng `..`.

### 5.2. HUD mode

HUD mode dùng nền đen, màu chính xanh lá / cam / trắng, không vẽ top bar hoặc bottom bar:

```
 ┌─────────────────────────────────────────────────────┐
 │                                                     │
 │   48      │        ▶             350                │
 │  km/h     │                       m                 │
 │                                                     │
 └─────────────────────────────────────────────────────┘
    speed       arrow              distance
```

CALL/SMS vẫn hiện, nhưng ở layout tối giản và có tốc độ nhỏ ở góc phải dưới.

---

## 6. Debugging

Serial monitor ở **115200 baud** in mọi sự kiện:

| Log prefix     | Khi nào                                   |
|----------------|-------------------------------------------|
| `[BLE]`        | Connect / disconnect / MTU change          |
| `[RX]`         | Mọi gói JSON nhận được (raw text)          |
| `[JSON]`       | Lỗi parse hoặc type không nhận dạng được   |
| `[NAV]`        | `arr` không khớp pattern nào → fallback `?` |
| `[CFG]`        | Load/save cấu hình HUD, flip, brightness   |

Trường hợp ảnh chụp ban đầu (số `900` không có đơn vị + hình tròn `?` thay vì mũi tên) sẽ xuất hiện log:
```
[RX] {"t":"nav","arr":"turn_right","d":900,"u":"m","s":"Den noi luc 9:..."}
[NAV] Unknown arrow string: 'turn_right'   ← (chỉ ở firmware cũ)
```
Ở v2 chuỗi `"turn_right"` được canonicalize thành `"turnright"` → khớp `ARR_RIGHT`.

---

## 7. Cấu trúc thư mục đề xuất

```
CarHUD_ESP32/
├── CarHUD_ESP32.ino     ← firmware
├── README.md            ← file này
└── User_Setup.h         ← config TFT_eSPI (chép vào thư mục thư viện)
```

---

## 8. Roadmap

- [x] Lệnh cấu hình HUD mode / normal mode qua BLE.
- [x] Lưu cấu hình màn hình vào NVS.
- [x] Notify lại phone qua TX characteristic cho lệnh cấu hình.
- [ ] ACK / báo lỗi cho mọi command JSON.
- [ ] Thêm 2 nút bấm vật lý: dismiss noti / chuyển page thủ công.
- [ ] Hỗ trợ font Unicode để hiển thị dấu tiếng Việt thay vì ASCII.
- [ ] Encrypt + pairing để chống phone lạ kết nối.
