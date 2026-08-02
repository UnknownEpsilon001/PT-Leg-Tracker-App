#pragma once
#include <stdint.h>

// นาฬิกาเวลาจริง DS3231 บนบัส I2C ของบอร์ดคอนโทรลเลอร์
// มีถ่านสำรอง เวลาจึงยังถูกต้องแม้ไฟดับ ค่าเวลาทั้งหมดเป็นวินาที UTC
// DS3231 real-time clock on the controller I2C bus. Battery-backed: correct
// across power cuts once set. All epochs are UTC unix seconds.

void rtcBegin();                    // Wire.begin + probe; safe if the chip is absent
                                    // [TH] เริ่ม I2C และตรวจว่ามีชิปหรือไม่ ไม่มีก็ไม่พัง
bool rtcPresent();                  // whether the chip answered the last time it was asked
                                    // [TH] ชิปตอบรับหรือไม่ในครั้งล่าสุดที่ถาม
uint32_t rtcEpoch();                // UTC seconds; 0 if absent or never set (year < 2020)
                                    // [TH] วินาที UTC คืน 0 ถ้าไม่มีชิปหรือยังไม่เคยตั้งเวลา
bool rtcSetFromEpoch(uint32_t utc); // write the time (used after an NTP sync); false if the
                                    // chip did not answer, so the caller can try again later
                                    // [TH] ตั้งเวลาลงชิป (ใช้หลังซิงก์เวลาจาก NTP)
                                    //      คืน false ถ้าชิปไม่ตอบ ผู้เรียกจะได้ลองใหม่ทีหลัง
