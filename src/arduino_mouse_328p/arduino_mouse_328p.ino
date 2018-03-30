#include <hidboot.h>
#include <usbhub.h>

// Satisfy the IDE, which needs to see the include statment in the ino too.
#ifdef dobogusinclude
#include <spi4teensy3.h>
#endif
#include <SPI.h>
int8_t Dy = 8;
int8_t key = 7;

struct {
  uint8_t buttons;
  int8_t x;
  int8_t y;
  int8_t wheel; /* Not yet implemented */
} mouseReport;

class MouseRptParser : public MouseReportParser {
 protected:
  void OnMouseMove(MOUSEINFO *mi);
  void OnLeftButtonUp(MOUSEINFO *mi);
  void OnLeftButtonDown(MOUSEINFO *mi);
  void OnRightButtonUp(MOUSEINFO *mi);
  void OnRightButtonDown(MOUSEINFO *mi);
  void OnMiddleButtonUp(MOUSEINFO *mi);
  void OnMiddleButtonDown(MOUSEINFO *mi);
};
void MouseRptParser::OnMouseMove(MOUSEINFO *mi) {
  // Serial.print("dx=");
  // Serial.print(mi->dX, DEC);
  // Serial.print(" dy=");
  // Serial.println(mi->dY, DEC);

  mouseReport.x = mi->dX;
  mouseReport.y = -mi->dY;  // set it by yourself
  Serial.write((uint8_t *)&mouseReport, 4);
  mouseReport.y = 0;
  //
  //
};
void MouseRptParser::OnLeftButtonUp(MOUSEINFO *mi) {
  mouseReport.buttons = 0;
  Serial.write((uint8_t *)&mouseReport, 4);
  // Serial.println("L Butt Up");
};
void MouseRptParser::OnLeftButtonDown(MOUSEINFO *mi) {
  mouseReport.buttons = 1;
  Serial.write((uint8_t *)&mouseReport, 4);
  mouseReport.y = Dy;
  Serial.write((uint8_t *)&mouseReport, 4);
  mouseReport.y = 0;
  // Serial.println("L Butt Dn");
};
void MouseRptParser::OnRightButtonUp(MOUSEINFO *mi) {
  mouseReport.buttons = 0;
  Serial.write((uint8_t *)&mouseReport, 4);
  // Serial.println("R Butt Up");
};
void MouseRptParser::OnRightButtonDown(MOUSEINFO *mi) {
  mouseReport.buttons = 2;
  Serial.write((uint8_t *)&mouseReport, 4);
  // Serial.println("R Butt Dn");
};
void MouseRptParser::OnMiddleButtonUp(MOUSEINFO *mi) {
  mouseReport.buttons = 0;
  Serial.write((uint8_t *)&mouseReport, 4);
  // Serial.println("M Butt Up");
};
void MouseRptParser::OnMiddleButtonDown(MOUSEINFO *mi) {
  mouseReport.buttons = 4;
  Serial.write((uint8_t *)&mouseReport, 4);
  // Serial.println("M Butt Dn");
};

USB Usb;
USBHub Hub(&Usb);
HIDBoot<USB_HID_PROTOCOL_MOUSE> HidMouse(&Usb);

MouseRptParser Prs;

void setup() {
  Serial.begin(57600);
#if !defined(__MIPSEL__)
  while (!Serial)
    ;  // Wait for serial port to connect - used on Leonardo, Teensy and other
       // boards with built-in USB CDC serial connection
#endif
  // Serial.println("Start");

  if (Usb.Init() == -1)
    // Serial.println("OSC did not start.");

    delay(200);

  HidMouse.SetReportParser(0, &Prs);

  pinMode(key, INPUT);
  if (digitalRead(key) == LOW) {
    Dy = 0;
  }
}

void loop() { Usb.Task(); }
