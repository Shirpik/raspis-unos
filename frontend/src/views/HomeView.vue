<template>
  <div class="home">
    <div class="home-card">
      <div class="home-logo">
        <svg width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round">
          <rect x="3" y="4" width="18" height="18" rx="2" ry="2"/>
          <path d="M16 2v4M8 2v4M3 10h18"/>
        </svg>
        <span class="home-logo-text">Расписание</span>
      </div>
      <p class="home-subtitle">Выберите режим просмотра</p>
      <div class="home-actions">
        <RouterLink to="/student" class="entry-btn entry-student">
          <GraduationCap :size="24" class="entry-icon" />
          <div class="entry-content">
            <span class="entry-label">Студентам</span>
            <span class="entry-desc">Посмотреть расписание своей группы</span>
          </div>
          <ChevronRight :size="20" class="entry-arrow" />
        </RouterLink>
        <RouterLink to="/teacher-portal" class="entry-btn entry-teacher">
          <UserCheck :size="24" class="entry-icon" />
          <div class="entry-content">
            <span class="entry-label">Преподавателям</span>
            <span class="entry-desc">Расписание, учет часов и уведомления</span>
          </div>
          <ChevronRight :size="20" class="entry-arrow" />
        </RouterLink>
        <RouterLink to="/schedule" class="entry-btn entry-admin">
          <Wrench :size="24" class="entry-icon" />
          <div class="entry-content">
            <span class="entry-label">Диспетчеру</span>
            <span class="entry-desc">Управление расписанием</span>
          </div>
          <ChevronRight :size="20" class="entry-arrow" />
        </RouterLink>
      </div>
    </div>
  </div>
</template>

<script setup>
import { GraduationCap, UserCheck, Wrench, ChevronRight } from 'lucide-vue-next'
</script>

<style scoped>
.home {
  min-height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 24px;
  background:
    radial-gradient(circle at 20% 30%, rgba(251, 146, 60, 0.03) 0%, transparent 50%),
    radial-gradient(circle at 80% 70%, rgba(251, 146, 60, 0.02) 0%, transparent 50%),
    var(--bg-primary);
}

.home-card {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 28px;
  max-width: 520px;
  width: 100%;
  background: var(--bg-secondary);
  border: 1px solid var(--border);
  border-radius: var(--radius-lg, 16px);
  padding: 52px 44px;
  box-shadow: 0 8px 40px rgba(0, 0, 0, 0.5);
}

.home-logo {
  display: flex;
  align-items: center;
  gap: 12px;
  color: var(--text-primary);
}
.home-logo svg {
  color: var(--accent);
}
.home-logo-text {
  font-size: 24px;
  font-weight: 600;
  color: var(--text-primary);
  letter-spacing: -0.02em;
}

.home-subtitle {
  margin: -8px 0 0;
  font-size: 14px;
  color: var(--text-muted);
  font-weight: 500;
}

.home-actions {
  display: flex;
  flex-direction: column;
  gap: 12px;
  width: 100%;
}

.entry-btn {
  display: flex;
  align-items: center;
  gap: 16px;
  padding: 20px 20px;
  border-radius: var(--radius);
  border: 1px solid var(--border);
  text-decoration: none;
  transition: all 0.2s cubic-bezier(0.4, 0, 0.2, 1);
  background: var(--bg-tertiary);
  position: relative;
  overflow: hidden;
}
.entry-btn::before {
  content: '';
  position: absolute;
  inset: 0;
  background: linear-gradient(135deg, var(--accent-light) 0%, transparent 60%);
  opacity: 0;
  transition: opacity 0.3s ease;
}
.entry-btn:hover::before {
  opacity: 1;
}
.entry-btn:hover {
  border-color: var(--accent);
  transform: translateY(-2px);
  box-shadow: 0 8px 20px rgba(251, 146, 60, 0.15);
}
.entry-btn:active {
  transform: translateY(0);
}

.entry-icon {
  flex-shrink: 0;
  color: var(--text-secondary);
  transition: color 0.2s ease;
  z-index: 1;
}
.entry-btn:hover .entry-icon {
  color: var(--accent);
}

.entry-content {
  display: flex;
  flex-direction: column;
  gap: 4px;
  flex: 1;
  z-index: 1;
}
.entry-label {
  font-size: 16px;
  font-weight: 600;
  color: var(--text-primary);
  letter-spacing: -0.01em;
  transition: color 0.2s ease;
}
.entry-desc {
  font-size: 13px;
  color: var(--text-muted);
  line-height: 1.4;
}

.entry-arrow {
  flex-shrink: 0;
  color: var(--text-muted);
  opacity: 0;
  transform: translateX(-8px);
  transition: all 0.2s ease;
  z-index: 1;
}
.entry-btn:hover .entry-arrow {
  opacity: 1;
  transform: translateX(0);
  color: var(--accent);
}

@media (max-width: 580px) {
  .home-card { padding: 40px 28px; max-width: 100%; }
  .entry-btn { padding: 18px 18px; }
  .home-logo-text { font-size: 22px; }
}
</style>
