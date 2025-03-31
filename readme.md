# M5STACK Timer Camera - Google Drive

Code that uses M5Stack's TimerCamera to periodically output images to Google Drive.

The applicable products are:
- [ESP32 PSRAM Timer Camera X (OV3660)](https://shop.m5stack.com/products/esp32-psram-timer-camera-x-ov3660)
- [ESP32 PSRAM Timer Camera F (OV3660)](https://shop.m5stack.com/products/esp32-psram-timer-camera-fisheye-ov3660)


### How to use

- Create the content of upload.gs in Google Apps Script and deploy it. Take note of the deployment ID (or URL) at that time.
- Enter the following URL into the Additional Boards Manager in the Arduino IDE:
    -  https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json
- The following libraries will be introduced:
    - M5Stack by M5Stack
    - Timer-CAM by M5Stack
- Edit the SSID/PASS and GAS deployment ID (URL) parts of the TimerCamera_GdrivePublish.ino file.
- Verify and write.



# Exif Datetime Setter

Since data taken with the TimerCamera does not contain EXIF ​​information, the shooting date is set from the file name.# Exif Datetime Setter

### Setup
- Install the required libraries:
    - `pip install pillow piexif`


### Build
pyinstaller exif-datetime-setter.spec


### How to use

- Basic usage (process current directory, skip existing EXIF ​​information):
    - `exif-datetime-setter.exe`
- Process a specific directory:
    - `exif-datetime-setter.exe /path/to/images`
- Force process all images (overwrite existing EXIF ​​information):
    - `exif-datetime-setter.exe --force`
- Recursively process including subdirectories:
    - `exif-datetime-setter.exe --recursive`
- Force process a specific directory and its subdirectories:
    - `exif-datetime-setter.exe /path/to/images --recursive --force`
