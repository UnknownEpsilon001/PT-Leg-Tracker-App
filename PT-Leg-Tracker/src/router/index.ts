import { createRouter, createWebHistory } from 'vue-router'
import HomeView from '../views/HomeView.vue'
import { useProfileStore } from '@/stores/profile'

const router = createRouter({
  history: createWebHistory(import.meta.env.BASE_URL),
  routes: [
    { path: '/', name: 'home', component: HomeView },
    {
      path: '/setup',
      name: 'setup',
      component: () => import('../views/SetupView.vue'),
      meta: { title: 'ลงทะเบียน' },
    },
    {
      path: '/knowledge',
      name: 'knowledge',
      component: () => import('../views/KnowledgeView.vue'),
      meta: { title: 'ความรู้' },
    },
    {
      path: '/knowledge/:id',
      name: 'article',
      component: () => import('../views/ArticleView.vue'),
      meta: { title: 'ความรู้' },
    },
    {
      path: '/guide',
      name: 'guide',
      component: () => import('../views/DeviceGuideView.vue'),
      meta: { title: 'วิธีใช้เครื่อง' },
    },
    {
      path: '/session',
      name: 'session',
      component: () => import('../views/SessionView.vue'),
      meta: { title: 'ออกกำลังกาย' },
    },
  ],
})

router.beforeEach((to) => {
  const store = useProfileStore()
  if (!store.hasProfile && to.name !== 'setup') return { name: 'setup' }
})

export default router
