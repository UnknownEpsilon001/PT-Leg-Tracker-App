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
  deviceUrl: string
  lastSync: string | null
  alarmEnabled: boolean
  alarmTime: string // "HH:mm"
}

export interface PainLog {
  sessionId: string | null
  recordedAt: string // ISO date-time
  painBefore: number
  painAfter: number
}

export interface DeviceStatus {
  state: 'idle' | 'running'
  elapsedSec: number
  sessionId: string | null
}
