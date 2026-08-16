#pragma once
#include <stdint.h>

// Wire-format enum shared with the controller: pt-leg-controller/session.h
// owns the real state machine, and this board only ever receives a phase
// number over the link and looks it up here (see pt-leg-display.ino and
// ui.cpp). The enumerator order must stay byte-identical between the two
// copies, since the wire's "phase":int is that order.
// [TH] enum รูปแบบข้อมูลบนสายที่ใช้ร่วมกับบอร์ดคอนโทรลเลอร์ เครื่องสถานะจริงอยู่ที่
//      pt-leg-controller/session.h บอร์ดนี้รับแค่หมายเลขสถานะผ่านลิงก์แล้วมาเทียบกับ enum นี้
//      (ดู pt-leg-display.ino และ ui.cpp) ลำดับค่าต้องตรงกันทุกตัวอักษรระหว่างสองไฟล์นี้
//      เพราะ "phase":int บนสายอ้างอิงลำดับนี้โดยตรง
enum class Phase { Idle, Lift, Hold, Lower, Rest, SafeLower, Fault };
