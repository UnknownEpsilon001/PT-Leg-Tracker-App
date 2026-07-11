<script setup lang="ts">
import { computed, watchEffect } from 'vue'
import { useRoute } from 'vue-router'
import content from '@/content/knowledge.json'
import { loadJson, saveJson } from '@/lib/storage'

const route = useRoute()
const article = computed(() =>
  content.sections.flatMap((s) => s.articles).find((a) => a.id === route.params.id),
)

watchEffect(async () => {
  const id = article.value?.id
  if (!id) return
  const progress = await loadJson<Record<string, boolean>>('contentProgress', {})
  if (!progress[id]) {
    progress[id] = true
    await saveJson('contentProgress', progress)
  }
})
</script>

<template>
  <main class="page">
    <template v-if="article">
      <h1>{{ article.title }}</h1>
      <p class="body">{{ article.body }}</p>
    </template>
    <p v-else>ไม่พบเนื้อหา</p>
  </main>
</template>

<style scoped>
h1 {
  margin-bottom: 1rem;
}

.body {
  font-size: 1.1rem;
  white-space: pre-line;
}
</style>
