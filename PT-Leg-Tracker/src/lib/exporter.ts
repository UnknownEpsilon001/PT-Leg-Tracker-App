// ส่งออกข้อมูลทั้งหมดของผู้ป่วยเป็นไฟล์ JSON แล้วเปิดแชร์ (ให้แพทย์/ครูดูได้)
// Export all patient data to a JSON file and open the share sheet.
import { Directory, Encoding, Filesystem } from '@capacitor/filesystem'
import { Share } from '@capacitor/share'
import type { PainLog, Profile, QuizResult, Session } from '@/types'

// [TH] รวมโปรไฟล์ เซสชัน ผลควิซ และบันทึกความปวด เป็นสตริง JSON เดียว
export function buildExport(
  profile: Profile | null,
  sessions: Session[],
  quizResults: QuizResult[],
  painLogs: PainLog[],
): string {
  return JSON.stringify(
    {
      app: 'smart-oa-knee',
      appVersion: '0.0.0',
      exportedAt: new Date().toISOString(),
      profile,
      sessions,
      quizResults,
      painLogs,
    },
    null,
    2,
  )
}

// [TH] เขียนไฟล์ JSON ลงแคช แล้วเรียกหน้าต่างแชร์ ; ชื่อไฟล์ทำให้ปลอดภัยจากอักขระแปลก
export async function shareExport(json: string, patientCode: string): Promise<void> {
  const date = new Date().toISOString().slice(0, 10)
  const safeCode = patientCode.replace(/[^A-Za-z0-9_-]/g, '_')
  const fileName = `smart-oa-knee-${safeCode}-${date}.json`
  const { uri } = await Filesystem.writeFile({
    path: fileName,
    data: json,
    directory: Directory.Cache,
    encoding: Encoding.UTF8,
  })
  await Share.share({ title: 'ข้อมูล Med-Flex Auto Smart', url: uri })
}
