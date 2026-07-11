export interface Profile {
  name: string
  age: number
  patientCode: string
  createdAt: string
}

export interface Session {
  id: string
  date: string // ISO date-time
  durationSec: number
  painBefore?: number
  painAfter?: number
  source: 'manual' | 'device'
}

export interface QuizResult {
  quizId: 'pre' | 'post' | 'satisfaction'
  answers: number[]
  score: number
  timestamp: string
}

export interface Settings {
  fontLarge: boolean
  serverUrl: string
  lastSync: string | null
}
