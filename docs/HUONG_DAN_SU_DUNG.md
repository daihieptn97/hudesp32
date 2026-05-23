# Hướng Dẫn Sử Dụng ESP32 BLE Display

## Tính Năng

Ứng dụng ESP32 này hỗ trợ nhận và hiển thị:
1. **Văn bản tiếng Việt** - Tự động chuyển đổi văn bản có dấu thành không dấu để hiển thị
2. **Ảnh bitmap RGB565** - Hiển thị ảnh màu từ Android

## Giao Thức Truyền Dữ Liệu

### **Gửi Lệnh Test**
```
Gửi chuỗi: "TEST" để hiển thị pattern test màn hình
```

### **Gửi Văn Bản**
- Gửi trực tiếp chuỗi UTF-8
- Ví dụ: "Xin chào, tôi là ESP32!"

### 2. Gửi Ảnh Bitmap
- **Format header**: `IMG:[width]:[height]:`
- **Dữ liệu ảnh**: RGB565 format (2 bytes per pixel)
- **Ví dụ**: `IMG:100:100:` + 20000 bytes dữ liệu RGB565 cho ảnh 100x100 pixels

## Thông Số Kỹ Thuật

- **Màn hình**: LilyGO T-Display-S3 với ST7789 driver
- **Kích thước màn hình**: 170x320 pixels
- **Kích thước ảnh tối đa**: 170x320 pixels
- **Format ảnh**: RGB565 (16-bit color)
- **Kích thước dữ liệu tối đa**: 108,800 bytes (170×320×2)
- **BLE MTU**: Tự động xử lý chunk data (khuyến nghị 20 bytes/chunk)
- **Orientation**: rotation(1) - landscape mode

## Cách Sử Dụng Từ Android

### Gửi Văn Bản
```java
// Gửi văn bản UTF-8
String text = "Xin chào ESP32!";
bluetoothCharacteristic.setValue(text.getBytes("UTF-8"));
bluetoothGatt.writeCharacteristic(bluetoothCharacteristic);
```

### Gửi Ảnh Bitmap
```java
// Gửi ảnh test đơn giản 50x50
private void sendTestImage() {
    int width = 50, height = 50;
    String header = "IMG:" + width + ":" + height + ":";
    byte[] headerBytes = header.getBytes();
    
    // Tạo dữ liệu ảnh RGB565
    byte[] imageData = new byte[width * height * 2];
    int index = 0;
    
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int red = (x * 31) / width;
            int green = (y * 63) / height; 
            int blue = ((x + y) * 31) / (width + height);
            int rgb565 = (red << 11) | (green << 5) | blue;
            
            // Little endian
            imageData[index++] = (byte)(rgb565 & 0xFF);
            imageData[index++] = (byte)((rgb565 >> 8) & 0xFF);
        }
    }
    
    // Gộp và gửi theo chunks
    byte[] fullData = new byte[headerBytes.length + imageData.length];
    System.arraycopy(headerBytes, 0, fullData, 0, headerBytes.length);
    System.arraycopy(imageData, 0, fullData, headerBytes.length, imageData.length);
    
    sendDataInChunks(fullData, 20); // 20 bytes per chunk
}

// Gửi lệnh test
private void sendTestCommand() {
    bluetoothCharacteristic.setValue("TEST".getBytes());
    bluetoothGatt.writeCharacteristic(bluetoothCharacteristic);
}
```

## UUID BLE

- **Service UUID**: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- **RX Characteristic**: `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` (gửi dữ liệu đến ESP32)
- **TX Characteristic**: `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` (nhận phản hồi từ ESP32)

## Debug và Monitor

ESP32 sẽ gửi thông tin debug qua Serial Monitor:
- Trạng thái kết nối BLE
- Thông tin dữ liệu nhận được
- Thông tin xử lý ảnh
- Lỗi và cảnh báo

## Lưu Ý

1. **Bộ nhớ**: ESP32 có bộ nhớ hạn chế, ảnh lớn có thể gây lỗi
2. **Tốc độ**: Truyền ảnh qua BLE có thể mất thời gian
3. **Format ảnh**: Chỉ hỗ trợ RGB565, không hỗ trợ JPEG/PNG
4. **Kết nối**: Chỉ hỗ trợ một kết nối BLE tại một thời điểm

## Troubleshooting

- **Ảnh không hiển thị**: Kiểm tra format header và kích thước dữ liệu
- **Ảnh bị lỗi**: Đảm bảo dữ liệu RGB565 đúng format
- **Kết nối bị mất**: ESP32 sẽ tự động khởi động lại advertising
- **Out of memory**: Giảm kích thước ảnh hoặc restart ESP32
