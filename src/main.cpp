/*
  Copyright 2020 fukuen

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#include <Arduino.h>
#include <Maixduino_GC0328.h>
#include <Sipeed_ST7789.h>
#include <stdio.h>
#include <WiFiEsp32.h>
#include <IPAddress.h>
#include <Print.h>
#include <SPI.h>
#include "toojpeg/toojpeg.h"
#include <WiFiEspUdp.h>
#include "ArduinoMDNS.h"

SPIClass spi_(SPI0); // MUST be SPI0 for Maix series on board LCD
Sipeed_ST7789 lcd(320, 240, spi_);

Maixduino_GC0328 camera(FRAMESIZE_QVGA, PIXFORMAT_RGB565);
//Maixduino_GC0328 camera(FRAMESIZE_QVGA, PIXFORMAT_YUV422);

WiFiEspUDP udp;
MDNS mdns(udp);

char SSID[] = "xxxxxxxxxxxx";        // your network SSID (name)
char pass[] = "zzzzzzzzzzzz";        // your network password
int status = WL_IDLE_STATUS;

WiFiEspServer server(80);
// use a ring buffer to increase speed and reduce memory allocation
EspRingBuffer buf(64);

#define BODY1 \
  "<!DOCTYPE html>\n"\
  "<meta http-equiv=\"refresh\" content=\"30;\">\n"\
  "<html>\n"\
  "<head> <meta content=\"text/html\" charset=\"UTF-8\"> </head>\n"\
  "<body>\n"\
  "<img src=\"/jpg\">\n"\
  "</body>\n"\
  "</html>"

/*
 * HTML Response
 */
void sendHttpResponse(WiFiEspClient client) {
  int len = sizeof(BODY1);
  // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
  // and a content-type so the client knows what's coming, then a blank line:
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html");
  client.printf("Content-lengzh: %d\n", len);
  client.println("Connection: close");
  client.println();

  client.print(BODY1);
  
  // The HTTP response ends with another blank line:
  client.println();
}

int pos = 0;
auto pixels = new unsigned char[320 * 240 * 3];
auto out = new unsigned char[320 * 240 * 3];

void myOutput(unsigned char oneByte) {
  out[pos] = oneByte;
  pos++;
}

/*
 * HTML Response img
 */
void sendHttpResponseJpg(WiFiEspClient client) {
  // take snapshot 1 RGB565 & send to LCD
  camera.setPixFormat(PIXFORMAT_RGB565);
  uint8_t*img = camera.snapshot();
  if(img == nullptr || img==0) {
    printf("snap fail\n");
    return;
  }
  lcd.drawImage(0, 0, camera.width(), camera.height(), (uint16_t*)img);

  // RGB565 -> RGB888
  for (int i = 0;  i < 320 * 240; i++) {
    uint16_t val = img[i * 2 + 1] * 256 + img[i * 2];
    pixels[i * 3    ] = val & 0xf800 >> 8;
    pixels[i * 3 + 1] = val & 0x7e0 >> 3;
    pixels[i * 3 + 2] = val & 0x1f << 3;
  }

  // jpeg
  pos = 0;
  TooJpeg::writeJpeg(myOutput, pixels, 320, 240, true);

  Serial.printf("jpeg size %d\n", pos);

  // HTTP response
//  client.setTimeout(180 * 1000);
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type: image/jpeg");
  client.print("Content-length: ");
  client.printf("%d", pos);
  client.println();
  client.println();

  // send jpeg
  // 80 bytes per write
  uint16_t rest = pos;
  for (int i = 0; i < pos; i += 80) {
    if (rest < 80) {
      client.write((uint8_t *)out + i, rest);
    } else {
      client.write((uint8_t *)out + i, 80);
      rest -= 80;
    }
  }
}

/*
 * HTML Response img
 */
void sendHttpResponseBmp(WiFiEspClient client) {
  // take snapshot 1 RGB565 & send to LCD
  camera.setPixFormat(PIXFORMAT_RGB565);
  uint8_t*img = camera.snapshot();
  if(img == nullptr || img==0) {
    printf("snap fail\n");
    return;
  }
  lcd.drawImage(0, 0, camera.width(), camera.height(), (uint16_t*)img);

  long len = 320 * 240 * 2 + 54 + 12;

  // HTTP response
  client.setTimeout(180 * 1000);
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type: image/bmp");
  client.print("Content-length: ");
  client.printf("%ld", len);
  client.println();
  client.println();

  int BitDepth = 16;
  int Width = 320;
  int Height = 240;
  double dBytesPerPixel = ( (double) BitDepth ) / 8.0;
  double dBytesPerRow = dBytesPerPixel * (Width+0.0);
  dBytesPerRow = ceil(dBytesPerRow);

  int BytePaddingPerRow = 4 - ( (int) (dBytesPerRow) )% 4;
  if( BytePaddingPerRow == 4 ) {
    BytePaddingPerRow = 0;
  }

  double dActualBytesPerRow = dBytesPerRow + BytePaddingPerRow;
  double dTotalPixelBytes = Height * dActualBytesPerRow;
  double dPaletteSize = 3 * 4;
  double dTotalFileSize = 14 + 40 + dPaletteSize + dTotalPixelBytes;

  // write the file header 

// typedef unsigned char  ebmpBYTE;
  typedef unsigned short ebmpWORD;
  typedef unsigned int  ebmpDWORD;

  ebmpWORD bfType = 19778; // BM
  ebmpDWORD bfSize = (ebmpDWORD) dTotalFileSize; 
  ebmpWORD bfReserved1 = 0; 
  ebmpWORD bfReserved2 = 0; 
  ebmpDWORD bfOffBits = (ebmpDWORD) (14+40+dPaletteSize);  
 
  client.write( (uint8_t*) &(bfType) , sizeof(ebmpWORD) );
  client.write( (uint8_t*) &(bfSize) , sizeof(ebmpDWORD) );
  client.write( (uint8_t*) &(bfReserved1) , sizeof(ebmpWORD) );
  client.write( (uint8_t*) &(bfReserved2) , sizeof(ebmpWORD) );
  client.write( (uint8_t*) &(bfOffBits) , sizeof(ebmpDWORD) );

  // write the info header 
 
  ebmpDWORD biSize = 40;
  ebmpDWORD biWidth = Width;
  ebmpDWORD biHeight = Height;
  ebmpWORD biPlanes = 1;
  ebmpWORD biBitCount = BitDepth;
  ebmpDWORD biCompression = 0;
  ebmpDWORD biSizeImage = (ebmpDWORD) dTotalPixelBytes;
  ebmpDWORD biXPelsPerMeter = 0;
  ebmpDWORD biYPelsPerMeter = 0;
  ebmpDWORD biClrUsed = 0;
  ebmpDWORD biClrImportant = 0;

  // indicates that we'll be using bit fields for 16-bit files
  if( BitDepth == 16 ) {
    biCompression = 3;
  }
 
  client.write( (uint8_t*) &(biSize) , sizeof(ebmpDWORD) );
  client.write( (uint8_t*) &(biWidth) , sizeof(ebmpDWORD) );
  client.write( (uint8_t*) &(biHeight) , sizeof(ebmpDWORD) );
  client.write( (uint8_t*) &(biPlanes) , sizeof(ebmpWORD) );
  client.write( (uint8_t*) &(biBitCount) , sizeof(ebmpWORD) );
  client.write( (uint8_t*) &(biCompression) , sizeof(ebmpDWORD) );
  client.write( (uint8_t*) &(biSizeImage) , sizeof(ebmpDWORD) );
  client.write( (uint8_t*) &(biXPelsPerMeter) , sizeof(ebmpDWORD) );
  client.write( (uint8_t*) &(biYPelsPerMeter) , sizeof(ebmpDWORD) ); 
  client.write( (uint8_t*) &(biClrUsed) , sizeof(ebmpDWORD) );
  client.write( (uint8_t*) &(biClrImportant) , sizeof(ebmpDWORD) );
 
  // write the bit masks

  ebmpWORD BlueMask = 31;    // bits 12-16
  ebmpWORD GreenMask = 2016; // bits 6-11
  ebmpWORD RedMask = 63488;  // bits 1-5
//  ebmpWORD GreenMask = 63488;  // bits 12-16
//  ebmpWORD BlueMask = 2016;    // bits 6-11
//  ebmpWORD RedMask = 31;       // bits 1-5
  ebmpWORD ZeroWORD = 0;
  
  client.write( (uint8_t*) &RedMask , sizeof(ebmpWORD) );
  client.write( (uint8_t*) &ZeroWORD , sizeof(ebmpWORD) );

  client.write( (uint8_t*) &GreenMask , sizeof(ebmpWORD) );
  client.write( (uint8_t*) &ZeroWORD , sizeof(ebmpWORD) );

  client.write( (uint8_t*) &BlueMask , sizeof(ebmpWORD) );
  client.write( (uint8_t*) &ZeroWORD , sizeof(ebmpWORD) );

  int DataBytes = Width*2;
  int PaddingBytes = ( 4 - DataBytes % 4 ) % 4;
  
  // write the actual pixels
  
  uint8_t b[320 * 2];

  // Send all the pixels on the whole screen
  for ( int y = Height; y >= 0; y--) {
    // Increment x by NPIXELS as we send NPIXELS for every byte received
    for ( uint32_t x = 0; x < Width * 2; x += 2) {
      b[x] = img[y * Width * 2 + x + 1];
      b[x + 1] = img[y * Width * 2 + x];
    }
    client.write( (uint8_t*) b , 640 );
  }
  
}

/*
 * WiFi status to serial
 */
void printWifiStatus() {
  // print the SSID of the network you're attached to
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your WiFi shield's IP address
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print where to go in the browser
  Serial.println();
  Serial.print("To see this page in action, open a browser to http://");
  Serial.println(ip);
  Serial.println();
}

/*
 * Setup
 */
void setup() {
  // init
  pll_init();
  uarths_init();

  lcd.begin(15000000, COLOR_BLACK);
  if(!camera.begin())
    printf("camera init fail\n");
  else
    printf("camera init success\n");
  camera.run(true);

//  // WiFi power control for NewMaixGo
//  fpioa_set_function(0, FUNC_GPIOHS1);
//  gpiohs_set_drive_mode(1, GPIO_DM_OUTPUT);
//  gpiohs_set_pin(1, GPIO_PV_LOW);

//  // ESP8265 WiFi reset for NewMaixGo
//  fpioa_set_function(8, FUNC_GPIOHS0);
//  gpiohs_set_drive_mode(0, GPIO_DM_OUTPUT);
//  gpiohs_set_pin(0, GPIO_PV_LOW);
//  sleep(1);
//  gpiohs_set_pin(0, GPIO_PV_HIGH);
//  sleep(3);

  Serial.begin(115200);   // initialize serial for debugging
//  Serial1.begin(115200);  // initialize serial for ESP WiFi module
//  WiFi.init(&Serial1);    // initialize ESP WiFi module
  WiFi.init();    // initialize ESP32 WiFi module

  // check for the presence of the shield
  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.println("WiFi shield not present");
    // don't continue
    while (true);
  }

  // attempt to connect to WiFi network
  while (status != WL_CONNECTED) {
    Serial.print("Attempting to connect to WPA SSID: ");
    Serial.println(SSID);
    // Connect to WPA/WPA2 network
    status = WiFi.begin(SSID, pass);
  }

  Serial.println("You're connected to the network");
  printWifiStatus();

  // display IP info
  IPAddress ip = WiFi.localIP();
  lcd.print("Show http://");
  lcd.print(ip);
  lcd.println("/");

  server.begin();

  mdns.begin(WiFi.localIP(), "maixduino");
  mdns.addServiceRecord("mDNS Webserver http",
                        80,
                        MDNSServiceTCP);
}

/*
 * Main loop
 */
void loop() {
  mdns.run();
  WiFiEspClient client = server.available();  // listen for incoming clients

  if (client) {                               // if you get a client,
    Serial.println("New client");             // print a message out the serial port
    buf.init();                               // initialize the circular buffer
    while (client.connected()) {              // loop while the client's connected
      if (client.available()) {               // if there's bytes to read from the client,
        char c = client.read();               // read a byte, then
        buf.push(c);                          // push it to the ring buffer

        // html
        if (buf.endsWith("\r\n\r\n")) {
          sendHttpResponse(client);
          break;
        }

        // jpeg
        if (buf.endsWith("/jpg")) {
          sendHttpResponseJpg(client);
          break;
        }

      }
    }
    
    // close the connection
    client.stop();
    Serial.println("Client disconnected");
  }
}
