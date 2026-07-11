import { computed, ref } from 'vue'
import { defineStore } from 'pinia'
import type { Profile } from '@/types'
import { loadJson, saveJson } from '@/lib/storage'

const KEY = 'profile'

export const useProfileStore = defineStore('profile', () => {
  const profile = ref<Profile | null>(null)
  const hasProfile = computed(() => profile.value !== null)

  async function hydrate() {
    profile.value = await loadJson<Profile | null>(KEY, null)
  }

  async function save(p: Profile) {
    profile.value = p
    await saveJson(KEY, p)
  }

  return { profile, hasProfile, hydrate, save }
})
