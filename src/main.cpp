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
#include <Sipeed_OV2640.h>
#include <Sipeed_ST7789.h>
#include <stdio.h>
#include <WiFiEsp.h>
#include <IPAddress.h>
#include <Print.h>
#include <SPI.h>

SPIClass spi_(SPI0); // MUST be SPI0 for Maix series on board LCD
Sipeed_ST7789 lcd(320, 240, spi_);

Sipeed_OV2640 camera(FRAMESIZE_QVGA, PIXFORMAT_RGB565);

char ssid[] = "xxxxxxxxxxxx";        // your network SSID (name)
char pass[] = "zzzzzzzzzzzz";        // your network password
int status = WL_IDLE_STATUS;

WiFiEspServer server(80);
// use a ring buffer to increase speed and reduce memory allocation
EspRingBuffer buf(64);

/*
 * HTML Response
 */
void sendHttpResponse(WiFiEspClient client) {
  // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
  // and a content-type so the client knows what's coming, then a blank line:
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html");
  client.println();

  client.println("<meta http-equiv=\"refresh\" content=\"30;\">");
  client.println("<html>");
  client.println("<head></head>");
  client.println("<body>");
  client.println("<img src=\"/jpg\"");
  client.println("</body>");
  client.println("</html>");
  
  // The HTTP response ends with another blank line:
  client.println();
}

/*
 * HTML Response img
 */
void sendHttpResponseJpg(WiFiEspClient client) {
  // take snapshot 1 RGB565 & send to LCD
  camera.setPixFormat(PIXFORMAT_RGB565);
  uint8_t*img = camera.snapshot();
  if(img == nullptr || img==0)
    printf("snap fail\n");
  else
    lcd.drawImage(0, 0, camera.width(), camera.height(), (uint16_t*)img);

  // clear buffer to find end of jpeg
  for (int i = 0; i < 320 * 240 * 2; i++) {
    img[i] = 0x0;
  }

  // take snapshot 2 JPEG
  camera.setPixFormat(PIXFORMAT_JPEG);
  img = camera.snapshot();

  // search end of jpeg
  long eof = 320 * 240 * 2 - 1; // max
  for (int i = 320 * 240 * 2 - 1; i > 0; i--) {
    if (img[i] != 0) {
      eof = i;
      break;
    }
  }
  Serial.printf("jpeg size %ld\n", eof);

  // HTTP response
  client.setTimeout(180 * 1000);
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type: image/jpeg");
  client.print("Content-length: ");
  client.printf("%ld", eof);
  client.println();
  client.println();

  // send jpeg
  // 80 bytes per write
  uint16_t rest = eof;
  for (int i = 0; i < eof / 2; i += 40) {
    if (rest < 80) {
      client.write((uint8_t *)img + i * 2, rest);
    } else {
      client.write((uint8_t *)img + i * 2, 80);
      rest -= 80;
    }
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

  // WiFi power control for NewMaixGo
  fpioa_set_function(0, FUNC_GPIOHS1);
  gpiohs_set_drive_mode(1, GPIO_DM_OUTPUT);
  gpiohs_set_pin(1, GPIO_PV_LOW);

  // ESP8265 WiFi reset for NewMaixGo
  fpioa_set_function(8, FUNC_GPIOHS0);
  gpiohs_set_drive_mode(0, GPIO_DM_OUTPUT);
  gpiohs_set_pin(0, GPIO_PV_LOW);
  sleep(1);
  gpiohs_set_pin(0, GPIO_PV_HIGH);
  sleep(3);

  Serial.begin(115200);   // initialize serial for debugging
  Serial1.begin(115200);  // initialize serial for ESP WiFi module
  WiFi.init(&Serial1);    // initialize ESP WiFi module

  // check for the presence of the shield
  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.println("WiFi shield not present");
    // don't continue
    while (true);
  }

  // attempt to connect to WiFi network
  while (status != WL_CONNECTED) {
    Serial.print("Attempting to connect to WPA SSID: ");
    Serial.println(ssid);
    // Connect to WPA/WPA2 network
    status = WiFi.begin(ssid, pass);
  }

  Serial.println("You're connected to the network");
  printWifiStatus();

  // display IP info
  IPAddress ip = WiFi.localIP();
  lcd.print("Show http://");
  lcd.print(ip);
  lcd.println("/");

  server.begin();
}

/*
 * Main loop
 */
void loop() {
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
