#pragma once

// Build identity for the [BUILD] line each board prints at boot. flash.ps1 passes
// BUILD_COMMIT and BUILD_BRANCH. An Arduino IDE build has neither and prints
// "unknown", which is how to tell the two apart on the serial port.
// [TH] ข้อมูลรุ่นสำหรับบรรทัด [BUILD] ที่บอร์ดพิมพ์ตอนเปิดเครื่อง
//      flash.ps1 ส่งค่า BUILD_COMMIT และ BUILD_BRANCH มาให้
//      ถ้าคอมไพล์จาก Arduino IDE จะไม่มีสองค่านี้และพิมพ์ว่า "unknown" จึงแยกออกได้จากพอร์ตอนุกรม

#define FW_VERSION "0.1.0"

#define BUILD_STR_(x) #x
#define BUILD_STR(x) BUILD_STR_(x)

// flash.ps1 writes build_info.h next to the sketch for each build and deletes it
// after, so only the sketch recompiles when the commit changes; the core and the
// libraries stay cached. The -D flags are the older way and still work.
// [TH] flash.ps1 เขียน build_info.h ไว้ข้างสเก็ตช์ทุกครั้งที่คอมไพล์แล้วลบทิ้ง เปลี่ยนคอมมิตจึงคอมไพล์ใหม่แค่สเก็ตช์
#if __has_include("build_info.h")
#include "build_info.h"
#endif

#ifndef BUILD_COMMIT_STR
#ifdef BUILD_COMMIT
#define BUILD_COMMIT_STR BUILD_STR(BUILD_COMMIT)
#else
#define BUILD_COMMIT_STR "unknown"
#endif
#endif

#ifndef BUILD_BRANCH_STR
#ifdef BUILD_BRANCH
#define BUILD_BRANCH_STR BUILD_STR(BUILD_BRANCH)
#else
#define BUILD_BRANCH_STR "unknown"
#endif
#endif

#define BUILD_TIME_STR __DATE__ " " __TIME__
