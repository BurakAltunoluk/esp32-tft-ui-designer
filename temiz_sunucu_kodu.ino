#include "BluetoothSerial.h"
BluetoothSerial SerialBT;
int menuGonderildi = 0;


void setup() {
 Serial.begin(115200);
 delay(1000);
 SerialBT.begin("ESP32_SUNUCU");
}



void loop() {
    /* ---- CLIENT'tan gelenler ---- */
  if (SerialBT.available()) {
    String gelen = SerialBT.readStringUntil('\n');
    gelen.trim();
    Serial.println(gelen);
    if (gelen == "ds") {
      SerialBT.disconnect();
      menuGonderildi = 0;
      }

  }
 if (SerialBT.hasClient() && menuGonderildi == 0) {
    menuGonderildi = 1;
    drawMainUI();
  }
}


void drawMainUI() {
  SerialBT.println("rotation(0)");
  delay(150);

  // index 0 - Baslik
  SerialBT.println("clear(0xCD0E0E)");
  delay(80);
  SerialBT.println("button(35,170,160,48,0x7C5CFF,0xFFFFFF,disconnect,ds,1)");

}
