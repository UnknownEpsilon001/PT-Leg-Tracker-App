<script setup lang="ts">
// หน้าบทความ: แสดงเนื้อหาบทความตาม id จาก URL (ปรับขนาด/ความคืบหน้าการอ่านเก็บในเครื่อง)
// Article screen: renders the article matching the URL id.
import { computed, onMounted, ref, watchEffect } from 'vue'
import { useRoute } from 'vue-router'
import { loadKnowledgeContent, type KnowledgeContent } from '@/lib/content'
import { loadJson, saveJson } from '@/lib/storage'

const route = useRoute()
const content = ref<KnowledgeContent | null>(null)
const loading = ref(true)
const contentError = ref(false)
const article = computed(() =>
  content.value?.sections
    .flatMap((section) => section.articles)
    .find((item) => item.id === route.params.id),
)

onMounted(async () => {
  try {
    content.value = await loadKnowledgeContent()
  } catch {
    contentError.value = true
  } finally {
    loading.value = false
  }
})

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
    <p v-if="loading">กำลังโหลดเนื้อหา…</p>
    <p v-else-if="contentError" class="error">
      โหลดเนื้อหาไม่สำเร็จ กรุณาตรวจสอบ public/editable-content/README.md
    </p>
    <template v-else-if="article">
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

.error {
  color: #b91c1c;
  font-weight: 600;
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
