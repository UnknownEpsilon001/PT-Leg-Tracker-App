import { afterEach, describe, expect, it, vi } from 'vitest'
import {
  ContentError,
  clearContentCache,
  loadAppContent,
  loadDeviceGuide,
  loadKnowledgeContent,
  loadQuizContent,
} from './content'

function response(body: unknown, ok = true): Response {
  return { ok, json: async () => body } as Response
}

afterEach(() => {
  clearContentCache()
  vi.unstubAllGlobals()
})

describe('editable content loader', () => {
  it('loads and caches app content', async () => {
    const fetchMock = vi.fn().mockResolvedValue(
      response({
        homeSubtitle: 'Smart OA Knee',
        logo: '/editable-content/images/branding/logo.png',
        logoAlt: 'logo',
      }),
    )
    vi.stubGlobal('fetch', fetchMock)

    const first = await loadAppContent()
    const second = await loadAppContent()

    expect(first).toEqual({
      homeSubtitle: 'Smart OA Knee',
      logo: '/editable-content/images/branding/logo.png',
      logoAlt: 'logo',
    })
    expect(second).toBe(first)
    expect(fetchMock).toHaveBeenCalledTimes(1)
    expect(fetchMock).toHaveBeenCalledWith('/editable-content/app.json')
  })

  it('throws ContentError when a file is missing', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(response({}, false)))

    await expect(loadAppContent()).rejects.toBeInstanceOf(ContentError)
  })

  it('throws ContentError when json parsing fails', async () => {
    vi.stubGlobal('fetch',
      vi.fn().mockResolvedValue({
        ok: true,
        json: async () => {
          throw new SyntaxError('bad json')
        },
      }),
    )

    await expect(loadAppContent()).rejects.toMatchObject({ file: 'app.json' })
  })

  it('rejects malformed app content', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(response({ homeSubtitle: 'Smart OA Knee' })))

    await expect(loadAppContent()).rejects.toMatchObject({ file: 'app.json' })
  })

  it('accepts knowledge with unique article ids', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(response({
      sections: [{
        id: 'knowledge',
        title: 'ความรู้',
        articles: [{
          id: 'a1',
          title: 'หัวข้อ',
          body: 'เนื้อหา',
          image: '/editable-content/images/knowledge/a.jpg',
          imageAlt: 'ภาพ',
        }],
      }],
    })))

    await expect(loadKnowledgeContent()).resolves.toMatchObject({ sections: [{ id: 'knowledge' }] })
  })

  it('rejects duplicate article ids', async () => {
    const article = {
      id: 'same',
      title: 'หัวข้อ',
      body: 'เนื้อหา',
      image: '/editable-content/images/knowledge/a.jpg',
      imageAlt: 'ภาพ',
    }
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(response({
      sections: [
        { id: 'one', title: 'หนึ่ง', articles: [article] },
        { id: 'two', title: 'สอง', articles: [article] },
      ],
    })))

    await expect(loadKnowledgeContent()).rejects.toMatchObject({ file: 'knowledge.json' })
  })

  it('accepts youtube and local guide videos', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(response({
      steps: [{
        id: 'step-1',
        title: 'ขั้นตอน',
        caption: 'คำอธิบาย',
        image: '/editable-content/images/guide/step.jpg',
      }],
      videos: [
        { type: 'youtube', id: 'abc123', title: 'ออนไลน์' },
        { type: 'file', src: '/editable-content/videos/demo.mp4', title: 'ออฟไลน์' },
      ],
    })))

    await expect(loadDeviceGuide()).resolves.toMatchObject({
      videos: [{ type: 'youtube' }, { type: 'file' }],
    })
  })

  it('rejects an unknown guide video type', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(response({
      steps: [{
        id: 'step-1',
        title: 'ขั้นตอน',
        caption: 'คำอธิบาย',
        image: '/editable-content/images/guide/step.jpg',
      }],
      videos: [{ type: 'vimeo', id: 'x', title: 'x' }],
    })))

    await expect(loadDeviceGuide()).rejects.toMatchObject({ file: 'device-guide.json' })
  })

  it('rejects a local guide video with an unsupported extension', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(response({
      steps: [{
        id: 'step-1',
        title: 'ขั้นตอน',
        caption: 'คำอธิบาย',
        image: '/editable-content/images/guide/step.jpg',
      }],
      videos: [{ type: 'file', src: '/editable-content/videos/demo.mov', title: 'ออฟไลน์' }],
    })))

    await expect(loadDeviceGuide()).rejects.toMatchObject({ file: 'device-guide.json' })
  })

  it('accepts the three fixed quiz ids', async () => {
    const q = (id: string, correctIndex?: number) => ({
      id,
      title: id,
      questions: [{ text: 'คำถาม', choices: ['ก', 'ข'], ...(correctIndex === undefined ? {} : { correctIndex }) }],
    })
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(response({
      quizzes: [q('pre', 0), q('post', 1), q('satisfaction')],
    })))

    await expect(loadQuizContent()).resolves.toMatchObject({
      quizzes: [{ id: 'pre' }, { id: 'post' }, { id: 'satisfaction' }],
    })
  })

  it('rejects quiz ids outside the fixed set', async () => {
    const q = (id: string, correctIndex?: number) => ({
      id,
      title: id,
      questions: [{ text: 'คำถาม', choices: ['ก', 'ข'], ...(correctIndex === undefined ? {} : { correctIndex }) }],
    })
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(response({
      quizzes: [q('pre', 0), q('post', 1), q('followup')],
    })))

    await expect(loadQuizContent()).rejects.toMatchObject({ file: 'quizzes.json' })
  })

  it('rejects an out-of-range correctIndex', async () => {
    const quiz = {
      id: 'pre',
      title: 'pre',
      questions: [{ text: 'คำถาม', choices: ['ก'], correctIndex: 1 }],
    }
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(response({
      quizzes: [
        quiz,
        { ...quiz, id: 'post' },
        { ...quiz, id: 'satisfaction', questions: [{ text: 'คำถาม', choices: ['ก'] }] },
      ],
    })))

    await expect(loadQuizContent()).rejects.toMatchObject({ file: 'quizzes.json' })
  })
})
