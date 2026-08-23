// ตรรกะของแผ่นยืนยันจำนวนเซ็ตก่อนเริ่มรอบ แยกออกจาก SessionView.vue เพื่อให้ทดสอบได้
// โดยไม่ต้อง mount คอมโพเนนต์ เหมือนกับ target-step.ts
// The start-confirmation sheet's decision logic, kept out of SessionView.vue so it can be
// tested without mounting a component — same reasoning as target-step.ts.

// 0 is a legal target ("run until STOP"), so it is never treated as unset. null is the only
// "unknown" here.
// [TH] ค่า 0 เป็นเป้าหมายที่ถูกต้อง ("ทำไปเรื่อยๆ จนกว่าจะกด STOP") จึงไม่ถือว่ายังไม่ได้ตั้งค่า
//      มีแต่ null เท่านั้นที่แปลว่า "ไม่ทราบ"
export function planConfirmStep(shown: number, delta: number): number {
  return Math.min(99, Math.max(0, shown + delta))
}

// The sheet needs a real baseline to open on, and must stay shut while a stepTarget edit is
// still in flight: `target` is stale until that settles, and sending a number now would race
// the outstanding setTarget and silently revert it.
// [TH] แผ่นยืนยันต้องมีค่าตั้งต้นจริงก่อนจึงจะเปิดได้ และต้องไม่เปิดขณะที่การแก้ค่าจาก stepTarget
//      ยังค้างอยู่ เพราะค่า target ยังเป็นค่าเก่าจนกว่าจะเสร็จ การส่งตอนนี้จะไปแข่งและทับการแก้ไขนั้น
export function canOpenConfirm(target: number | null, targetPending: number | null): boolean {
  return target !== null && targetPending === null
}

export function confirmSeed(target: number | null): number {
  return target ?? 0
}
