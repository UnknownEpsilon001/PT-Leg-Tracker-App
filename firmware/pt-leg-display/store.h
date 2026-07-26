#pragma once
#include <Arduino.h>

// ทะเบียนผู้ป่วยที่เก็บบนการ์ด SD ของบอร์ดจอ (ไฟล์ /patients.csv)
// เครื่องต้องใช้งานได้โดยไม่มีมือถือและไม่มีอินเทอร์เน็ต ข้อมูลจึงอยู่บนการ์ด
// Patient roster on the CYD's microSD card (/patients.csv). The machine is meant
// to work with no phone and no server, so the roster lives on the card.

struct Patient {
  String code;
  String name;
  uint16_t age;
};

// SD card on the CYD (VSPI). storeBegin mounts the card and loads /patients.csv.
// Everything below is safe to call with no card: the roster is simply empty.
// [TH] เรียกได้แม้ไม่มีการ์ด จะได้ทะเบียนว่างเปล่า ไม่ทำให้เครื่องหยุดทำงาน
bool storeBegin();                       // false if no card / mount failed
bool storeCardPresent();
int storePatientCount();
const Patient& storePatient(int i);      // 0..count-1; returns a static blank if out of range
bool storeAddPatient(const Patient& p);  // append to CSV + RAM; false on write failure
