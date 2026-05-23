// Ví dụ Android code để gửi ảnh test đơn giản
// Thêm vào Activity hoặc Fragment của bạn

private void sendTestImage() {
    // Tạo một ảnh test đơn giản 50x50 pixels
    int width = 50;
    int height = 50;
    
    // Tạo header
    String header = "IMG:" + width + ":" + height + ":";
    byte[] headerBytes = header.getBytes();
    
    // Tạo dữ liệu ảnh RGB565 đơn giản (gradient màu đỏ)
    byte[] imageData = new byte[width * height * 2];
    int index = 0;
    
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            // Tạo gradient từ đen (0) đến đỏ (31 << 11)
            int red = (x * 31) / width;
            int green = (y * 63) / height;
            int blue = ((x + y) * 31) / (width + height);
            
            // Chuyển sang RGB565
            int rgb565 = (red << 11) | (green << 5) | blue;
            
            // Little endian
            imageData[index++] = (byte)(rgb565 & 0xFF);
            imageData[index++] = (byte)((rgb565 >> 8) & 0xFF);
        }
    }
    
    // Gộp header và data
    byte[] fullData = new byte[headerBytes.length + imageData.length];
    System.arraycopy(headerBytes, 0, fullData, 0, headerBytes.length);
    System.arraycopy(imageData, 0, fullData, headerBytes.length, imageData.length);
    
    // Gửi qua BLE
    if (bluetoothGatt != null && rxCharacteristic != null) {
        // Gửi theo chunks do giới hạn MTU của BLE (thường là 20-244 bytes)
        int chunkSize = 20; // Kích thước chunk nhỏ để đảm bảo tương thích
        int offset = 0;
        
        while (offset < fullData.length) {
            int remainingBytes = fullData.length - offset;
            int currentChunkSize = Math.min(chunkSize, remainingBytes);
            
            byte[] chunk = new byte[currentChunkSize];
            System.arraycopy(fullData, offset, chunk, 0, currentChunkSize);
            
            rxCharacteristic.setValue(chunk);
            boolean result = bluetoothGatt.writeCharacteristic(rxCharacteristic);
            
            Log.d("BLE", "Sent chunk " + offset + "-" + (offset + currentChunkSize) + 
                  " (" + currentChunkSize + " bytes): " + result);
            
            offset += currentChunkSize;
            
            // Delay nhỏ giữa các chunk
            try {
                Thread.sleep(50);
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
        }
        
        Log.d("BLE", "Image sent successfully: " + fullData.length + " bytes total");
    }
}

private void sendTestCommand() {
    // Gửi lệnh test để hiển thị pattern
    if (bluetoothGatt != null && rxCharacteristic != null) {
        String testCommand = "TEST";
        rxCharacteristic.setValue(testCommand.getBytes());
        bluetoothGatt.writeCharacteristic(rxCharacteristic);
        Log.d("BLE", "Test command sent");
    }
}

// Cách sử dụng:
// Gọi sendTestCommand() để test pattern
// Gọi sendTestImage() để test gửi ảnh đơn giản
