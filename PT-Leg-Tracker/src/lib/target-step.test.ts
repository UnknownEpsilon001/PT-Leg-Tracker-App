// ทดสอบตรรกะการปรับค่าจำนวนเป้าหมาย (target-step.ts) ที่แยกออกมาจาก SessionView.vue
// Tests for the stepTarget decision logic extracted from SessionView.vue.
//
// These tests exercise the branching in isolation: setTarget success/fail × re-read
// success/fail × a stale (superseded) tap. They do not, and cannot, prove that the server's
// /current always reflects a target the device has round-tripped — that assumption is what
// made the 'unconfirmed' outcome necessary in the first place, and no mock of getCurrent can
// stand in for the real device's timing. What is covered here is that this module reacts to
// each combination of outcomes the way the view depends on.
import { describe, expect, it, vi } from 'vitest'
import { performTargetStep, planTargetStep } from './target-step'

describe('planTargetStep', () => {
  it('returns null when the baseline is unknown', () => {
    expect(planTargetStep(null, 1)).toBeNull()
  })

  it('steps up from the shown value', () => {
    expect(planTargetStep(10, 1)).toEqual({ next: 11 })
  })

  it('steps down from the shown value', () => {
    expect(planTargetStep(10, -1)).toEqual({ next: 9 })
  })

  it('clamps at the 99 ceiling', () => {
    expect(planTargetStep(99, 1)).toBeNull()
    expect(planTargetStep(98, 5)).toEqual({ next: 99 })
  })

  it('clamps at the 0 floor', () => {
    expect(planTargetStep(0, -1)).toBeNull()
    expect(planTargetStep(3, -10)).toEqual({ next: 0 })
  })

  it('treats 0 as a real baseline, not an unknown one', () => {
    // 0 means "run until STOP" — it must be steppable like any other value, and must not be
    // confused with the null "unknown" baseline.
    expect(planTargetStep(0, 1)).toEqual({ next: 1 })
  })

  it('returns null for a delta that would not change the clamped value', () => {
    expect(planTargetStep(50, 0)).toBeNull()
  })
})

describe('performTargetStep', () => {
  function deps(overrides: Partial<Parameters<typeof performTargetStep>[1]> = {}) {
    return {
      setTarget: vi.fn().mockResolvedValue(undefined),
      getCurrent: vi.fn().mockResolvedValue({
        sessionId: null,
        state: 'idle' as const,
        elapsedSec: 0,
        reps: 0,
        deviceOnline: true,
        targetReps: 7,
      }),
      isCurrent: () => true,
      isDisposed: () => false,
      ...overrides,
    }
  }

  it('reports failed when setTarget rejects, and never re-reads', async () => {
    const getCurrent = vi.fn()
    const outcome = await performTargetStep(
      5,
      deps({ setTarget: vi.fn().mockRejectedValue(new Error('x')), getCurrent }),
    )
    expect(outcome).toEqual({ kind: 'failed' })
    expect(getCurrent).not.toHaveBeenCalled()
  })

  it('reports confirmed with the server value when setTarget and the re-read both succeed', async () => {
    const outcome = await performTargetStep(
      5,
      deps({
        getCurrent: vi
          .fn()
          .mockResolvedValue({
            sessionId: null,
            state: 'idle',
            elapsedSec: 0,
            reps: 0,
            deviceOnline: true,
            targetReps: 5,
          }),
      }),
    )
    expect(outcome).toEqual({ kind: 'confirmed', targetReps: 5 })
  })

  it('confirms a server value of 0 rather than treating it as falsy/unset', async () => {
    const outcome = await performTargetStep(
      0,
      deps({
        getCurrent: vi
          .fn()
          .mockResolvedValue({
            sessionId: null,
            state: 'idle',
            elapsedSec: 0,
            reps: 0,
            deviceOnline: true,
            targetReps: 0,
          }),
      }),
    )
    expect(outcome).toEqual({ kind: 'confirmed', targetReps: 0 })
  })

  it('reports unconfirmed when setTarget succeeds but the re-read fails', async () => {
    const outcome = await performTargetStep(
      5,
      deps({ getCurrent: vi.fn().mockRejectedValue(new Error('network')) }),
    )
    expect(outcome).toEqual({ kind: 'unconfirmed' })
  })

  it('reports stale when a newer tap has already superseded this one before the failure lands', async () => {
    const outcome = await performTargetStep(
      5,
      deps({ setTarget: vi.fn().mockRejectedValue(new Error('x')), isCurrent: () => false }),
    )
    expect(outcome).toEqual({ kind: 'stale' })
  })

  it('reports stale, not confirmed, when a newer tap has superseded this one after a successful re-read', async () => {
    const outcome = await performTargetStep(5, deps({ isCurrent: () => false }))
    expect(outcome).toEqual({ kind: 'stale' })
  })

  it('reports stale, not unconfirmed, when a newer tap has superseded this one after a failed re-read', async () => {
    const outcome = await performTargetStep(
      5,
      deps({ getCurrent: vi.fn().mockRejectedValue(new Error('network')), isCurrent: () => false }),
    )
    expect(outcome).toEqual({ kind: 'stale' })
  })

  it('reports stale when the view was disposed before a successful re-read landed', async () => {
    const outcome = await performTargetStep(5, deps({ isDisposed: () => true }))
    expect(outcome).toEqual({ kind: 'stale' })
  })

  it('reports stale when the view was disposed before a failed re-read landed', async () => {
    const outcome = await performTargetStep(
      5,
      deps({ getCurrent: vi.fn().mockRejectedValue(new Error('network')), isDisposed: () => true }),
    )
    expect(outcome).toEqual({ kind: 'stale' })
  })

  it('reports failed even when disposed, if setTarget itself is what failed', async () => {
    // Matches the pre-extraction behaviour exactly: the disposed check only guards the
    // re-read paths, so a failed setTarget still resolves as 'failed' (the view clears its
    // own pending value) even after unmount.
    const outcome = await performTargetStep(
      5,
      deps({ setTarget: vi.fn().mockRejectedValue(new Error('x')), isDisposed: () => true }),
    )
    expect(outcome).toEqual({ kind: 'failed' })
  })
})
