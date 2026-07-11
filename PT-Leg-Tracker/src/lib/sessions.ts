import type { Session } from '@/types'

export function mergeSessions(local: Session[], remote: Session[]): Session[] {
  const byId = new Map<string, Session>()
  for (const s of remote) byId.set(s.id, s)
  for (const s of local) byId.set(s.id, s) // local wins
  return [...byId.values()].sort((a, b) => a.date.localeCompare(b.date))
}
