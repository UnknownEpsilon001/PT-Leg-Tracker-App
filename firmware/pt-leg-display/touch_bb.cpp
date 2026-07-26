#include "touch_bb.h"
#include <Arduino.h>

// CYD touch wiring — fixed by the board, not a choice.
// [TH] ขาทัชสกรีนของบอร์ด CYD ตายตัวมาจากโรงงาน เปลี่ยนไม่ได้
#define T_IRQ 36   // PENIRQ, input-only pin — LOW while the panel is pressed
#define T_MOSI 32
#define T_MISO 39  // input-only pin
#define T_CLK 25
#define T_CS 33

// Control bytes: start bit, channel, 12-bit mode, differential, power-down
// between conversions. Power-down is what keeps PENIRQ alive, so the pressed
// check below can stay a single digitalRead.
// [TH] ไบต์คำสั่ง: เลือกแกน โหมด 12 บิต และให้ชิปพักระหว่างการวัด
//      การให้พักคือสิ่งที่ทำให้ขา PENIRQ ยังทำงาน จึงเช็คการกดด้วย digitalRead ขาเดียวได้
#define CMD_X 0xD0
#define CMD_Y 0x90

static uint16_t transfer(uint8_t cmd) {
  digitalWrite(T_CS, LOW);
  for (int i = 7; i >= 0; i--) {  // command, MSB first — ส่งคำสั่งบิตสูงก่อน
    digitalWrite(T_MOSI, (cmd >> i) & 1);
    digitalWrite(T_CLK, HIGH);
    digitalWrite(T_CLK, LOW);
  }
  // 16 clocks: one busy bit, then the 12 data bits, then padding. Shifting by 3
  // drops the padding and leaves the reading right-aligned — the same sequence
  // the hardware-SPI drivers use (transfer(cmd), transfer(0), transfer(0)).
  // [TH] ยิงสัญญาณนาฬิกา 16 ครั้ง: บิตแรกเป็นบิตรอ ตามด้วยข้อมูล 12 บิต ที่เหลือเป็นบิตว่าง
  //      เลื่อนขวา 3 เพื่อตัดบิตว่างทิ้ง ได้ลำดับเดียวกับไดรเวอร์ที่ใช้ SPI ฮาร์ดแวร์
  uint16_t v = 0;
  for (int i = 0; i < 16; i++) {
    digitalWrite(T_CLK, HIGH);
    v = (v << 1) | (digitalRead(T_MISO) ? 1 : 0);
    digitalWrite(T_CLK, LOW);
  }
  digitalWrite(T_CS, HIGH);
  return v >> 3;
}

void touchBBBegin() {
  pinMode(T_CS, OUTPUT);
  pinMode(T_CLK, OUTPUT);
  pinMode(T_MOSI, OUTPUT);
  pinMode(T_MISO, INPUT);
  pinMode(T_IRQ, INPUT);
  digitalWrite(T_CS, HIGH);
  digitalWrite(T_CLK, LOW);
}

bool touchBBRead(uint16_t& x, uint16_t& y) {
  if (digitalRead(T_IRQ) != LOW) return false;  // not pressed — ไม่ได้แตะ

  // Two samples averaged. A single read of a resistive panel jitters by tens of
  // counts, which on a 320 px axis is a visible wobble under the finger.
  // [TH] เฉลี่ยสองครั้ง เพราะจอสัมผัสแบบต้านทานอ่านครั้งเดียวจะแกว่งหลายสิบหน่วย
  //      ซึ่งเห็นเป็นอาการสั่นใต้ปลายนิ้วบนแกนกว้าง 320 พิกเซล
  uint16_t x1 = transfer(CMD_X), y1 = transfer(CMD_Y);
  uint16_t x2 = transfer(CMD_X), y2 = transfer(CMD_Y);

  // PENIRQ can still be asserted while the finger is lifting, and those last
  // conversions read as 0 or full-scale. Treat the extremes as "released"
  // rather than reporting a tap in the corner.
  // [TH] ตอนยกนิ้ว ขา PENIRQ ยังค้างอยู่ได้ และค่าที่วัดได้จะเป็น 0 หรือค่าสุดสเกล
  //      ถือว่าเป็นการปล่อยนิ้ว ดีกว่ารายงานว่ามีการแตะที่มุมจอ
  uint16_t xa = (x1 + x2) / 2, ya = (y1 + y2) / 2;
  if (xa < 100 || xa > 4000 || ya < 100 || ya > 4000) return false;

  x = xa;
  y = ya;
  return true;
}
