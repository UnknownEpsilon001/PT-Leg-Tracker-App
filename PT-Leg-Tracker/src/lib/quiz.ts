// ตรวจให้คะแนนควิซความรู้เรื่องเข่าเสื่อม: นับข้อที่ตอบตรงกับเฉลย
// Score the knee-OA knowledge quiz: count answers matching the correct index.
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
