#include "rtc.h"
#include <Wire.h>
#include <RTClib.h>
#include "config.h"

// RTClib keeps its copy of this inside RTC_DS3231.cpp, so it is repeated here.
// [TH] RTClib เก็บค่านี้ไว้ในไฟล์ .cpp ของตัวเอง จึงต้องประกาศซ้ำที่นี่
static const uint8_t DS3231_ADDR = 0x68;

static RTC_DS3231 rtc;
static bool present = false;

void rtcBegin() {
  Wire.begin(PIN_RTC_SDA, PIN_RTC_SCL);
  present = rtc.begin(&Wire);
}

bool rtcPresent() { return present; }

// Asks the chip whether it is on the bus right now, and updates `present` with
// the answer. Every path that talks to the DS3231 goes through this first,
// because RTClib's now() throws away the result of its I2C read and builds a date
// out of the buffer either way: a connector shaken loose on a moving machine
// returns last call's bytes — a frozen but perfectly plausible time — or
// uninitialised stack. Only the ack tells the two apart.
//
// The answer is not latched off. A bus glitch during one session must not
// condemn a working chip for the rest of the boot: rtcBegin only runs in setup(),
// so a latched failure would also silence the NTP sync, and a machine that booted
// offline, hiccuped once and then found WiFi would never write the time into a
// perfectly good clock.
// [TH] ถามชิปว่ายังอยู่บนบัสหรือไม่ ณ ตอนนี้ แล้วอัปเดตค่า present ตามคำตอบ
//      ทุกเส้นทางที่คุยกับ DS3231 ต้องผ่านตรงนี้ก่อน เพราะ RTClib ทิ้งผลการอ่าน I2C
//      แล้วสร้างวันที่จากบัฟเฟอร์อยู่ดี ถ้าสายหลุดเพราะเครื่องสั่น จะได้ไบต์ของการอ่านครั้งก่อน
//      ซึ่งเป็นเวลาที่หยุดนิ่งแต่ดูน่าเชื่อถือ หรือได้ค่าขยะจากสแตก
//      มีแต่สัญญาณตอบรับเท่านั้นที่แยกสองกรณีนี้ออกจากกัน
//      และไม่ตัดสินค้างไว้ สัญญาณรบกวนบนบัสครั้งเดียวระหว่างรอบหนึ่งต้องไม่ตัดสินว่าชิปที่ยังดีอยู่ใช้ไม่ได้ทั้งการเปิดเครื่องนั้น
//      rtcBegin ทำงานแค่ใน setup() ถ้าตัดสินค้างไว้ การซิงก์เวลาจากอินเทอร์เน็ตจะเงียบไปด้วย
//      เครื่องที่บูตตอนออฟไลน์ สะดุดหนึ่งครั้ง แล้วต่อ WiFi ได้ทีหลัง จะไม่มีวันเขียนเวลาลงชิปที่ยังดีอยู่เลย
static bool rtcAck() {
  Wire.beginTransmission(DS3231_ADDR);
  present = (Wire.endTransmission() == 0);
  return present;
}

uint32_t rtcEpoch() {
  if (!rtcAck()) return 0;
  DateTime now = rtc.now();
  // A DS3231 that has never been set reads back year 2000, which would log as a
  // confidently wrong date. Report "unknown" instead and let the row say so. The
  // upper bound catches the other end: all-0xFF BCD reads as year 2165, whose
  // unixtime() overflows uint32 and wraps back to a believable ~2029 date.
  // [TH] ชิปที่ยังไม่เคยตั้งเวลาจะอ่านได้ปี 2000 ซึ่งเป็นวันที่ผิดแบบดูน่าเชื่อ
  //      จึงคืน 0 ให้รู้ว่าไม่ทราบเวลา ดีกว่าบันทึกวันที่ผิด
  //      ขอบบนดักอีกด้านหนึ่ง ค่า 0xFF ทั้งหมดจะอ่านได้ปี 2165 ซึ่ง unixtime() ล้นค่า 32 บิต
  //      แล้ววนกลับมาเป็นวันที่ราวปี 2029 ที่ดูเหมือนจริง
  if (now.year() < 2020 || now.year() > 2100) return 0;
  return now.unixtime();
}

bool rtcSetFromEpoch(uint32_t utc) {
  if (utc == 0 || !rtcAck()) return false;
  rtc.adjust(DateTime(utc));
  // Confirmed by reading it back, not assumed from the ack. rtc.adjust() returns
  // void and RTClib swallows a NACK partway through its seven-byte register
  // write, so a connector that opens between the probe and the write leaves the
  // chip at year 2000 while the caller believes the time is set — which is the
  // exact outcome the caller's retry exists to prevent.
  // [TH] ยืนยันด้วยการอ่านกลับ ไม่ใช่เดาจากสัญญาณตอบรับ
  //      rtc.adjust() ไม่คืนค่าใดๆ และ RTClib กลืนการปฏิเสธที่เกิดกลางคันของการเขียนทะเบียนเจ็ดไบต์
  //      ถ้าขั้วต่อหลุดระหว่างการตรวจกับการเขียน ชิปจะยังอยู่ที่ปี 2000 ทั้งที่ผู้เรียกเชื่อว่าตั้งเวลาแล้ว
  //      ซึ่งเป็นผลลัพธ์เดียวกับที่การลองใหม่ของผู้เรียกมีไว้ป้องกัน
  return rtcEpoch() != 0;
}
