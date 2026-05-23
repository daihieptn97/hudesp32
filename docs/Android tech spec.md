# Android App Technical Specification — Car HUD Companion

> Tài liệu kỹ thuật để triển khai app Android companion cho thiết bị **Car HUD ESP32**.
> App đọc notification từ Google Maps, lấy thông tin cuộc gọi/tin nhắn, tốc độ, đồng hồ, pin từ điện thoại, đóng gói thành JSON rồi gửi qua BLE GATT sang thiết bị ESP32 để hiển thị trên màn TFT 320×170.
>
> **Đối tượng đọc**: Android developer (Kotlin/Java) sẽ triển khai app.
> **Ngôn ngữ khuyến nghị**: Kotlin + Jetpack.
> **Min SDK**: API 24 (Android 7.0) — đủ phổ biến và đầy đủ API BLE peripheral interaction.
> **Target SDK**: API 34 (Android 14).

---

## 1. Tổng quan kiến trúc

```
┌──────────────────────────────────────────────┐
│              Android Phone                   │
│                                              │
│  ┌────────────────────────────────────────┐  │
│  │   Google Maps / Phone / SMS apps       │  │
│  │         (sources of data)              │  │
│  └────────────────┬───────────────────────┘  │
│                   │ notifications, intents   │
│                   ▼                          │
│  ┌────────────────────────────────────────┐  │
│  │  CarHudApp                             │  │
│  │  ┌──────────────────────────────────┐  │  │
│  │  │ NotificationListenerService      │  │  │
│  │  │  - reads Google Maps nav text    │  │  │
│  │  │  - reads SMS, call notifications │  │  │
│  │  └──────────────────────────────────┘  │  │
│  │  ┌──────────────────────────────────┐  │  │
│  │  │ Parsers                          │  │  │
│  │  │  - GoogleMapsNavParser           │  │  │
│  │  │  - CallParser, SmsParser         │  │  │
│  │  │  - SpeedProvider (FusedLocation) │  │  │
│  │  │  - ClockProvider, BatteryProvider│  │  │
│  │  └──────────────────────────────────┘  │  │
│  │  ┌──────────────────────────────────┐  │  │
│  │  │ JsonEncoder                      │  │  │
│  │  └────────────┬─────────────────────┘  │  │
│  │  ┌────────────▼─────────────────────┐  │  │
│  │  │ BleClient (GATT)                 │  │  │
│  │  └────────────┬─────────────────────┘  │  │
│  └───────────────┼────────────────────────┘  │
│                  │                           │
└──────────────────┼───────────────────────────┘
                   │ BLE (2.4 GHz)
                   ▼
        ┌──────────────────────┐
        │  ESP32 Car HUD       │
        │  - GATT Server       │
        │  - JSON Parser       │
        │  - ST7789 Display    │
        └──────────────────────┘
```

App chạy nền liên tục (foreground service) để duy trì BLE và NotificationListener.

---

## 2. BLE Protocol — Specification chi tiết

### 2.1. Device & Service UUIDs

| Thành phần | UUID |
|---|---|
| Device name (advertising) | `CarHUD-ESP32` |
| Service UUID | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` |
| RX Characteristic (Android → ESP32) | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` (WRITE) |
| TX Characteristic (ESP32 → Android) | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` (NOTIFY) |

> Đây là biến thể của **Nordic UART Service (NUS)** — quy ước phổ biến, dễ test bằng nRF Connect.

### 2.2. Connection flow

1. App scan BLE thiết bị có service UUID `6e400001-...` (hoặc theo device name `CarHUD-ESP32`)
2. Connect tới thiết bị
3. **Quan trọng**: request MTU = 247 byte ngay sau khi connect (mặc định BLE chỉ 23 byte → JSON dài sẽ bị cắt)
4. Discover services
5. Lưu reference tới RX characteristic, enable notification trên TX characteristic
6. Bắt đầu gửi dữ liệu (write to RX char)

```kotlin
private fun onConnected(gatt: BluetoothGatt) {
    gatt.requestMtu(247)
}

override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
    Log.d(TAG, "MTU negotiated: $mtu")
    gatt.discoverServices()
}
```

### 2.3. Auto-reconnect

ESP32 sẽ tự re-advertise khi mất kết nối. App phải implement auto-reconnect:
- Khi `onConnectionStateChange` báo `STATE_DISCONNECTED` → wait 2s rồi connect lại
- Lưu MAC address của thiết bị đã pair vào SharedPreferences để bypass scan sau lần đầu tiên

---

## 3. JSON Schema — chi tiết từng loại message

Tất cả message gửi qua RX characteristic là **JSON object 1 dòng** (không có newline cuối, không pretty-print). Field `t` là discriminator.

### 3.1. Navigation
```json
{"t":"nav","arr":"right","d":200,"u":"m","s":"Le Loi"}
```

| Field | Type | Required | Mô tả |
|---|---|---|---|
| `t` | string | yes | Luôn là `"nav"` |
| `arr` | string | yes | Loại mũi tên. Giá trị hợp lệ: `"left"`, `"right"`, `"straight"`, `"uturn"`, `"slight-left"`, `"slight-right"`, `"sharp-left"`, `"sharp-right"` |
| `d` | int | yes | Khoảng cách đến maneuver kế tiếp |
| `u` | string | yes | Đơn vị: `"m"` hoặc `"km"` |
| `s` | string | yes | Tên đường tiếp theo, **đã loại bỏ dấu tiếng Việt** (ASCII only), tối đa 47 ký tự |

**Lưu ý quan trọng về tiếng Việt**: ESP32 chỉ render ASCII. App PHẢI strip dấu trước khi gửi. Ví dụ:
- `"Lê Lợi"` → `"Le Loi"`
- `"Nguyễn Văn Cừ"` → `"Nguyen Van Cu"`

Dùng `java.text.Normalizer`:
```kotlin
fun stripVietnameseDiacritics(input: String): String {
    val normalized = Normalizer.normalize(input, Normalizer.Form.NFD)
    return normalized
        .replace("\\p{M}".toRegex(), "")
        .replace("đ", "d").replace("Đ", "D")
}
```

### 3.2. Speed
```json
{"t":"spd","v":42}
```

| Field | Type | Required | Mô tả |
|---|---|---|---|
| `t` | string | yes | `"spd"` |
| `v` | int | yes | Tốc độ hiện tại tính bằng km/h |

**Nguồn dữ liệu**: `FusedLocationProviderClient` (Google Play Services), update mỗi 1-2s. Lấy `location.speed` (m/s) rồi convert sang km/h (`speed * 3.6f`).

Gửi tối đa 1 message/giây để không spam BLE.

### 3.3. Incoming Call
```json
{"t":"call","n":"Mom","p":"+84901234567"}
```

| Field | Type | Required | Mô tả |
|---|---|---|---|
| `t` | string | yes | `"call"` |
| `n` | string | yes | Tên contact (đã strip dấu), tối đa 31 ký tự |
| `p` | string | yes | Số điện thoại, tối đa 23 ký tự |

**Nguồn**: `TelephonyManager.PhoneStateListener` hoặc đọc từ NotificationListenerService khi notification của "com.android.phone" hoặc "com.google.android.dialer" trigger.

Khi cuộc gọi kết thúc/từ chối, gửi:
```json
{"t":"clr"}
```

### 3.4. SMS / Chat message
```json
{"t":"sms","f":"Anh","m":"Em o dau"}
```

| Field | Type | Required | Mô tả |
|---|---|---|---|
| `t` | string | yes | `"sms"` |
| `f` | string | yes | Tên người gửi (đã strip dấu), tối đa 31 ký tự |
| `m` | string | yes | Nội dung tin nhắn (đã strip dấu, tối đa 127 ký tự — nội dung dài hơn sẽ bị truncate) |

**Nguồn**: NotificationListenerService bắt notification từ các package: `com.google.android.apps.messaging`, `com.whatsapp`, `org.telegram.messenger`, `com.zing.zalo`, `com.facebook.orca`,...

### 3.5. Clock
```json
{"t":"clk","h":14,"m":30}
```

| Field | Type | Required | Mô tả |
|---|---|---|---|
| `t` | string | yes | `"clk"` |
| `h` | int | yes | Giờ (0-23) |
| `m` | int | yes | Phút (0-59) |

Gửi mỗi 30 giây là đủ.

### 3.6. Battery
```json
{"t":"bat","p":75}
```

| Field | Type | Required | Mô tả |
|---|---|---|---|
| `t` | string | yes | `"bat"` |
| `p` | int | yes | Phần trăm pin (0-100) |

Gửi khi có thay đổi hoặc mỗi 60s. ESP32 sẽ hiện màu đỏ khi pin ≤ 20%.

### 3.7. Clear / reset display
```json
{"t":"clr"}
```

Đưa ESP32 về màn IDLE. Dùng khi: cuộc gọi kết thúc, navigation dừng, hoặc user manual stop.

---

## 4. Android Components — Implementation Guide

### 4.1. Manifest permissions

```xml
<!-- BLE -->
<uses-permission android:name="android.permission.BLUETOOTH_SCAN"
    android:usesPermissionFlags="neverForLocation" />
<uses-permission android:name="android.permission.BLUETOOTH_CONNECT" />
<uses-permission android:name="android.permission.BLUETOOTH_ADVERTISE" />
<uses-permission android:name="android.permission.ACCESS_FINE_LOCATION" />

<!-- Notification listener -->
<uses-permission android:name="android.permission.BIND_NOTIFICATION_LISTENER_SERVICE"
    tools:ignore="ProtectedPermissions" />

<!-- Foreground service -->
<uses-permission android:name="android.permission.FOREGROUND_SERVICE" />
<uses-permission android:name="android.permission.FOREGROUND_SERVICE_CONNECTED_DEVICE" />
<uses-permission android:name="android.permission.FOREGROUND_SERVICE_LOCATION" />
<uses-permission android:name="android.permission.POST_NOTIFICATIONS" />

<!-- Phone state -->
<uses-permission android:name="android.permission.READ_PHONE_STATE" />
<uses-permission android:name="android.permission.READ_CONTACTS" />

<!-- Required Bluetooth feature -->
<uses-feature android:name="android.hardware.bluetooth_le" android:required="true" />
```

### 4.2. NotificationListenerService

Đây là cách duy nhất hợp pháp + ổn định để đọc nội dung notification của Google Maps mà không cần root.

```kotlin
class CarHudNotificationListener : NotificationListenerService() {
    override fun onNotificationPosted(sbn: StatusBarNotification) {
        when (sbn.packageName) {
            "com.google.android.apps.maps" -> handleMapsNotification(sbn)
            "com.google.android.dialer", "com.android.phone" -> handleCallNotification(sbn)
            "com.google.android.apps.messaging",
            "com.whatsapp",
            "com.zing.zalo",
            "org.telegram.messenger" -> handleMessageNotification(sbn)
        }
    }

    private fun handleMapsNotification(sbn: StatusBarNotification) {
        val title = sbn.notification.extras.getString(Notification.EXTRA_TITLE) ?: return
        val text  = sbn.notification.extras.getString(Notification.EXTRA_TEXT) ?: return
        val subText = sbn.notification.extras.getString(Notification.EXTRA_SUB_TEXT)

        // title example: "In 200 m" or "Now"
        // text example: "Turn right onto Le Loi"
        val nav = GoogleMapsNavParser.parse(title, text, subText)
        nav?.let { CarHudBus.publishNav(it) }
    }

    // ...similar for call / sms
}
```

Đăng ký service trong manifest:

```xml
<service android:name=".CarHudNotificationListener"
    android:label="Car HUD Notification Reader"
    android:permission="android.permission.BIND_NOTIFICATION_LISTENER_SERVICE"
    android:exported="false">
    <intent-filter>
        <action android:name="android.service.notification.NotificationListenerService" />
    </intent-filter>
</service>
```

User cần cấp quyền thủ công: Settings → Notifications → Device & app notifications → Special app access → Notification access → bật cho app.

### 4.3. GoogleMapsNavParser — parse notification text

Đây là phần **fragile nhất** vì Google có thể thay đổi format. Phải parse theo pattern:

```kotlin
object GoogleMapsNavParser {

    // Title pattern: "In <dist> <unit>" | "Now" | "<time> ETA"
    private val distancePattern = Regex(
        """In\s+(\d+(?:\.\d+)?)\s*(km|m|mi|ft)""",
        RegexOption.IGNORE_CASE
    )

    // Text pattern: "<action> onto <street>" | "<action> toward <street>"
    private val actionPatterns = mapOf(
        Regex("(?i)turn right")        to "right",
        Regex("(?i)turn left")         to "left",
        Regex("(?i)slight right")      to "slight-right",
        Regex("(?i)slight left")       to "slight-left",
        Regex("(?i)sharp right")       to "sharp-right",
        Regex("(?i)sharp left")        to "sharp-left",
        Regex("(?i)u-?turn")           to "uturn",
        Regex("(?i)continue|straight|head") to "straight",
    )

    data class NavInfo(
        val arrow: String,
        val distance: Int,
        val unit: String,
        val street: String
    )

    fun parse(title: String, text: String, subText: String?): NavInfo? {
        val distMatch = distancePattern.find(title) ?: run {
            // Could be "Now" - distance 0
            return parseNow(text)
        }
        val distValue = distMatch.groupValues[1].toFloatOrNull() ?: return null
        val unitRaw = distMatch.groupValues[2].lowercase()
        val (dist, unit) = normalizeDist(distValue, unitRaw)

        val arrow = actionPatterns.entries
            .firstOrNull { it.key.containsMatchIn(text) }
            ?.value ?: "straight"

        val street = extractStreet(text)

        return NavInfo(arrow, dist, unit, street)
    }

    private fun normalizeDist(value: Float, unit: String): Pair<Int, String> = when (unit) {
        "km" -> value.toInt() to "km"
        "m"  -> value.toInt() to "m"
        "mi" -> (value * 1.609f).toInt() to "km"
        "ft" -> (value * 0.305f).toInt() to "m"
        else -> value.toInt() to "m"
    }

    private fun extractStreet(text: String): String {
        // "Turn right onto Le Loi" -> "Le Loi"
        val keywords = listOf(" onto ", " toward ", " on ")
        for (k in keywords) {
            val idx = text.indexOf(k, ignoreCase = true)
            if (idx >= 0) return text.substring(idx + k.length).trim()
        }
        return text.trim()
    }

    private fun parseNow(text: String): NavInfo? {
        val arrow = actionPatterns.entries
            .firstOrNull { it.key.containsMatchIn(text) }
            ?.value ?: return null
        return NavInfo(arrow, 0, "m", extractStreet(text))
    }
}
```

**Testing strategy**: Vì format có thể thay đổi, viết unit test với 20+ ví dụ notification text thực tế trong nhiều ngôn ngữ Google Maps (EN, VI). Khi user báo lỗi parse, thêm test case mới.

### 4.4. BleClient — kết nối ESP32

```kotlin
class BleClient(private val context: Context) {

    companion object {
        val SERVICE_UUID = UUID.fromString("6e400001-b5a3-f393-e0a9-e50e24dcca9e")
        val RX_UUID      = UUID.fromString("6e400002-b5a3-f393-e0a9-e50e24dcca9e")
        val TX_UUID      = UUID.fromString("6e400003-b5a3-f393-e0a9-e50e24dcca9e")
        const val TARGET_NAME = "CarHUD-ESP32"
    }

    private var gatt: BluetoothGatt? = null
    private var rxChar: BluetoothGattCharacteristic? = null
    private val txQueue = LinkedBlockingQueue<ByteArray>()

    @SuppressLint("MissingPermission")
    fun startScanAndConnect() {
        val scanner = BluetoothAdapter.getDefaultAdapter().bluetoothLeScanner
        val filters = listOf(
            ScanFilter.Builder()
                .setServiceUuid(ParcelUuid(SERVICE_UUID))
                .build()
        )
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()
        scanner.startScan(filters, settings, scanCallback)
    }

    private val scanCallback = object : ScanCallback() {
        @SuppressLint("MissingPermission")
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            if (result.device.name == TARGET_NAME) {
                BluetoothAdapter.getDefaultAdapter()
                    .bluetoothLeScanner.stopScan(this)
                connect(result.device)
            }
        }
    }

    @SuppressLint("MissingPermission")
    private fun connect(device: BluetoothDevice) {
        gatt = device.connectGatt(context, true, gattCallback, BluetoothDevice.TRANSPORT_LE)
    }

    private val gattCallback = object : BluetoothGattCallback() {

        @SuppressLint("MissingPermission")
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                g.requestMtu(247)
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                rxChar = null
                // auto reconnect
                Handler(Looper.getMainLooper()).postDelayed({
                    g.connect()
                }, 2000)
            }
        }

        @SuppressLint("MissingPermission")
        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) {
            g.discoverServices()
        }

        @SuppressLint("MissingPermission")
        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            val svc = g.getService(SERVICE_UUID) ?: return
            rxChar = svc.getCharacteristic(RX_UUID)
            flushQueue()
        }

        override fun onCharacteristicWrite(
            g: BluetoothGatt,
            ch: BluetoothGattCharacteristic,
            status: Int
        ) {
            flushQueue()
        }
    }

    /** Send a JSON string to ESP32. Queues if not connected yet. */
    @SuppressLint("MissingPermission")
    fun send(json: String) {
        val bytes = json.toByteArray(Charsets.UTF_8)
        if (bytes.size > 240) {
            Log.w("BleClient", "Payload too large: ${bytes.size}")
            return
        }
        txQueue.offer(bytes)
        flushQueue()
    }

    @SuppressLint("MissingPermission")
    private fun flushQueue() {
        val ch = rxChar ?: return
        val g  = gatt ?: return
        val next = txQueue.poll() ?: return
        ch.value = next
        ch.writeType = BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
        g.writeCharacteristic(ch)
    }
}
```

> **Lưu ý API**: Code trên dùng API cũ (`ch.value = ...; g.writeCharacteristic(ch)`). Trên Android 13+ (API 33+), API mới là `gatt.writeCharacteristic(ch, value, writeType)`. Cần check `Build.VERSION.SDK_INT` và dùng API tương ứng.

### 4.5. Foreground Service

App phải chạy foreground service để duy trì BLE và notification listener khi user khóa màn hình:

```kotlin
class CarHudService : Service() {
    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val notification = NotificationCompat.Builder(this, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_hud)
            .setContentTitle("Car HUD active")
            .setContentText("Streaming to ESP32 via BLE")
            .setOngoing(true)
            .build()

        startForeground(NOTIF_ID, notification,
            ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
            or ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION
        )
        return START_STICKY
    }
}
```

### 4.6. SpeedProvider (FusedLocation)

```kotlin
class SpeedProvider(context: Context, private val onSpeed: (Int) -> Unit) {
    private val client = LocationServices.getFusedLocationProviderClient(context)

    private val request = LocationRequest.Builder(
        Priority.PRIORITY_HIGH_ACCURACY, 1000L
    ).setMinUpdateIntervalMillis(500L).build()

    private val callback = object : LocationCallback() {
        override fun onLocationResult(result: LocationResult) {
            val loc = result.lastLocation ?: return
            if (loc.hasSpeed()) {
                val kmh = (loc.speed * 3.6f).toInt()
                onSpeed(kmh)
            }
        }
    }

    @SuppressLint("MissingPermission")
    fun start() = client.requestLocationUpdates(request, callback, Looper.getMainLooper())
    fun stop()  = client.removeLocationUpdates(callback)
}
```

### 4.7. Phối hợp luồng - JsonEncoder và bus

```kotlin
object CarHudBus {
    private lateinit var ble: BleClient

    fun init(bleClient: BleClient) { ble = bleClient }

    fun publishNav(nav: GoogleMapsNavParser.NavInfo) {
        val json = JSONObject().apply {
            put("t", "nav")
            put("arr", nav.arrow)
            put("d", nav.distance)
            put("u", nav.unit)
            put("s", stripVietnameseDiacritics(nav.street).take(47))
        }.toString()
        ble.send(json)
    }

    fun publishSpeed(kmh: Int) {
        ble.send("""{"t":"spd","v":$kmh}""")
    }

    fun publishCall(name: String, phone: String) {
        val json = JSONObject().apply {
            put("t", "call")
            put("n", stripVietnameseDiacritics(name).take(31))
            put("p", phone.take(23))
        }.toString()
        ble.send(json)
    }

    fun publishSms(from: String, msg: String) {
        val json = JSONObject().apply {
            put("t", "sms")
            put("f", stripVietnameseDiacritics(from).take(31))
            put("m", stripVietnameseDiacritics(msg).take(127))
        }.toString()
        ble.send(json)
    }

    fun publishClock(h: Int, m: Int) {
        ble.send("""{"t":"clk","h":$h,"m":$m}""")
    }

    fun publishBattery(pct: Int) {
        ble.send("""{"t":"bat","p":$pct}""")
    }

    fun publishClear() {
        ble.send("""{"t":"clr"}""")
    }
}
```

---

## 5. Testing & Debugging

### 5.1. Test phía ESP32 không cần app

Trước khi viết app Android, test firmware ESP32 bằng:

1. Cài **nRF Connect** (Nordic Semiconductor) trên Play Store
2. Scan → tìm `CarHUD-ESP32` → Connect
3. Tìm service `6e400001-...` → expand → tìm char `6e400002-...` (write)
4. Tap upload icon → chọn "Send", paste JSON, gửi
5. Quan sát màn TFT thay đổi

Ví dụ payload để gửi (hex hoặc text):
```json
{"t":"nav","arr":"right","d":150,"u":"m","s":"Le Loi"}
```

### 5.2. Build dần dần

| Sprint | Mục tiêu |
|---|---|
| 1 | Connect BLE + gửi 1 hardcoded JSON từ button "Test" trong app |
| 2 | NotificationListenerService bắt được notification Google Maps (log ra) |
| 3 | Parse text Google Maps thành nav object đúng |
| 4 | FusedLocation gửi speed |
| 5 | Call + SMS notification |
| 6 | Foreground service + auto-reconnect |
| 7 | UI app: pairing screen, status, log viewer |

### 5.3. Logging

- Mở Serial monitor ESP32 (115200) để xem JSON nhận được
- Android: `adb logcat | grep -E "CarHud|BleClient"`

---

## 6. Edge cases & lưu ý quan trọng

### 6.1. Tiếng Việt
Google Maps tiếng Việt có thể trả text dạng "Rẽ phải vào Lê Lợi" thay vì "Turn right onto Le Loi". Parser cần handle cả 2 ngôn ngữ:

```kotlin
private val actionPatternsVi = mapOf(
    Regex("(?i)rẽ phải")     to "right",
    Regex("(?i)rẽ trái")     to "left",
    Regex("(?i)quay đầu")    to "uturn",
    Regex("(?i)đi thẳng|tiếp tục") to "straight",
)
```

Khuyến nghị: set Google Maps về tiếng Anh trong app settings để parser ổn định hơn. Hoặc parser handle cả 2.

### 6.2. Rate limiting
- BLE write có thể fail nếu spam. Dùng queue (đã có trong code mẫu).
- Speed update tối đa 1 lần/giây.
- Nav update chỉ khi có thay đổi.

### 6.3. Doze mode
Android 6+ có Doze mode tắt BLE khi màn hình tắt. Foreground service mitigate phần lớn, nhưng vẫn cần test thực tế.

### 6.4. Pairing & security
Hiện firmware ESP32 dùng **no security** (just-works pairing). Phù hợp cho car HUD vì không có dữ liệu nhạy cảm. Nếu cần security:
- Thêm `NimBLEDevice::setSecurityAuth(...)` phía ESP32
- Bonded pairing phía Android

### 6.5. Battery của điện thoại
App chạy GPS + BLE liên tục → tốn pin. Khuyến nghị:
- Chỉ start service khi user mở Google Maps (qua AppOps hoặc UsageStatsManager)
- Stop service khi disconnect khỏi ESP32 > 5 phút

### 6.6. ESP32 nguồn điện
Khi gắn lên ô tô, cấp nguồn 5V qua USB từ tẩu sạc → board OK. Nhưng:
- Tránh sạc khi ô tô khởi động (nhiễu nguồn lớn có thể reset ESP32)
- Nên có tụ ceramic 100nF + tụ điện 10µF gần chân nguồn module

---

## 7. Roadmap nâng cấp

Sau MVP, các tính năng có thể thêm sau:

| Feature | Mức độ khó |
|---|---|
| Hỗ trợ Unicode tiếng Việt có dấu (font `.vlw` trên SPIFFS) | trung bình |
| Bidirectional: nút bấm ESP32 trả lời cuộc gọi qua BLE | khó |
| Voice command qua BLE → đọc TTS trên điện thoại | trung bình |
| Lưu lịch sử navigation gần nhất khi mất BLE | dễ |
| Cảnh báo tốc độ vượt giới hạn (so với map data) | khó |
| Dark/Light theme tự động theo giờ | dễ |
| Themes / animation chuyển trang | trung bình |

---

## 8. Reference - files & links

- Firmware repo (ESP32): `firmware/` (đính kèm)
- BLE NUS spec: `https://docs.nordicsemi.com/bundle/ncs-latest/page/nrf/libraries/bluetooth_services/services/nus.html`
- Android BLE guide: `https://developer.android.com/develop/connectivity/bluetooth/ble/connect-gatt-server`
- NotificationListenerService docs: `https://developer.android.com/reference/android/service/notification/NotificationListenerService`
- FusedLocationProvider: `https://developer.android.com/develop/sensors-and-location/location/retrieve-current`
- nRF Connect (test tool): Play Store
- ArduinoJson docs: `https://arduinojson.org/`
- NimBLE-Arduino: `https://github.com/h2zero/NimBLE-Arduino`