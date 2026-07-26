#include "rtc.h"
#include <Wire.h>
#include <RTClib.h>
#include "config.h"

static RTC_DS3231 rtc;
static bool present = false;

void rtcBegin() {
  Wire.begin(PIN_RTC_SDA, PIN_RTC_SCL);
  present = rtc.begin(&Wire);
}

bool rtcPresent() { return present; }

uint32_t rtcEpoch() {
  if (!present) return 0;
  DateTime now = rtc.now();
  // A DS3231 that has never been set reads back year 2000, which would log as a
  // confidently wrong date. Report "unknown" instead and let the row say so.
  // [TH] ชิปที่ยังไม่เคยตั้งเวลาจะอ่านได้ปี 2000 ซึ่งเป็นวันที่ผิดแบบดูน่าเชื่อ
  //      จึงคืน 0 ให้รู้ว่าไม่ทราบเวลา ดีกว่าบันทึกวันที่ผิด
  if (now.year() < 2020) return 0;
  return now.unixtime();
}

void rtcSetFromEpoch(uint32_t utc) {
  if (!present || utc == 0) return;
  rtc.adjust(DateTime(utc));
}
