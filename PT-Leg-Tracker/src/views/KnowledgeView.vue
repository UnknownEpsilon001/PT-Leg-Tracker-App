<script setup lang="ts">
import { useRouter } from 'vue-router'
import content from '@/content/knowledge.json'

const router = useRouter()
const sections = content.sections
</script>

<template>
  <main class="page">
    <section v-for="section in sections" :key="section.id">
      <h2>{{ section.title }}</h2>
      <button
        v-for="a in section.articles"
        :key="a.id"
        class="card item"
        @click="router.push({ name: 'article', params: { id: a.id } })"
      >
        {{ a.title }} ›
      </button>
    </section>
  </main>
</template>

<style scoped>
h2 {
  margin: 1rem 0 0.5rem;
}

.item {
  width: 100%;
  text-align: left;
  font-size: 1.1rem;
  display: block;
}
</style>
