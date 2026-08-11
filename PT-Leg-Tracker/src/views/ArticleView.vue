<script setup lang="ts">
// หน้าบทความ: แสดงเนื้อหาบทความตาม id จาก URL (ปรับขนาด/ความคืบหน้าการอ่านเก็บในเครื่อง)
// Article screen: renders the article matching the URL id.
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
      <img v-if="article.image" :src="article.image" :alt="article.imageAlt" class="illus" />
      <p class="body">{{ article.body }}</p>
    </template>
    <p v-else>ไม่พบเนื้อหา</p>
  </main>
</template>

<style scoped>
h1 {
  margin-bottom: 1rem;
}

.illus {
  display: block;
  width: 100%;
  margin-bottom: 1rem;
  border: 1.5px solid var(--c-border);
  border-radius: var(--radius-card);
  background: var(--c-card);
}

.body {
  font-size: 1.1rem;
  white-space: pre-line;
}
</style>
