// firmware for ulnoiot ESP dongle (UED)
//
// author: ulno
// created: 2019-02-22

#define BUFFER_LEN 1460
#define DISPLAY_BUFFER_LEN 128
#define SERVER_PORT 28266

#define FLASH 0
#define SPIFFS 100
#define AUTH 200

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

#include <functional>

// used external hardware
#include <Wire.h>
#include <U8g2lib.h>

// config defaults
#include "ulnoiot-default.h"

// simple command line TODO: would it be better to use bitlash or hobbitlash?
#include <SimpleCLI.h>
using namespace simplecli;
// create an instance of it
SimpleCLI* cli;

char buffer[BUFFER_LEN];
int buffer_fill = 0;

char display_string[DISPLAY_BUFFER_LEN];


// small display shield for Wemos
U8G2_SSD1306_64X48_ER_F_HW_I2C u8g2(U8G2_R0); // R0 no rotation, R1 - 90°

// The following is taken from: http://www.forward.com.au/pfod/ArduinoProgramming/I2C_ClearBus/index.html
// Matthew Ford 1st August 2017 (original 28th September 2014)
// (c) Forward Computing and Control Pty. Ltd. NSW Australia
// https://github.com/esp8266/Arduino/issues/1025#issuecomment-319199959
int i2c_clear_bus() {
#if defined(TWCR) && defined(TWEN)
    TWCR &= ~(_BV(TWEN)); //Disable the Atmel 2-Wire interface so we can control the sda_pin and scl_pin pins directly
#endif

    const uint8_t sda_pin = SDA;
    const uint8_t scl_pin = SCL;

    pinMode(sda_pin, INPUT_PULLUP); // Make sda_pin (data) and scl_pin (clock) pins Inputs with pullup.
    pinMode(scl_pin, INPUT_PULLUP);

    delay(250); // assert master

    boolean SCL_LOW = (digitalRead(scl_pin) == LOW); // Check is scl_pin is Low.
    if (SCL_LOW) { //If it is held low Arduino cannot become the I2C master. 
        return 1; //I2C bus error. Could not clear scl_pin clock line held low
    }

    boolean SDA_LOW = (digitalRead(sda_pin) == LOW);  // vi. Check sda_pin input.
    int clockCount = 20; // > 2x9 clock

    while (SDA_LOW && (clockCount > 0)) { //  vii. If sda_pin is Low,
        clockCount--;
        // Note: I2C bus is open collector so do NOT drive scl_pin or sda_pin high.
        pinMode(scl_pin, INPUT); // release scl_pin pullup so that when made output it will be LOW
        pinMode(scl_pin, OUTPUT); // then clock scl_pin Low
        delayMicroseconds(10); //  for >5uS
        pinMode(scl_pin, INPUT); // release scl_pin LOW
        pinMode(scl_pin, INPUT_PULLUP); // turn on pullup resistors again
        // do not force high as slave may be holding it low for clock stretching.
        delayMicroseconds(10); //  for >5uS
        // The >5uS is so that even the slowest I2C devices are handled.
        SCL_LOW = (digitalRead(scl_pin) == LOW); // Check if scl_pin is Low.
        int counter = 20;
        while (SCL_LOW && (counter > 0)) {  //  loop waiting for scl_pin to become High only wait 2sec.
            counter--;
            delay(100);
            SCL_LOW = (digitalRead(scl_pin) == LOW);
        }
        if (SCL_LOW) { // still low after 2 sec error
            return 2; // I2C bus error. Could not clear. scl_pin clock line held low by slave clock stretch for >2sec
        }
        SDA_LOW = (digitalRead(sda_pin) == LOW); //   and check sda_pin input again and loop
    }
    if (SDA_LOW) { // still low
        return 3; // I2C bus error. Could not clear. sda_pin data line held low
    }

    // else pull sda_pin line low for Start or Repeated Start
    pinMode(sda_pin, INPUT); // remove pullup.
    pinMode(sda_pin, OUTPUT);  // and then make it LOW i.e. send an I2C Start or Repeated start control.
    // When there is only one I2C master a Start or Repeat Start has the same function as a Stop and clears the bus.
    /// A Repeat Start is a Start occurring after a Start with no intervening Stop.
    delayMicroseconds(10); // wait >5uS
    pinMode(sda_pin, INPUT); // remove output low
    pinMode(sda_pin, INPUT_PULLUP); // and make sda_pin high i.e. send I2C STOP control.
    delayMicroseconds(10); // x. wait >5uS
    pinMode(sda_pin, INPUT); // and reset pins as tri-state inputs which is the default state on reset
    pinMode(scl_pin, INPUT);
    return 0; // all ok
}

std::function<void()> show_func = NULL;

void show_init() {
    u8g2.setFont(u8g2_font_7x14B_tr);
    u8g2.drawStr(7, 18, "UlnoIoT");
    u8g2.drawStr(10, 42, "Dongle");
}

void show() {
    u8g2.firstPage();
    do {
        if(show_func) show_func();
    } while ( u8g2.nextPage() );
}

void prompt() {
    Serial.print("UED>"); // ulnoiot esp dongle
}

bool ota_serve(int firmware_size, const char* firmware_md5) {

    // TODO: change quickly to something web-based and at least sha256 auth

    // based on espota of ESP8266 for Arduino
    WiFiServer wifi_server(SERVER_PORT);
    WiFiUDP invite_client;
    const int send_buffer_len = 128;
    char send_buffer[send_buffer_len];

    // Start local server listening for image request
    wifi_server.begin();
    Serial.print("Listening for answers on UDP port: ");
    Serial.println(SERVER_PORT+1);
    invite_client.begin(SERVER_PORT+1);

    // Sending invitation
    Serial.println("Sending invitation for firmware OTA to node.");
    snprintf(send_buffer, 128, "%d %d %d %s\n", 
        FLASH, SERVER_PORT, firmware_size, firmware_md5);
    Serial.print("Sending as invite: ");
    Serial.println(send_buffer);
    const IPAddress invite_client_ip(192,168,4,1);
    // debug const IPAddress invite_client_ip(192,168,12,49);
    invite_client.beginPacket(invite_client_ip, 8266);
    invite_client.write(send_buffer, strnlen(send_buffer, send_buffer_len));
    if(!invite_client.endPacket()) {
        Serial.println("!error sending invite package");
    }
    uint16_t local_port =  invite_client.localPort();
    Serial.print("Local port:");
    Serial.println(local_port);
    Serial.println("Waiting for answer from node.");
    int buf_size = 0;
    for(int i=0; i<1000; i++) { // wait for up to 10s
        buf_size = invite_client.parsePacket();
        if(buf_size>0) break;
        delay(10);
    }
    if(buf_size == 0) {
        Serial.println("!error no answer to invite");
        invite_client.stop();
        wifi_server.stop();
        return false;
    }
    invite_client.read(buffer, buf_size);
    if (buf_size >= BUFFER_LEN) buf_size = BUFFER_LEN-1;
    buffer[buf_size] = 0; // 0 terminate

    Serial.print("answer: ");
    Serial.println(buffer);
    if(strcmp(buffer,"OK")) { // answer is not OK
        if(memcmp(buffer, "AUTH ", 5)) { // does not start with AUTH (or OK)
            Serial.println("!error wrong answer to invite");
            invite_client.stop();
            wifi_server.stop();
            return false;
        }

        char *nonce = buffer + 5; // hexdigest nonce comes directly after AUTH
        
        // compute cnonce
        char cnonce_text[128];
        snprintf(cnonce_text, 128, "firmware.bin%u%s192.168.4.1", firmware_size, firmware_md5);
        // Serial.print("cnonce text: ");
        // Serial.println(cnonce_text);
        char cnonce[33];
        MD5Builder cnonce_md5; 
        cnonce_md5.begin();
        cnonce_md5.add(cnonce_text); 
        cnonce_md5.calculate(); 
        cnonce_md5.getChars(cnonce);
        
        // compute passmd5
        char passmd5[33];
        MD5Builder passmd5_md5;
        passmd5_md5.begin();
        passmd5_md5.add(ULNOIOT_FLASH_DEFAULT_PASSWORD); 
        passmd5_md5.calculate(); 
        passmd5_md5.getChars(passmd5);
        // Serial.print("passmd5: ");
        // Serial.println(passmd5);
        
        // compute result
        char result_text[128];
        snprintf(result_text, send_buffer_len, "%s:%s:%s", passmd5, nonce, cnonce );
        // Serial.print("result text: ");
        // Serial.println(result_text);

        char result[33];
        MD5Builder result_md5;
        result_md5.begin();
        result_md5.add(result_text); 
        result_md5.calculate(); 
        result_md5.getChars(result);

        // Do authentication
        snprintf(send_buffer, send_buffer_len, "%d %s %s\n", AUTH, cnonce, result);
        // Serial.print("Sending: ");
        // Serial.println(send_buffer);
        invite_client.beginPacket(invite_client_ip, 8266);
        invite_client.write(send_buffer, strnlen(send_buffer, send_buffer_len));
        if(!invite_client.endPacket()) {
            Serial.println("!error sending auth package");
            invite_client.stop();
            wifi_server.stop();
            return false;
        }

        // wait for AUTH answer
        for(int i=0; i<1000; i++) { // wait for up to 10s
            buf_size = invite_client.parsePacket();
            if(buf_size>0) break;
            delay(10);
        }
        if(buf_size == 0) {
            Serial.println("!error no answer to auth answer");
            invite_client.stop();
            wifi_server.stop();
            return false;
        }
        invite_client.read(buffer, buf_size);
        if (buf_size >= BUFFER_LEN) buf_size = BUFFER_LEN-1;
        buffer[buf_size] = 0; // 0 terminate
        Serial.print("Auth answer: ");
        Serial.println(buffer);

        if(strcmp(buffer,"OK")) { // answer is not OK
            Serial.println("!error receiving positive auth confirmation");
            invite_client.stop();
            wifi_server.stop();
            return false;
        }
    }

    // Finally done with UDP
    invite_client.stop();

    // answer is OK or we have successfully authenticated
    Serial.println("Waiting 10s for node to open connection.");

    WiFiClient client;
    // wait for node connection
    for(int i=0; i<1000; i++) { // wait for up to 10s
        client = wifi_server.available();
        if(client) break;
        delay(10);
    }
    if(!client) {
        Serial.println("!error no response from node");
        wifi_server.stop();
        return false;
    }
    
    Serial.println("!upload start");

    buffer_fill = 0;
    int bytes_sent = 0;
    int bytes_read = 0;
    char next_char;
    bool something_received;
    bool ok_found = false;

    while(bytes_sent < firmware_size) {
        if(bytes_read < firmware_size) { // if not read all, read more
            if(buffer_fill < BUFFER_LEN) { // but only if still space in buffer
                if(Serial.available() > 0) {
                    next_char = Serial.read();
                    if(next_char >=0) {
                        buffer[buffer_fill] = (unsigned char)next_char;
                        buffer_fill ++;
                        bytes_read ++;
                    }
                }
            }
        }
        // time to send it out
        if(buffer_fill == BUFFER_LEN || bytes_read == firmware_size) {
            // trigger send
            if(!client.write(buffer, buffer_fill)) {
                Serial.println("!error problems sending firmware data");
                wifi_server.stop();
                return false;
            }
            bytes_sent += buffer_fill;
            // try to receive something for max 10s
            something_received = false;
            for(int i=0; i<1000; i++) {
                int available = client.available();
                if(available>=0) {
                    something_received = true;
                    client.read((uint8_t*)buffer, available);
                    for(int j=0; j<available; j++) {
                        if(buffer[j] == 'O') {
                            ok_found = true;
                            break;
                        } 
                    }
                    break;
                }
                delay(10);
            }
            if(something_received) {
                Serial.println("!send more");
                Serial.flush();
            } else {
                Serial.println("!error no confirmation data");
                Serial.flush();
                wifi_server.stop();
                return false;
            }
            buffer_fill = 0;
        }
    } // end (bytes_sent < firmware_size)
    something_received = false;
    if(!ok_found) { // try for 15s longer to get the final ok
        for(int i=0; i<1500; i++) {
            int available = client.available();
            if(available>0) {
                something_received = true;
                client.read((uint8_t*)buffer, available);
                for(int j=0; j<available; j++) {
                    if(buffer[j] == 'O') {
                        ok_found = true;
                        break;
                    } 
                }
                if(ok_found) break;
            }
            delay(10);
        }
    }
    if(!ok_found) {
        Serial.printf("!error no final confirmation: received %d", something_received);
        Serial.flush();
        wifi_server.stop();
        return false;
    } else {
        Serial.println("!success uploaded successfully");
        Serial.flush();
    }
    wifi_server.stop();
    return true; // success
}

void adopt(Cmd* cmd) {
    const int ssid_maxlen = 32;
    char node_network[ssid_maxlen+1];
    char md5sum[33];
    md5sum[32]=0;
    const char *node_ap_password = ULNOIOT_AP_RECONFIG_PASSWORD;
// debug   const char *node_ap_password = "mypassword";

    node_network[ssid_maxlen] = 0 ; // terminate
    cmd->getArg(0)->getValue().toCharArray(node_network, ssid_maxlen);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    Serial.printf("Connecting to reconfig node network %s.", node_network);
    WiFi.begin(node_network, node_ap_password);

    // Try for 15 seconds
    for(int i=0; i<15; i++) {
        delay(1000);
        if(WiFi.status() == WL_CONNECTED) break;
        Serial.print(".");
    }
    if(WiFi.status() != WL_CONNECTED) {
        Serial.println("failure.");
        Serial.println("!error adopt connect");
        return;
    };
    Serial.println("OK.");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    // when successfully connected start OTA handshake
    cmd->getArg(2)->getValue().toCharArray(md5sum, 33);
    ota_serve(cmd->getArg(1)->getValue().toInt(), md5sum);
};

void setup_cli() {
    cli = new SimpleCLI();

    // when no valid command could be found for given user input
    cli->onNotFound = [](String str) {
                          Serial.println("\"" + str + "\" command not found, try help.");
                      };


    //// help
    cli->addCmd(new Command("help", [](Cmd* cmd) {
        Serial.println(
            "You can use the follwing commands:\n"
            "- help: shows this help\n"
            "- display TEXT: show TEXT on the display\n"
            "- scan: scan wifi networks\n"
            "- adopt NODE SIZE MD5: adopt NODE and send initial firmware,\n"
            "    SIZE and MD5 are part of following espota protocal\n"
            "- reset: resets the dongle\n");
    }));

    //// help
    cli->addCmd(new Command("reset", [](Cmd* cmd) {
        Serial.println("Reset in 2s.\n");
        delay(2000);
        ESP.restart();
    }));

    //// display
    Command* display_cmd = new Command("display", [](Cmd* cmd) {
        Serial.print("Display got: ");
        cmd->getArg(0)->getValue().toCharArray(display_string, DISPLAY_BUFFER_LEN);
        Serial.println(display_string);
        show_func = [cmd]{u8g2.drawStr(0,16,display_string);};
    });
    display_cmd->addArg(new AnonymOptArg()); // text
    cli->addCmd(display_cmd);

    //// scan
    cli->addCmd(new Command("scan", [](Cmd* cmd) {
        // Set WiFi to station mode and disconnect from an AP if it was previously connected
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();

        Serial.println("!network_scan start");
        int count = WiFi.scanNetworks();
        for (int i = 0; i < count; ++i) {
            Serial.print(WiFi.RSSI(i));
            Serial.print(" ");
            Serial.println(WiFi.SSID(i));
        }
        Serial.println("!network_scan end");
    }));

    //// adopt
    Command* adopt_cmd = new Command("adopt", adopt);
    adopt_cmd->addArg(new AnonymOptArg()); // node
    adopt_cmd->addArg(new AnonymOptArg()); // size
    adopt_cmd->addArg(new AnonymOptArg()); // md5
    cli->addCmd(adopt_cmd);

    // // TODO: remove =========== Add ping command =========== //
    // // ping                 => pong
    // // ping -s ponk         => ponk
    // // ping -s ponk -n 2    => ponkponk
    // Command* ping = new Command("ping", [](Cmd* cmd) {
    //     int h = cmd->getValue("n").toInt();

    //     for (int i = 0; i < h; i++) {
    //         Serial.print(cmd->getValue("s"));
    //     }
    // });
    // ping->addArg(new OptArg("s", "ping!"));
    // ping->addArg(new OptArg("n", "1"));
    // cli->addCmd(ping);
    // // ======================================== //


    prompt();
}

void setup() {
//    Serial.begin(921600);  // let's be "fast", should work on a Wemos D1 mini
    Serial.begin(2000000);  // let's be "fast", should work on a Wemos D1 mini

    i2c_clear_bus();
    u8g2.begin();

    // flush some garbage
    for( int i=0; i<20; i++) {
      Serial.print(".");
      delay(50);
    }
    Serial.println();
    Serial.println();
    // print init message
    Serial.println("Starting UlnoIoT Dongle.");
    Serial.println();
    show_func = show_init;
    show();
    // initially no WiFi switched on, TODO: later be in ESP-Now mode and initialize LORA
    WiFi.softAPdisconnect (true);
    WiFi.disconnect(true);

    setup_cli();
}

void loop() {
    static unsigned long last_time = millis();
    unsigned long current_time = millis();
    int next_char;

    if(Serial.available() > 0) {
        next_char= Serial.read();
        if(next_char > 0) {
            Serial.write(next_char);
            Serial.flush();
            buffer[buffer_fill] = (unsigned char)next_char;
            if(next_char == '\n' || buffer_fill == BUFFER_LEN - 1) {
                if(next_char != '\n') {
                    buffer_fill ++;
                    Serial.println();
                }
                buffer[buffer_fill] = 0;
                cli->parse(buffer);
                buffer_fill = 0;
                prompt();
            } else {
                buffer_fill ++;
            }
        }
    }
    if(current_time - last_time >= 50) {
        last_time = current_time;
        show();
    }
    yield();
}