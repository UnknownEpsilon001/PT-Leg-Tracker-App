<script setup lang="ts">
// หน้าความรู้: แสดงหัวข้อและบทความเรื่องข้อเข่าเสื่อม แตะเพื่ออ่านรายละเอียด
// Knowledge screen: lists knee-OA topics and articles; tap to read.
import { onMounted, ref } from 'vue'
import { useRouter } from 'vue-router'
import { loadKnowledgeContent, type KnowledgeSection } from '@/lib/content'

const router = useRouter()
const sections = ref<KnowledgeSection[]>([])
const loading = ref(true)
const contentError = ref(false)

onMounted(async () => {
  try {
    sections.value = (await loadKnowledgeContent()).sections
  } catch {
    contentError.value = true
  } finally {
    loading.value = false
  }
})
</script>

<template>
  <main class="page">
    <p v-if="loading">กำลังโหลดเนื้อหา…</p>
    <p v-else-if="contentError" class="error">
      โหลดเนื้อหาไม่สำเร็จ กรุณาตรวจสอบ public/editable-content/README.md
    </p>
    <template v-else>
      <section v-for="section in sections" :key="section.id">
        <h2>{{ section.title }}</h2>
        <button
          v-for="a in section.articles"
          :key="a.id"
          class="card item"
          @click="router.push({ name: 'article', params: { id: a.id } })"
        >
          <img v-if="a.image" :src="a.image" alt="" class="thumb" />
          <span class="title">{{ a.title }} ›</span>
        </button>
      </section>
    </template>
  </main>
</template>

<style scoped>
h2 {
  margin: 1rem 0 0.5rem;
}

.error {
  color: #b91c1c;
  font-weight: 600;
}

.item {
  width: 100%;
  text-align: left;
  font-size: 1.1rem;
  display: flex;
  align-items: center;
  gap: 0.75rem;
  justify-content: flex-start;
}

/* [TH] ภาพย่อหน้าหัวข้อ ให้ผู้สูงอายุจำหัวข้อได้จากภาพ */
/* Thumbnail so elderly users can recognise a topic by its picture. */
.thumb {
  flex-shrink: 0;
  width: 4.5rem;
  height: 3.4rem;
  object-fit: cover;
  border-radius: 0.5rem;
  background: var(--c-bg);
}

.title {
  overflow-wrap: anywhere;
}
</style>
