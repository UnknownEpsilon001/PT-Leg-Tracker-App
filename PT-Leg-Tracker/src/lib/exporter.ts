import { Directory, Encoding, Filesystem } from '@capacitor/filesystem'
import { Share } from '@capacitor/share'
import type { Profile, QuizResult, Session } from '@/types'

export function buildExport(
  profile: Profile | null,
  sessions: Session[],
  quizResults: QuizResult[],
): string {
  return JSON.stringify(
    {
      app: 'smart-oa-knee',
      appVersion: '0.0.0',
      exportedAt: new Date().toISOString(),
      profile,
      sessions,
      quizResults,
    },
    null,
    2,
  )
}

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
  await Share.share({ title: 'ข้อมูล Smart OA Knee', url: uri })
}
