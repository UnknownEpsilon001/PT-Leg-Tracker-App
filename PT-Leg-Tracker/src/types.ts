// ชนิดข้อมูลกลางที่ใช้ทั้งแอป: โปรไฟล์ผู้ป่วย เซสชัน ผลควิซ ค่าตั้งค่า บันทึกความปวด และสถานะเซสชันปัจจุบัน
// Shared data types used across the app.

export interface Profile {  // โปรไฟล์ผู้ป่วย
  name: string
  age: number
  patientCode: string  // รหัสผู้ป่วย ใช้จับคู่ข้อมูลกับเซิร์ฟเวอร์
  createdAt: string
}

export interface Session {  // หนึ่งเซสชันการออกกำลังกาย
  id: string
  date: string // ISO date-time — วันเวลาแบบ ISO
  durationSec: number  // ระยะเวลา (วินาที)
  painBefore?: number  // ความปวดก่อน
  painAfter?: number   // ความปวดหลัง
  source: 'manual' | 'device'  // ที่มา: กรอกเอง หรือ จากเครื่อง
}

export interface QuizResult {
  quizId: 'pre' | 'post' | 'satisfaction'
  answers: number[]
  score: number
  timestamp: string
}

export interface Settings {  // ค่าตั้งค่าของแอป
  fontLarge: boolean   // โหมดตัวอักษรใหญ่ (ช่วยผู้สูงอายุ)
  serverUrl: string    // ที่อยู่เซิร์ฟเวอร์
  deviceCode: string   // รหัสอุปกรณ์สำหรับสั่งงานเครื่อง
  lastSync: string | null  // เวลาซิงก์ล่าสุด
  alarmEnabled: boolean    // เปิดการเตือนประจำวันหรือไม่
  alarmTime: string // "HH:mm" — เวลาเตือน
}

export interface PainLog {
  sessionId: string | null
  recordedAt: string // ISO date-time
  painBefore: number
  painAfter: number
}

export interface CurrentSession {
  sessionId: string | null
  state: 'idle' | 'starting' | 'running'
  elapsedSec: number
  reps: number
  deviceOnline: boolean
  targetReps: number
}
