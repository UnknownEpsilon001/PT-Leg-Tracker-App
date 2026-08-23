import { describe, it, expect } from 'vitest'
import { planConfirmStep, canOpenConfirm, confirmSeed } from './start-confirm'

describe('planConfirmStep', () => {
  it('steps by the delta', () => {
    expect(planConfirmStep(10, 1)).toBe(11)
    expect(planConfirmStep(10, -1)).toBe(9)
  })
  it('clamps to 0..99', () => {
    expect(planConfirmStep(99, 1)).toBe(99)
    expect(planConfirmStep(0, -1)).toBe(0)
  })
  it('treats 0 as a real value, not unset', () => {
    expect(planConfirmStep(0, 1)).toBe(1)
  })
})

describe('canOpenConfirm', () => {
  it('is open when the target is known and nothing is in flight', () => {
    expect(canOpenConfirm(10, null)).toBe(true)
  })
  it('is open when the known target is 0', () => {
    expect(canOpenConfirm(0, null)).toBe(true)
  })
  it('is closed while a stepTarget edit is in flight', () => {
    expect(canOpenConfirm(10, 12)).toBe(false)
  })
  it('is closed when the target is not known yet', () => {
    expect(canOpenConfirm(null, null)).toBe(false)
  })
})

describe('confirmSeed', () => {
  it('seeds from the known target', () => {
    expect(confirmSeed(8)).toBe(8)
  })
  it('seeds 0 as 0, not as a default', () => {
    expect(confirmSeed(0)).toBe(0)
  })
})
