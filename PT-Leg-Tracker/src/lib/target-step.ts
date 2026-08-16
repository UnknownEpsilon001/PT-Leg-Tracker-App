// ตรรกะการปรับค่าจำนวนเป้าหมาย (stepTarget) แยกออกมาจาก SessionView.vue เพื่อให้ทดสอบได้
// โดยไม่ต้อง mount คอมโพเนนต์ Vue เงื่อนไขแตกแขนง (setTarget สำเร็จ/ล้มเหลว × อ่านยืนยันค่าใหม่
// สำเร็จ/ล้มเหลว × การกดที่ค้างเก่ากว่า) คือส่วนที่เคยผ่านการแก้ไข race condition มาหลายรอบโดยอาศัย
// การไล่โค้ดด้วยมือล้วนๆ จึงย้ายมาไว้ที่นี่ให้ทดสอบครอบคลุมได้จริง
// stepTarget's decision logic, pulled out of SessionView.vue so it can be tested without
// mounting a Vue component. The branching here — setTarget success/fail × re-read
// success/fail × a stale (superseded) tap — went through several async-state and race fixes
// verified only by hand-tracing; that is exactly the shape that regresses silently under
// refactoring, hence the extraction.
import type { CurrentSession } from '@/types'

// Computes the next target from what the stepper is *showing* (targetShown), not the last
// confirmed value, so taps made while an earlier request is still in flight accumulate
// instead of all landing on the same number. Returns null when there is nothing to send:
// the baseline is unknown ("null" always means "unknown", never zero), or the delta would
// not change the clamped [0, 99] value at all. 0 is a legal target ("run until STOP") and is
// never treated as unset here.
// [TH] คำนวณค่าถัดไปจากค่าที่ปุ่มปรับ "กำลังแสดง" (targetShown) ไม่ใช่จากค่าที่ยืนยันล่าสุด
//      การกดระหว่างที่คำขอก่อนหน้ายังไม่ได้คำตอบจึงสะสมกันไป ไม่ใช่ตกลงที่ตัวเลขเดียวกันหมด
//      คืนค่า null เมื่อไม่มีอะไรต้องส่ง: ค่าเริ่มต้นยังไม่ทราบ ("null" หมายถึง "ไม่ทราบ" เสมอ ไม่ใช่ศูนย์)
//      หรือการเปลี่ยนแปลงนี้ไม่ทำให้ค่าที่ถูกจำกัดช่วง [0, 99] ต่างไปเลย ค่า 0 เป็นเป้าหมายที่ถูกต้อง
//      ("ทำไปเรื่อยๆ ไม่จำกัด") และจะไม่ถูกมองว่ายังไม่ได้ตั้งค่าที่นี่
export interface TargetStepPlan {
  next: number
}

export function planTargetStep(targetShown: number | null, delta: number): TargetStepPlan | null {
  if (targetShown === null) return null // unknown baseline: wait for a real value first
  // [TH] ค่าเริ่มต้นยังไม่ทราบ รอค่าจริงก่อน
  const next = Math.min(99, Math.max(0, targetShown + delta))
  if (next === targetShown) return null
  return { next }
}

// The outcome of actually sending a step to the server, for the view to apply to its own
// refs. The module never touches view state itself.
// - 'failed': setTarget itself failed. Nothing else changes — the displayed value is not
//   rolled back to anything invented; the next poll (once a session exists), or the next
//   tap, re-reads the truth.
// - 'confirmed': setTarget succeeded and the re-read came back, so the server's own value —
//   not a local echo of `next` — is what the view should show. That value may legitimately
//   be 0.
// - 'unconfirmed': setTarget succeeded but the confirming re-read failed. The edit reached
//   the server, but we do not know the resulting number — the same state as "never learned a
//   value" — so the view is told to treat it that way rather than guess `next` or keep the
//   stale pre-edit number. This is what stops a later start() from sending a body that would
//   silently overwrite the edit that already took effect server-side. Subtle and load-bearing:
//   it is the fix for a silent-revert bug, so do not collapse it into 'failed'.
// - 'stale': a newer tap has since started its own request (or the view has been unmounted)
//   — this call's result, success or failure, must not overwrite whatever the newer one
//   already produced, so the view does nothing.
// [TH] ผลของการส่งค่าไปเซิร์ฟเวอร์จริง ให้ฝั่ง view นำไปใช้กับ ref ของตัวเอง โมดูลนี้ไม่แตะ state
//      ของ view เลย
//      - 'failed': setTarget เองล้มเหลว ไม่มีอะไรอื่นเปลี่ยน ค่าที่แสดงจะไม่ถูกดึงกลับไปเป็นค่าที่
//        เดาขึ้นมา การอ่านสถานะรอบถัดไป (ถ้ามีเซสชันแล้ว) หรือการกดครั้งถัดไปจะดึงค่าจริงกลับมาเอง
//      - 'confirmed': setTarget สำเร็จและอ่านยืนยันค่ากลับมาได้ ค่าของเซิร์ฟเวอร์เอง — ไม่ใช่การจำ
//        ค่า next ไว้เอง — คือค่าที่ view ควรแสดง ค่านั้นอาจเป็น 0 ได้จริงๆ
//      - 'unconfirmed': setTarget สำเร็จ แต่การอ่านยืนยันค่าใหม่ล้มเหลว การแก้ไขถึงเซิร์ฟเวอร์แล้ว
//        แต่เราไม่รู้ว่าค่าที่ได้คืออะไร เหมือนกับ "ยังไม่เคยรู้ค่าเลย" จึงบอกให้ view จัดการแบบเดียวกัน
//        ไม่เดาว่าเป็น next และไม่ใช้ค่าเก่าก่อนแก้ไข นี่คือสิ่งที่ทำให้ start() ครั้งถัดไปไม่ส่งค่านี้ไป
//        ทับการแก้ไขที่สำเร็จแล้วที่เซิร์ฟเวอร์ ประเด็นนี้ละเอียดอ่อนและสำคัญมาก คือตัวแก้บั๊กที่ค่าเดิม
//        ถูกทับเงียบๆ ห้ามรวบให้เหมือนกับ 'failed'
//      - 'stale': มีการกดครั้งใหม่กว่าเริ่มคำขอของตัวเองไปแล้ว (หรือ view ถูกถอดออกไปแล้ว) ผลของ
//        ครั้งนี้ ไม่ว่าจะสำเร็จหรือล้มเหลว ต้องไม่ไปทับสิ่งที่ครั้งใหม่กว่าทำไว้แล้ว view จึงไม่ทำอะไรเลย
export type TargetStepOutcome =
  | { kind: 'failed' }
  | { kind: 'confirmed'; targetReps: number }
  | { kind: 'unconfirmed' }
  | { kind: 'stale' }

export interface TargetStepDeps {
  setTarget: (reps: number) => Promise<void>
  getCurrent: () => Promise<CurrentSession>
  // True while this call is still the newest step in flight. Checked after each await so a
  // slower earlier tap's outcome — success or failure — never overwrites a newer tap's
  // result.
  // [TH] จริงตราบเท่าที่การเรียกนี้ยังเป็นการกดล่าสุดที่ค้างอยู่ ตรวจหลัง await ทุกครั้ง เพื่อไม่ให้
  //      ผลของการกดที่ช้ากว่าไปทับผลของการกดที่ใหม่กว่า ไม่ว่าจะสำเร็จหรือล้มเหลว
  isCurrent: () => boolean
  // True once the view has unmounted. Checked only on the re-read paths, matching the
  // original behaviour exactly: a failed setTarget still clears its own pending value even
  // after dispose, it just never reaches this check.
  // [TH] จริงเมื่อ view ถูกถอดออกไปแล้ว ตรวจเฉพาะเส้นทางอ่านยืนยันค่า ตรงตามพฤติกรรมเดิม:
  //      ถ้า setTarget ล้มเหลว ค่าที่ค้างของตัวเองจะยังถูกล้างแม้ view จะถูกถอดไปแล้วก็ตาม เพราะ
  //      เส้นทางนั้นไม่มาถึงการตรวจนี้เลย
  isDisposed: () => boolean
}

// Sends one step to the server and reports what happened. Does not write to any view state —
// the view applies the returned outcome to its own refs.
// [TH] ส่งค่าที่ปรับหนึ่งครั้งไปเซิร์ฟเวอร์ แล้วรายงานว่าเกิดอะไรขึ้น ไม่เขียนลง state ของ view เลย
//      view จะเป็นฝ่ายนำผลลัพธ์ที่ได้ไปใช้กับ ref ของตัวเอง
export async function performTargetStep(
  next: number,
  deps: TargetStepDeps,
): Promise<TargetStepOutcome> {
  try {
    await deps.setTarget(next)
  } catch {
    // a failed change is simply not applied; the next poll re-reads the truth
    // [TH] ถ้าเปลี่ยนไม่สำเร็จก็ถือว่าไม่เปลี่ยน การอ่านสถานะรอบถัดไปจะดึงค่าจริงกลับมาเอง
    return deps.isCurrent() ? { kind: 'failed' } : { kind: 'stale' }
  }
  try {
    const cur = await deps.getCurrent()
    if (deps.isDisposed()) return { kind: 'stale' }
    // A newer tap has since started its own request — let that one's result win instead of
    // overwriting it with this now-stale read.
    // [TH] ถ้ามีการกดครั้งใหม่กว่าเริ่มไปแล้ว ให้ผลของครั้งใหม่ชนะ ไม่เอาค่าเก่าที่เพิ่งอ่านมาทับ
    if (!deps.isCurrent()) return { kind: 'stale' }
    return { kind: 'confirmed', targetReps: cur.targetReps }
  } catch {
    if (deps.isDisposed()) return { kind: 'stale' }
    if (!deps.isCurrent()) return { kind: 'stale' }
    // The edit reached the server (setTarget above succeeded), but the read that would
    // confirm the new number failed. See the 'unconfirmed' case above for why this becomes
    // null upstream rather than a guess.
    // [TH] การแก้ไขถึงเซิร์ฟเวอร์แล้ว (setTarget ข้างบนสำเร็จ) แต่การอ่านยืนยันค่าใหม่ล้มเหลว
    //      ดูเหตุผลที่กรณี 'unconfirmed' ด้านบนว่าทำไมค่านี้จึงกลายเป็น null ที่ฝั่งเรียกใช้ แทนที่จะเดา
    return { kind: 'unconfirmed' }
  }
}
