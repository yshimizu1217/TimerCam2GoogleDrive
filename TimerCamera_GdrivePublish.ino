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

// Variables to store in RTC memory (retained after deep sleep)
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR int failedUploadCount = 0;

void setup() {
  Serial.begin(115200);
  delay(10);

  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  bootCount++;
  Serial.println("Boot number: " + String(bootCount));
  Serial.println("Failed upload count: " + String(failedUploadCount));

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
  config.fb_count     = 1;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY; //CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);

  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    delay(1000);
    ESP.restart();
  }

  sensor_t * s = esp_camera_sensor_get();
  //initial sensors are flipped vertically and colors are a bit saturated
  s->set_vflip(s, 1);//flip it back
  s->set_brightness(s, -1);//up the blightness just a bit　Setting OV3660 closer to OV2640
  s->set_saturation(s, 2);//lower the saturation　Setting OV3660 closer to OV2640
  s->set_denoise(s, 1);//Setting OV3660 closer to OV2640
  s->set_contrast(s, 1);//Setting OV3660 closer to OV2640
  //drop down frame size for higher initial frame rate
  // s->set_framesize(s, FRAMESIZE_SXGA);
  delay(1000);
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

      camera_fb_t * fb = esp_camera_fb_get(); //Discard the first one
      esp_camera_fb_return(fb); //Discard the first one
      fb = esp_camera_fb_get();  //Save here
      if(!fb) {
        Serial.println("Camera capture failed");
        client.stop();
        retryCount++;
        delay(1000);
        continue;
      }
      Serial.printf("frame buffer size: %u x %u\n", fb->width, fb->height);
      
      Serial.println("Step 1: calicurating data size...");    // Count the size of Base64 and urlencoded data.

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
      Serial.printf("frame buffer size: %u\n", urlencodedSize);

      Serial.println("Step 2: Sending a captured image to Google Drive.");
      String Data = myFilename + mimeType + myImage;    // The beginning of the data sent by POST. This is followed by the Base64 version of the image.
      client.println("POST " + myScript + " HTTP/1.1");
      client.println("Host: " + String(myDomain));
      client.println("Content-Length: " + String(Data.length() + urlencodedSize));    // Step 1 is required here because we need to write the length of the data.
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