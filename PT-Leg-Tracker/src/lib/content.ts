export interface AppContent {
  homeSubtitle: string
  logo: string
  logoAlt: string
}

export interface KnowledgeArticle {
  id: string
  title: string
  body: string
  image: string
  imageAlt: string
}

export interface KnowledgeSection {
  id: string
  title: string
  articles: KnowledgeArticle[]
}

export interface KnowledgeContent {
  sections: KnowledgeSection[]
}

export interface GuideStep {
  id: string
  title: string
  caption: string
  image: string
}

export type GuideVideo =
  | { type: 'youtube'; id: string; title: string }
  | { type: 'file'; src: string; title: string }

export interface DeviceGuideContent {
  steps: GuideStep[]
  videos: GuideVideo[]
}

export type QuizId = 'pre' | 'post' | 'satisfaction'

export interface QuizQuestion {
  text: string
  choices: string[]
  correctIndex?: number
}

export interface QuizDefinition {
  id: QuizId
  title: string
  questions: QuizQuestion[]
}

export interface QuizContent {
  quizzes: QuizDefinition[]
}

export class ContentError extends Error {
  constructor(public file: string) {
    super(`invalid editable content: ${file}`)
  }
}

const cache = new Map<string, unknown>()

export function clearContentCache(): void {
  cache.clear()
}

async function loadFile<T>(file: string, validate: (value: unknown) => T): Promise<T> {
  if (cache.has(file)) return cache.get(file) as T

  try {
    const res = await fetch(`/editable-content/${file}`)
    if (!res.ok) throw new ContentError(file)
    const value = validate(await res.json())
    cache.set(file, value)
    return value
  } catch (error) {
    if (error instanceof ContentError) throw error
    throw new ContentError(file)
  }
}

function record(value: unknown): Record<string, unknown> {
  if (typeof value !== 'object' || value === null || Array.isArray(value)) throw new Error()
  return value as Record<string, unknown>
}

function text(value: unknown): string {
  if (typeof value !== 'string' || value.trim() === '') throw new Error()
  return value
}

function list(value: unknown): unknown[] {
  if (!Array.isArray(value)) throw new Error()
  return value
}

function imagePath(value: unknown): string {
  const path = text(value)
  if (
    !/^\/editable-content\/images\/(?:[a-z0-9]+(?:-[a-z0-9]+)*\/)*[a-z0-9]+(?:-[a-z0-9]+)*\.(png|jpg|jpeg|webp|svg)$/.test(
      path,
    )
  ) {
    throw new Error()
  }
  return path
}

function videoPath(value: unknown): string {
  const path = text(value)
  if (
    !/^\/editable-content\/videos\/(?:[a-z0-9]+(?:-[a-z0-9]+)*\/)*[a-z0-9]+(?:-[a-z0-9]+)*\.(mp4|webm)$/.test(
      path,
    )
  ) {
    throw new Error()
  }
  return path
}

function youtubeId(value: unknown): string {
  const id = text(value)
  if (!/^[A-Za-z0-9_-]{11}$/.test(id)) throw new Error()
  return id
}

function validateApp(value: unknown): AppContent {
  const item = record(value)
  return {
    homeSubtitle: text(item.homeSubtitle),
    logo: imagePath(item.logo),
    logoAlt: text(item.logoAlt),
  }
}

function validateKnowledge(value: unknown): KnowledgeContent {
  const articleIds = new Set<string>()
  const sections = list(record(value).sections).map((sectionValue): KnowledgeSection => {
    const section = record(sectionValue)
    const articles = list(section.articles).map((articleValue): KnowledgeArticle => {
      const article = record(articleValue)
      const id = text(article.id)
      if (articleIds.has(id)) throw new Error()
      articleIds.add(id)
      return {
        id,
        title: text(article.title),
        body: text(article.body),
        image: imagePath(article.image),
        imageAlt: text(article.imageAlt),
      }
    })
    return { id: text(section.id), title: text(section.title), articles }
  })
  return { sections }
}

function validateGuide(value: unknown): DeviceGuideContent {
  const item = record(value)
  const stepIds = new Set<string>()
  const steps = list(item.steps).map((stepValue): GuideStep => {
    const step = record(stepValue)
    const id = text(step.id)
    if (stepIds.has(id)) throw new Error()
    stepIds.add(id)
    return {
      id,
      title: text(step.title),
      caption: text(step.caption),
      image: imagePath(step.image),
    }
  })
  if (steps.length === 0) throw new Error()

  const videos = list(item.videos).map((videoValue): GuideVideo => {
    const video = record(videoValue)
    const title = text(video.title)
    if (video.type === 'youtube') {
      return { type: 'youtube', id: youtubeId(video.id), title }
    }
    if (video.type === 'file') {
      const src = videoPath(video.src)
      return { type: 'file', src, title }
    }
    throw new Error()
  })
  return { steps, videos }
}

function validateQuizzes(value: unknown): QuizContent {
  const fixedIds: QuizId[] = ['pre', 'post', 'satisfaction']
  const seen = new Set<QuizId>()
  const quizzes = list(record(value).quizzes).map((quizValue): QuizDefinition => {
    const quiz = record(quizValue)
    const rawId = text(quiz.id)
    if (!fixedIds.includes(rawId as QuizId) || seen.has(rawId as QuizId)) throw new Error()
    const id = rawId as QuizId
    seen.add(id)
    const questions = list(quiz.questions).map((questionValue): QuizQuestion => {
      const question = record(questionValue)
      const choices = list(question.choices).map(text)
      if (choices.length < 2) throw new Error()
      const result: QuizQuestion = { text: text(question.text), choices }
      if (id === 'satisfaction') {
        if (question.correctIndex !== undefined) throw new Error()
      } else {
        if (
          !Number.isInteger(question.correctIndex) ||
          (question.correctIndex as number) < 0 ||
          (question.correctIndex as number) >= choices.length
        )
          throw new Error()
        result.correctIndex = question.correctIndex as number
      }
      return result
    })
    if (questions.length === 0) throw new Error()
    return { id, title: text(quiz.title), questions }
  })
  if (quizzes.length !== fixedIds.length || fixedIds.some((id) => !seen.has(id))) throw new Error()
  return { quizzes }
}

export const loadAppContent = () => loadFile('app.json', validateApp)
export const loadKnowledgeContent = () => loadFile('knowledge.json', validateKnowledge)
export const loadDeviceGuide = () => loadFile('device-guide.json', validateGuide)
export const loadQuizContent = () => loadFile('quizzes.json', validateQuizzes)
