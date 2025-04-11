#include "M5TimerCAM.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "Base64.h"

const char *ssid     = "SSID";
const char *password = "SSID PASS";

const char* myDomain = "script.google.com";
String myScript = "/macros/s/***GAS_DeplyID***/exec"; //Replace with your own url
String myFilename = "filename=M5Camera.jpg";
String mimeType = "&mimetype=image/jpeg";
String myImage = "&data=";

int waitingTime = 10; //Wait 10 seconds to google response.

int shootingIntervalSec = 60;
int maxWiFiConnectAttempts = 20;
int wifiReconnectDelay = 500;   // millisecond
int maxUploadRetries = 3;
int maxCaptureRetries = 8;

// Variables to store in RTC memory (retained after deep sleep)
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR int failedUploadCount = 0;
RTC_DATA_ATTR int lastLightLevel = 128; // Remember last light level
// Store camera settings in RTC memory
RTC_DATA_ATTR int currentBrightness = 1;
RTC_DATA_ATTR int currentAecValue = 300;

void setup() {
  Serial.begin(115200);
  delay(100); // To stabilize communication

  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  bootCount++;
  Serial.println("Boot number: " + String(bootCount));
  Serial.println("Failed upload count: " + String(failedUploadCount));

  delay(500); // wait a while after launching
  
  initCamera();
  initWiFi();

  Serial.println("");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection lost. Attempting to reconnect...");
    reconnectWiFi();
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (saveCapturedImage()) {
      failedUploadCount = 0;
      Serial.println("Upload successful!");
    } else {
      failedUploadCount++;
      Serial.println("Upload failed! Count: " + String(failedUploadCount));
      
      if (failedUploadCount >= 3) {
        Serial.println("Too many failed uploads. Rebooting...");
        ESP.restart();
        return;
      }
    }
  } else {
    Serial.println("WiFi not connected. Cannot upload image.");
    failedUploadCount++;
    
    if (failedUploadCount >= 5) {
      Serial.println("WiFi connection issues persist. Rebooting...");
      ESP.restart();
      return;
    }
  }

  Serial.printf("Waiting for %u sec.\n", shootingIntervalSec);
  Serial.printf("esp_sleep_enable_timer_wakeup: %d\n", esp_sleep_enable_timer_wakeup((uint64_t)shootingIntervalSec * 1000ULL * 1000ULL)); //microsecond
  esp_deep_sleep_start();
}

void initCamera() {
  esp_camera_deinit();
  delay(100);

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 10000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_UXGA;  // UXGA|SXGA|XGA|SVGA|VGA|CIF|QVGA|HQVGA|QQVGA
  config.jpeg_quality = 5;
  config.fb_count = 1;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  esp_err_t err = esp_camera_init(&config);

  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    delay(1000);
    ESP.restart();
  }

  sensor_t * s = esp_camera_sensor_get();
  s->set_vflip(s, 1);

  // Basic camera settings
  s->set_saturation(s, 3); // Maintain saturation
  s->set_denoise(s, 1);    // Enable noise reduction
  s->set_sharpness(s, 1);  // Sharpness setting

  // Auto mode settings (important)
  s->set_exposure_ctrl(s, 1);  // Enable automatic exposure control
  s->set_gain_ctrl(s, 1);      // Enable automatic gain control
  s->set_whitebal(s, 1);       // Enable automatic white balance
  s->set_awb_gain(s, 1);       // Enable AWB gain
  s->set_aec2(s, 1);           // Enable AEC automatic exposure correction

  // Start with intermediate values for brightness and contrast
  s->set_brightness(s, currentBrightness);  // Restore from RTC variable
  s->set_contrast(s, 1);       // Standard contrast

  // Special settings - adjustments for low light environments
  // Set AEC maximum and minimum values (exposure control over wider range)
  s->set_aec_value(s, currentAecValue);  // Restore from RTC variable

  delay(2000);  // Ensure wait time after camera initialization

  // Perform test capture to evaluate environment brightness
  camera_fb_t * test_fb = NULL;
  for (int i = 0; i < 3; i++) {
    test_fb = esp_camera_fb_get();
    if (test_fb) {
      // Evaluate image brightness
      uint32_t brightness = evaluateImageBrightness(test_fb);
      Serial.println("Image brightness level: " + String(brightness));
      
      // Adjust settings based on previous and current brightness
      adjustCameraSettings(s, brightness);
      
      esp_camera_fb_return(test_fb);
      Serial.println("Test capture " + String(i+1) + " done");
    }
    delay(500);
  }
}

uint32_t evaluateImageBrightness(camera_fb_t *fb) {
  if (!fb || fb->len < 100) return 128; // Default value
  
  // Sample first 100 bytes of JPEG to estimate brightness
  uint32_t sum = 0;
  int samples = 0;
  
  // Get more samples (up to 1000 bytes)
  for (size_t i = 0; i < fb->len && i < 1000; i++) {
    if (i % 10 == 0) {  // Sample every 10 bytes
      sum += fb->buf[i];
      samples++;
    }
  }
  
  if (samples == 0) return 128;
  return sum / samples;
}

// Function to adjust camera settings based on brightness
void adjustCameraSettings(sensor_t *s, uint32_t brightness) {
  Serial.println("Adjusting camera settings based on brightness: " + String(brightness));
  
  // Adaptively change settings based on brightness level
  if (brightness < 60) {  // Very dark environment
    Serial.println("Very low light conditions detected");
    currentBrightness = 2;           // Maximum brightness
    s->set_brightness(s, currentBrightness);
    s->set_contrast(s, 0);           // Lower contrast
    s->set_saturation(s, 4);         // Increase saturation
    currentAecValue = 600;           // Set high exposure value
    s->set_aec_value(s, currentAecValue);
    s->set_gainceiling(s, GAINCEILING_32X); // Maximum gain ceiling
    s->set_lenc(s, 1);               // Enable lens correction
  } 
  else if (brightness < 90) {  // Dark environment
    Serial.println("Low light conditions detected");
    currentBrightness = 1;           // Increase brightness
    s->set_brightness(s, currentBrightness);
    s->set_contrast(s, 1);           // Lower contrast
    s->set_saturation(s, 3);         // Maintain saturation
    currentAecValue = 400;           // Increase exposure value
    s->set_aec_value(s, currentAecValue);
    s->set_gainceiling(s, GAINCEILING_16X); // Increase gain ceiling
  }
  else if (brightness > 180) {  // Very bright environment
    Serial.println("Very bright conditions detected");
    currentBrightness = 0;           // Lower brightness
    s->set_brightness(s, currentBrightness);
    s->set_contrast(s, 2);           // Increase contrast
    s->set_saturation(s, 2);         // Lower saturation
    currentAecValue = 100;           // Lower exposure value
    s->set_aec_value(s, currentAecValue);
    s->set_gainceiling(s, GAINCEILING_2X); // Lower gain ceiling
  }
  else {  // Standard brightness
    Serial.println("Normal lighting conditions");
    currentBrightness = 0;           // Standard brightness
    s->set_brightness(s, currentBrightness);
    s->set_contrast(s, 2);           // Standard contrast
    s->set_saturation(s, 2);         // Standard saturation
    currentAecValue = 350;           // Standard exposure value
    s->set_aec_value(s, currentAecValue);
    s->set_gainceiling(s, GAINCEILING_8X); // Standard gain
  }

  // Save brightness level
  lastLightLevel = brightness;

  // Wait for new settings to be applied
  delay(500);
}

void initWiFi() {
  WiFi.mode(WIFI_STA);
  
  Serial.println("");
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < maxWiFiConnectAttempts) {
    Serial.print(".");
    delay(wifiReconnectDelay);
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("");
    Serial.println("WiFi connection failed");
  }
}

bool reconnectWiFi() {
  WiFi.disconnect();
  delay(100);

  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < maxWiFiConnectAttempts) {
    Serial.print(".");
    delay(wifiReconnectDelay);
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi reconnected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    return true;
  } else {
    Serial.println("");
    Serial.println("WiFi reconnection failed");
    return false;
  }
}

bool saveCapturedImage() {
  Serial.println("Connect to " + String(myDomain));
  WiFiClientSecure client;

  int retryCount = 0;
  bool uploadSuccess = false;

  while (!uploadSuccess && retryCount < maxUploadRetries) {
    client.setInsecure();
    
    if (client.connect(myDomain, 443)) {
      Serial.println("Connection successful");
      
      // Improve camera capture reliability
      Serial.println("Preparing camera...");
      delay(800);  // Wait longer before capture
      
      // Try multiple capture attempts
      int captureRetries = 0;
      camera_fb_t * fb = NULL;
      sensor_t * s = esp_camera_sensor_get();
      
      while (captureRetries < maxCaptureRetries && fb == NULL) {
        Serial.println("Attempting camera capture " + String(captureRetries + 1) + "...");
        
        // Adjust settings as capture attempts increase
        if (captureRetries > 0) {
          // Use RTC variables to get current settings (instead of using get_ methods)
          // Gradually increase sensitivity with retries
          currentBrightness = min(currentBrightness + 1, 2);
          s->set_brightness(s, currentBrightness);
          currentAecValue = min(currentAecValue + 100, 800);
          s->set_aec_value(s, currentAecValue);
          
          Serial.println("Adjusted settings - Brightness: " + String(currentBrightness) + ", AEC: " + String(currentAecValue));
          
          if (captureRetries >= 3) {
            // For darker conditions, increase gain ceiling
            s->set_gainceiling(s, GAINCEILING_32X);
          }
          
          Serial.println("Adjusted camera settings for retry");
          delay(500); // Wait for settings to apply
        }
        
        // Add delay for auto exposure stabilization
        delay(500);
        
        // Try to get a frame
        fb = esp_camera_fb_get();
        
        if (fb != NULL) {
          Serial.println("Capture successful!");
          Serial.printf("Size: %u bytes, %u x %u resolution\n", fb->len, fb->width, fb->height);
          
          // Evaluate image brightness
          uint32_t brightness = evaluateImageBrightness(fb);
          Serial.println("Captured image brightness: " + String(brightness));
          
          // Very dark image but use it anyway
          if (brightness < 30 && captureRetries < maxCaptureRetries - 1) {
            Serial.println("Image too dark, but attempting another capture with adjusted settings");
            esp_camera_fb_return(fb);
            fb = NULL;
            
            // Significant adjustment for dark image detection
            currentBrightness = 2;  // Maximum brightness
            s->set_brightness(s, currentBrightness);
            s->set_contrast(s, 0);    // Minimum contrast
            currentAecValue = 800; // Maximum exposure
            s->set_aec_value(s, currentAecValue);
            s->set_gainceiling(s, GAINCEILING_32X); // Maximum gain
            
            delay(800); // Wait until the settings are applied
          } else {
            // When the brightness is within acceptable range or the maximum number of retries is reached
            break;
          }
        } else {
          Serial.println("Camera capture failed, retry " + String(captureRetries + 1) + " of " + String(maxCaptureRetries));
          captureRetries++;
          delay(1000); // Wait before next attempt
        }
      }
      
      if (!fb) {
        Serial.println("Camera capture failed after multiple attempts");
        client.stop();
        retryCount++;
        delay(2000);
        continue;
      }
      
      Serial.printf("Frame buffer size: %u x %u\n", fb->width, fb->height);
      
      Serial.println("Step 1: calculating data size...");    // Count the size of Base64 and urlencoded data.

      int index = 0;
      uint8_t *p = fb->buf;
      int rest = fb->len;
      int base64EncodedSize = 0;
      int urlencodedSize = 0;
      while (rest > 0)
      {
        char output[2048 +1];    // A buffer to put the Base64ized data to be output at once (base64_encode() adds a null to the end, so add one byte.)
        int srcLen = rest > 1536 ? 1536 : rest;   // Size of original data to be encoded in this cycle (maximum is 3/4 of the buffer size)
        int encLen = base64_encode(output, (char *)p + index, srcLen);   // Base64 encode
        base64EncodedSize += encLen;
        if (encLen > 0) {
          String str = urlencode(String(output));   // URL encode
          urlencodedSize += str.length();
        }
        index += srcLen;
        rest -= srcLen;
      }
      Serial.printf("frame buffer size: %u\n", fb->len);
      Serial.printf("after Base64 encoding: %u\n", base64EncodedSize);
      Serial.printf("after URL encoding: %u\n", urlencodedSize);

      Serial.println("Step 2: Sending a captured image to Google Drive.");
      String Data = myFilename + mimeType + myImage;    // The beginning of the data sent by POST. This is followed by the Base64 version of the image.
      client.println("POST " + myScript + " HTTP/1.1");
      client.println("Host: " + String(myDomain));
      client.println("Content-Length: " + String(Data.length() + urlencodedSize));
      client.println("Content-Type: application/x-www-form-urlencoded");
      client.println();
      client.print(Data);

      index = 0;
      p = fb->buf;
      rest = fb->len;
      Serial.printf("Estimated cycle: %u\n", rest / 1536);
      while (rest > 0 && client.connected())
      {
        char output[2048 +1];    // A buffer to put the Base64ized data to be output at once (base64_encode() adds a null to the end, so add one byte.)
        int srcLen = rest > 1536 ? 1536 : rest;   // Size of original data to be encoded in this cycle (maximum is 3/4 of the buffer size)
        int encLen = base64_encode(output, (char *)p + index, srcLen);    // Base64 encode
        if (encLen > 0) {
          String str = urlencode(String(output));   // URL encode
          client.write((uint8_t *)(str.c_str()), str.length());   // Send data
          index += srcLen;
          rest -= srcLen;
          Serial.print(".");
        }
      }
      Serial.println();
      client.flush();

      esp_camera_fb_return(fb);

      Serial.println("Send a captured image to Google Drive.");
      Serial.println("Waiting for response.");
      long int StartTime=millis();
      bool receivedResponse = false;
      
      while (!client.available()) {
        Serial.print(".");
        delay(100);
        if ((StartTime+waitingTime * 1000) < millis()) {
          Serial.println();
          Serial.println("No response.");
          break;
        }
      }
      
      Serial.println();
      
      // Checking the response
      String response = "";
      while (client.available()) {
        char c = client.read();
        response += c;
        Serial.print(c);
      }
      
      // Parse the response to determine success
      if (response.indexOf("HTTP/1.1 200") >= 0 || response.indexOf("HTTP/1.1 302") >= 0) {
        uploadSuccess = true;
      } else {
        Serial.println("Upload failed with response: " + response);
        retryCount++;
      }
      
    } else {         
      Serial.println("Connected to " + String(myDomain) + " failed.");
      retryCount++;
    }
    
    client.stop();
    
    if (!uploadSuccess && retryCount < maxUploadRetries) {
      Serial.println("Retrying upload in 2 seconds...");
      delay(2000);
    }
  }

  return uploadSuccess;
}

//https://github.com/zenmanenergy/ESP8266-Arduino-Examples/
String urlencode(String str)
{
    String encodedString="";
    char c;
    char code0;
    char code1;
    char code2;
    for (int i =0; i < str.length(); i++){
      c=str.charAt(i);
      if (c == ' '){
        encodedString+= '+';
      } else if (isalnum(c)){
        encodedString+=c;
      } else{
        code1=(c & 0xf)+'0';
        if ((c & 0xf) >9){
            code1=(c & 0xf) - 10 + 'A';
        }
        c=(c>>4)&0xf;
        code0=c+'0';
        if (c > 9){
            code0=c - 10 + 'A';
        }
        code2='\0';
        encodedString+='%';
        encodedString+=code0;
        encodedString+=code1;
        //encodedString+=code2;
      }
      yield();
    }
    return encodedString;
}
