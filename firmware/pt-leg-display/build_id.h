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

#ifdef BUILD_COMMIT
#define BUILD_COMMIT_STR BUILD_STR(BUILD_COMMIT)
#else
#define BUILD_COMMIT_STR "unknown"
#endif

#ifdef BUILD_BRANCH
#define BUILD_BRANCH_STR BUILD_STR(BUILD_BRANCH)
#else
#define BUILD_BRANCH_STR "unknown"
#endif

#define BUILD_TIME_STR __DATE__ " " __TIME__
