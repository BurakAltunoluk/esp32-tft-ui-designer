/* 
ESP32 TFT UI Client
- Bluetooth üzerinden komut alır
- Dokunmatik ekran ile butonlara basılabilir
- Gelen komutları seri monitöre yazdırır
*/

#include "BluetoothSerial.h"
#include <Arduino_GFX_Library.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>

#include "Fonts/FreeSans9pt7b.h"
#include "Fonts/FreeSans12pt7b.h"
#include "Fonts/FreeSans18pt7b.h"
#include "Fonts/FreeSansBold9pt7b.h"
#include "Fonts/FreeMonoOblique24pt7b.h"
#include "Fonts/FreeSans24pt7b.h"
#include "Fonts/FreeSansOblique12pt7b.h"
#include "Fonts/FreeSerifBold24pt7b.h"
#include "Fonts/TomThumb.h"
#include "Fonts/Tiny3x3a2pt7b.h"
#include "Fonts/Picopixel.h"
#include "Fonts/Org_01.h"

/* ===================== BLUETOOTH ===================== */
BluetoothSerial SerialBT;
// Bağlanmak istediğin cihazın MAC adresi:
uint8_t serverAddress[6] = {0x00, 0x4B, 0x12, 0xEE, 0x4E, 0x12};

/* ===================== TFT ===================== */
#define TFT_CS   15
#define TFT_DC   2
#define TFT_SCK  14
#define TFT_MOSI 13
#define TFT_MISO 12
#define TFT_RST  -1
#define TFT_BL   21
#define BACKLIGHT_ON() digitalWrite(TFT_BL, HIGH)

/* ===================== TOUCH ===================== */
#define TOUCH_CS   33
#define TOUCH_IRQ  36
#define TOUCH_SCK  25
#define TOUCH_MOSI 32
#define TOUCH_MISO 39

Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, TFT_MISO);
Arduino_GFX *gfx = new Arduino_ILI9341(bus, TFT_RST, 1, false);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

/* ===================== FONT SETİ ===================== */
const GFXfont* FONTS[] = {
  &FreeSans9pt7b,
  &FreeSans12pt7b,
  &FreeSans18pt7b,
  &FreeSansBold9pt7b,
  &FreeMonoOblique24pt7b,
  &FreeSans24pt7b,
  &FreeSansOblique12pt7b,
  &FreeSerifBold24pt7b,
  &TomThumb,
  &Tiny3x3a2pt7b,
  &Picopixel,
  &Org_01
};

/* ===================== UI ===================== */
enum ElementType { LABEL, BUTTON };

struct UIElement {
  ElementType type;
  int x, y, w, h;
  uint16_t bg, tc;
  String text, tx;
  int fontID;
};

#define MAX_ELEMENTS 50
UIElement elements[MAX_ELEMENTS];
int elementCount = 0;

/* ===================== HEX → RGB565 ===================== */
uint16_t hexTo565(String hex) {
  hex.replace("#", "");
  if (hex.startsWith("0x")) hex = hex.substring(2);
  long rgb = strtol(hex.c_str(), NULL, 16);
  uint8_t r = (rgb >> 16) & 0xFF;
  uint8_t g = (rgb >> 8) & 0xFF;
  uint8_t b = rgb & 0xFF;
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

/* ===================== PARSE ===================== */
String getArg(String &s, int index) {
  int from = 0;
  for (int i = 0; i < index; i++) {
    from = s.indexOf(',', from) + 1;
    if (from <= 0) return "";
  }
  int to = s.indexOf(',', from);
  if (to == -1) to = s.length();
  return s.substring(from, to);
}

/* ===================== ÇİZİM ===================== */
void drawTextBox(UIElement &e, bool rounded) {
  gfx->setFont(FONTS[e.fontID]);
  gfx->setTextColor(e.tc);

  if (rounded) {
    gfx->fillRoundRect(e.x, e.y, e.w, e.h, 12, e.bg);
    gfx->drawRoundRect(e.x, e.y, e.w, e.h, 12, e.tc);
  } else {
    gfx->fillRect(e.x, e.y, e.w, e.h, e.bg);
  }

  int16_t x1, y1;
  uint16_t tw, th;
  gfx->getTextBounds(e.text, 0, 0, &x1, &y1, &tw, &th);
  gfx->setCursor(e.x + (e.w - tw) / 2, e.y + (e.h + th) / 2 - 2);
  gfx->print(e.text);
}

void redrawAll() {
  for (int i = 0; i < elementCount; i++) {
    drawTextBox(elements[i], elements[i].type == BUTTON);
  }
}

void drawLineCmd(int x0, int y0, int x1, int y1, uint16_t c, int t) {
  for (int i = 0; i < max(1, t); i++) {
    gfx->drawLine(x0, y0 + i, x1, y1 + i, c);
  }
}

/* ===================== EKLE ===================== */
void addLabel(int x, int y, int w, int h, String bg, String tc, String text, int fontID) {
  if (elementCount >= MAX_ELEMENTS) return;
  elements[elementCount++] = { LABEL, x, y, w, h, hexTo565(bg), hexTo565(tc), text, "", fontID };

 
  elements[elementCount - 1].text.reserve(48);  // label yazısı için
  elements[elementCount - 1].tx.reserve(16);    // (labelda boş ama sorun değil)

  
  drawTextBox(elements[elementCount - 1], false);
}

void addButton(int x, int y, int w, int h, String bg, String tc, String text, String tx, int fontID) {
  if (elementCount >= MAX_ELEMENTS) return;
  elements[elementCount++] = { BUTTON, x, y, w, h, hexTo565(bg), hexTo565(tc), text, tx, fontID };

  elements[elementCount - 1].text.reserve(32);  // buton yazısı
  elements[elementCount - 1].tx.reserve(16);    // BT komutu
  
  drawTextBox(elements[elementCount - 1], true);
}

void setElementText(int idx, const String &newText) {
  if (idx < 0 || idx >= elementCount) return;

  UIElement &e = elements[idx];
  // Sadece LABEL (istersen BUTTON da ekleyebilirsin)
  if (e.type != LABEL) return;

  // Metni güncelle
  e.text = newText;

  // SADECE o elemanı yeniden çiz (diğerlerine dokunmaz)
  drawTextBox(e, false);
}

void setButtonText(int idx, const String &newText) {
  if (idx < 0 || idx >= elementCount) return;

  UIElement &e = elements[idx];
  if (e.type != BUTTON) return;

  e.text = newText;
  drawTextBox(e, true);   // rounded = true (button)
}



/* ===================== BAĞLANMA ===================== */
void connectDevice1() {
  Serial.println("Device1: connecting...");

  if (SerialBT.connected()) {
    Serial.println("Zaten bagli, disconnect...");
    SerialBT.disconnect();
    delay(200);
  }

  bool ok = SerialBT.connect(serverAddress);
  Serial.println(ok ? "Baglanti basarili!" : "Baglanti basarisiz!");
}

/* ===================== KOMUT ===================== */
void processCommand(String cmd) {
  cmd.trim();
  if (!cmd.length()) return;

  Serial.println("Processing: " + cmd);

  if (cmd.startsWith("rotation(")) {
    int r = cmd.substring(9, cmd.indexOf(')')).toInt();
    gfx->setRotation(r);
    ts.setRotation(r);
    gfx->fillScreen(0x0000);
    redrawAll();
  }
  else if (cmd.startsWith("clear(")) {
    gfx->fillScreen(hexTo565(cmd.substring(6, cmd.indexOf(')'))));
    elementCount = 0;
  }
  else if (cmd.startsWith("line(")) {
    String p = cmd.substring(5, cmd.indexOf(')'));
    drawLineCmd(
      getArg(p, 0).toInt(), getArg(p, 1).toInt(),
      getArg(p, 2).toInt(), getArg(p, 3).toInt(),
      hexTo565(getArg(p, 4)), getArg(p, 5).toInt()
    );
  }
  else if (cmd.startsWith("label(")) {
    String p = cmd.substring(6, cmd.indexOf(')'));
    addLabel(
      getArg(p, 0).toInt(), getArg(p, 1).toInt(),
      getArg(p, 2).toInt(), getArg(p, 3).toInt(),
      getArg(p, 4), getArg(p, 5),
      getArg(p, 6), getArg(p, 7).toInt()
    );
  }
  else if (cmd.startsWith("button(")) {
    String p = cmd.substring(7, cmd.indexOf(')'));
    addButton(
      getArg(p, 0).toInt(), getArg(p, 1).toInt(),
      getArg(p, 2).toInt(), getArg(p, 3).toInt(),
      getArg(p, 4), getArg(p, 5),
      getArg(p, 6), getArg(p, 7),
      getArg(p, 8).toInt()
    );
  }
  // İstersen Serial Monitor’dan "001" yazınca da bağlansın:
  else if (cmd == "001") {
    connectDevice1();
  }

else if (cmd.startsWith("set(")) {
  int close = cmd.lastIndexOf(')');
  if (close < 0) return;

  String p = cmd.substring(4, close);  // "idx,text..."
  int comma = p.indexOf(',');
  if (comma < 0) return;

  int idx = p.substring(0, comma).toInt();
  String text = p.substring(comma + 1); // geri kalan her şey metin

  setElementText(idx, text);
}

else if (cmd.startsWith("setbtn(")) {
  int close = cmd.lastIndexOf(')');
  if (close < 0) return;

  String p = cmd.substring(7, close);     // "idx,text..."
  int comma = p.indexOf(',');
  if (comma < 0) return;

  int idx = p.substring(0, comma).toInt();
  String text = p.substring(comma + 1);

  setButtonText(idx, text);
}


  
}

/* ===================== KURULUM EKRANI ===================== */
void kurulum() {
  delay(10);
  processCommand("rotation(0)");
  delay(10);
  processCommand("clear(0xF90B0B)");
  delay(10);

  processCommand("button(12,41,100,100,0x623FEE,0xFFFFFF,Device1,001,0)");
  delay(10);
  processCommand("button(126,42,100,100,0x623FEE,0xFFFFFF,Device2,002,0)");
  delay(10);
  processCommand("button(12,177,100,100,0x623FEE,0xFFFFFF,Device3,003,0)");
  delay(10);
  processCommand("button(125,177,100,100,0x623FEE,0xFFFFFF,Device4,004,0)");
  delay(10);

  processCommand("label(-5,297,246,25,0xC19A9A,0xFFFFFF,Burachline 2.1v,3)");
}

/* ===================== SETUP ===================== */
void setup() {
  Serial.begin(115200);

  pinMode(TFT_BL, OUTPUT);
  BACKLIGHT_ON();

  gfx->begin();

  SPI.begin(TOUCH_SCK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  ts.begin();

  // true => master/client mode
  SerialBT.begin("ESP32_ISTEMCI", true);
  delay(500);

  kurulum();
  Serial.println("Setup complete.");
}

/* ===================== LOOP ===================== */
void loop() {
  // TOUCH
  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    int px = map(p.x, 300, 3800, 0, gfx->width());
    int py = map(p.y, 300, 3800, 0, gfx->height());

    for (int i = 0; i < elementCount; i++) {
      UIElement &e = elements[i];

      if (e.type == BUTTON &&
          px > e.x && px < e.x + e.w &&
          py > e.y && py < e.y + e.h) {

        Serial.println(e.tx);

        if (e.tx == "001") {
          connectDevice1();        // Device1'e basınca bağlan
        } else {
          SerialBT.println(e.tx);  // diğer butonlar BT'ye gitsin
        }


         if (e.tx == "ds") {
          SerialBT.println("ds");
          delay(100);
          SerialBT.disconnect();
          delay(1000);
          
          kurulum();        
        }

        delay(300);
        break;
      }
    }
  }

  // BT'DEN GELEN KOMUTLAR
  while (SerialBT.available()) {
    processCommand(SerialBT.readStringUntil('\n'));
  }

  // SERIAL'DEN GELEN KOMUTLAR
  while (Serial.available()) {
    processCommand(Serial.readStringUntil('\n'));
  }
}
