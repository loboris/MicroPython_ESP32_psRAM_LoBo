#include <Arduino.h>
#include <U8g2lib.h>
#include <OneWire.h> 
#include <DallasTemperature.h>

// Teensy
U8G2_KS0108_128X64_F u8g2(U8G2_R2, 10, 9, 8, 7, 6, 5, 4, 3, /*enable=*/ 11, /*dc=*/ 14, /*cs0=*/ 2, /*cs1=*/ 15, /*cs2=*/ U8X8_PIN_NONE, /* reset=*/  U8X8_PIN_NONE);   // Set R/W to low!
const int rw = 12;
const int reset_pin = 16;
OneWire oneWire(17);
// 20 y 21 son "llave de restart"
const int restart1 = 20;
const int restart2 = 21;
const int led = 13;
const int relay1 = 22;
const int relay2 = 23;
int rpm = 0;
DallasTemperature sensors(&oneWire);
DeviceAddress address_motor = { 0x28, 0x11, 0x39, 0x44, 0x07, 0x00, 0x00, 0x91 };
DeviceAddress address_variador = { 0x28, 0xF2, 0x7E, 0x1F, 0x06, 0x00, 0x00, 0xBE };
String status_msg = F("STOPPED: just booted up");
String inputString = "";

#define monologo_width 36
#define monologo_height 30
static unsigned char monologo_bits[] = {
   0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x1f,
   0x00, 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x1e, 0x10, 0x00,
   0x00, 0x00, 0x1e, 0xf0, 0x00, 0x00, 0x00, 0x1e, 0xf0, 0x00, 0x00, 0x00,
   0x3e, 0xf0, 0x00, 0x00, 0x00, 0x3e, 0xf0, 0x00, 0x00, 0x00, 0x3c, 0xf8,
   0x00, 0x00, 0x00, 0x3c, 0xf8, 0x00, 0x00, 0x00, 0x3c, 0x78, 0x00, 0x00,
   0x00, 0x3c, 0x78, 0x00, 0x00, 0x00, 0x3c, 0x78, 0x00, 0x00, 0x00, 0x7c,
   0x7c, 0x00, 0x00, 0x00, 0x78, 0x7c, 0x00, 0x00, 0x00, 0x78, 0x3c, 0x00,
   0x00, 0x00, 0xf8, 0x3e, 0x00, 0x00, 0x00, 0xf8, 0x3d, 0x00, 0x00, 0x00,
   0x7d, 0x3f, 0x2c, 0x00, 0xa0, 0x7e, 0xbe, 0xdb, 0x02, 0xec, 0xfa, 0xbe,
   0x8a, 0x0a, 0x5a, 0xf1, 0xbf, 0xc1, 0x0d, 0x2a, 0xf0, 0x3e, 0x50, 0x0b,
   0x37, 0xfa, 0x1f, 0x6a, 0x05, 0x54, 0xfb, 0xcf, 0xde, 0x00, 0xb0, 0xf6,
   0x5f, 0x15, 0x00, 0x00, 0xe0, 0xdf, 0x05, 0x00, 0x00, 0x80, 0xbf, 0x00,
   0x00, 0x00, 0x00, 0x14, 0x00, 0x00 };

void setup() {
  pinMode(relay1, OUTPUT);
  pinMode(relay2, OUTPUT);
  digitalWrite(relay1, HIGH);
  digitalWrite(relay2, HIGH);
  pinMode(restart1, INPUT_PULLUP);
  pinMode(restart2, INPUT_PULLUP);

  pinMode(led, OUTPUT);
  pinMode(rw, OUTPUT);
  digitalWrite(rw, LOW);
  pinMode(reset_pin, OUTPUT);
  digitalWrite(reset_pin, HIGH);
  Serial.begin(115200);
  Serial1.begin(115200);
  u8g2.begin();
  sensors.begin(); 
}

int restart_phase = 0;

void draw() {
  sensors.requestTemperatures();
  
  u8g2.setFont(u8g2_font_tenfatguys_tf);

  u8g2.setDrawColor(1);
  u8g2.drawBox(0, 0, 128, 14);

  u8g2.setDrawColor(0);
  u8g2.setCursor(0, 12);
  u8g2.print( F("Ventilastation"));
  
  u8g2.setDrawColor(1);

  float motor = sensors.getTempC(address_motor);
  if (motor != DEVICE_DISCONNECTED_C) {
    u8g2.setCursor(28, 46);
    u8g2.print(motor, 1);
    u8g2.println( F("\xb0" "C"));
    Serial1.print("temperatura motor=");
    Serial1.println(motor);
  }
  
  u8g2.setCursor(4, 31);
  u8g2.print(rpm);
  u8g2.print( F(" RPM"));
  
  u8g2.setFont(u8g2_font_tom_thumb_4x6_tf);
  u8g2.setCursor( 96, 31);
  u8g2.print(rpm*2/60.0, 1);
  u8g2.print(" fps");

  u8g2.setCursor(0, 46);
  u8g2.print( F("Motor: "));

  float variador = sensors.getTempC(address_variador);
  if (variador != DEVICE_DISCONNECTED_C) {
    u8g2.setCursor(0, 55);
    u8g2.print( F("Variador: "));
    u8g2.print( variador, 1);
    u8g2.println( F("\xb0" "C"));
    Serial1.print("temperatura variador=");
    Serial1.println(variador);
  }

  u8g2.drawXBM( 90, 34, monologo_width, monologo_height, monologo_bits);
  
  u8g2.setCursor(0, 63);
  u8g2.println(status_msg.c_str() );
}

unsigned long last_update = 0;

void loop() {

  unsigned long now = millis();

  if (now - last_update > 1000) {
    digitalWrite(led, HIGH);
    u8g2.clearBuffer();
    draw();
    u8g2.sendBuffer();
    digitalWrite(led, LOW);
    last_update = now;
  }

  bool restart_phase1 = digitalRead(restart1) == LOW;
  bool restart_phase2 = digitalRead(restart2) == LOW;

  if (restart_phase1 && restart_phase2) {
    restart_phase = 0;
    // big error here
  } else {
  
    if (restart_phase1 && restart_phase != 1) {
      restart_phase = 1;
      Serial.println("restart phase 1");
    }
  
    if (restart_phase2 && restart_phase == 1) {
      restart_phase = 2;
      Serial.println("restart phase 2... go!");
      Serial1.println("start_fan ");
    }
  }

  if (restart_phase == 2) {
    digitalWrite(relay1, LOW);
    digitalWrite(relay2, LOW);
  } else {
    digitalWrite(relay1, HIGH);
    digitalWrite(relay2, HIGH);
  }

  while (Serial1.available()) {
    char inChar = (char)Serial1.read();
    Serial.print(inChar);
    if (inChar == '\n') {
      int space = inputString.indexOf(" ");
      String topic = inputString.substring(0, space);
      String payload = inputString.substring(space + 1);
      if (topic.equals("fan_speed")) {
        if(payload.startsWith("rpm")) {
          int equal_idx = payload.indexOf("=");
          rpm = payload.substring(equal_idx + 1).toInt();
        }
      } else if (topic.equals("start_fan")) {
        status_msg = F("Status: todo perfecto");
      } else if (topic.equals("stop_fan")) {
        digitalWrite(relay1, HIGH);
        digitalWrite(relay2, HIGH);
        status_msg = inputString.substring(space + 1);
        restart_phase = 0;
      }
      inputString = "";
    } else {
      inputString += inChar;
    }
  }
  
}
