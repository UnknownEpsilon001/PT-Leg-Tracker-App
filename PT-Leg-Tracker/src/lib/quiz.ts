export function scoreQuiz(
  questions: { correctIndex?: number }[],
  answers: number[],
): number {
  return questions.reduce(
    (score, q, i) =>
      q.correctIndex !== undefined && answers[i] === q.correctIndex ? score + 1 : score,
    0,
  )
}
