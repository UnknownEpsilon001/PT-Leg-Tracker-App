import { describe, expect, it } from 'vitest'
import { scoreQuiz } from './quiz'

describe('scoreQuiz', () => {
  const questions = [{ correctIndex: 0 }, { correctIndex: 2 }, { correctIndex: 1 }]

  it('counts correct answers', () => {
    expect(scoreQuiz(questions, [0, 2, 1])).toBe(3)
    expect(scoreQuiz(questions, [0, 0, 0])).toBe(1)
    expect(scoreQuiz(questions, [1, 0, 0])).toBe(0)
  })

  it('ignores questions without correctIndex (satisfaction survey)', () => {
    expect(scoreQuiz([{}, {}, {}], [4, 4, 3])).toBe(0)
  })

  it('handles missing answers safely', () => {
    expect(scoreQuiz(questions, [0])).toBe(1)
  })
})
