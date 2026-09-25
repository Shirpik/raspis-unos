<template>
  <div class="home">
    <div class="home-inner">
      <div class="home-header">
        <CalendarRange :size="32" />
        <h1>Расписание</h1>
      </div>

      <div class="home-grid">
        <RouterLink to="/student" class="mode-card">
          <div class="mode-icon">
            <GraduationCap :size="24" />
          </div>
          <div class="mode-text">
            <span class="mode-title">Студентам</span>
            <span class="mode-desc">Просмотр расписания группы</span>
          </div>
          <div class="mode-arrow">
            <ArrowRight :size="18" />
          </div>
        </RouterLink>

        <RouterLink to="/teacher-portal" class="mode-card">
          <div class="mode-icon">
            <UserCheck :size="24" />
          </div>
          <div class="mode-text">
            <span class="mode-title">Преподавателям</span>
            <span class="mode-desc">Нагрузка и уведомления</span>
          </div>
          <div class="mode-arrow">
            <ArrowRight :size="18" />
          </div>
        </RouterLink>

        <RouterLink to="/schedule" class="mode-card">
          <div class="mode-icon">
            <Settings :size="24" />
          </div>
          <div class="mode-text">
            <span class="mode-title">Диспетчеру</span>
            <span class="mode-desc">Управление системой</span>
          </div>
          <div class="mode-arrow">
            <ArrowRight :size="18" />
          </div>
        </RouterLink>
      </div>
    </div>
  </div>
</template>

<script setup>
import { GraduationCap, UserCheck, Settings, CalendarRange, ArrowRight } from 'lucide-vue-next'
</script>

<style scoped>
.home {
  min-height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 24px;
  background: var(--bg-primary);
}

.home-inner {
  max-width: 800px;
  width: 100%;
}

.home-header {
  display: flex;
  align-items: center;
  gap: 16px;
  margin-bottom: 64px;
  justify-content: center;
}
.home-header svg {
  color: var(--accent);
}
.home-header h1 {
  font-size: 32px;
  font-weight: 600;
  color: var(--text-primary);
  letter-spacing: -0.03em;
  margin: 0;
}

.home-grid {
  display: grid;
  gap: 16px;
}

.mode-card {
  display: flex;
  align-items: center;
  gap: 20px;
  padding: 28px 32px;
  background: var(--bg-secondary);
  border: 1px solid var(--border);
  border-radius: var(--radius-lg);
  text-decoration: none;
  transition: all var(--transition);
  position: relative;
  overflow: hidden;
}

.mode-card::before {
  content: '';
  position: absolute;
  top: 0;
  left: 0;
  width: 4px;
  height: 100%;
  background: var(--accent);
  transform: scaleY(0);
  transition: transform var(--transition);
  transform-origin: bottom;
}

.mode-card:hover::before {
  transform: scaleY(1);
  transform-origin: top;
}

.mode-card:hover {
  border-color: var(--accent);
  transform: translateX(4px);
  background: var(--bg-tertiary);
}

.mode-card:active {
  transform: translateX(2px);
}

.mode-icon {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 48px;
  height: 48px;
  border-radius: var(--radius);
  background: rgba(251, 146, 60, 0.1);
  flex-shrink: 0;
  transition: all var(--transition);
}

.mode-card:hover .mode-icon {
  background: rgba(251, 146, 60, 0.15);
  transform: scale(1.05);
}

.mode-icon svg {
  color: var(--accent);
}

.mode-text {
  display: flex;
  flex-direction: column;
  gap: 6px;
  flex: 1;
}

.mode-title {
  font-size: 18px;
  font-weight: 600;
  color: var(--text-primary);
  letter-spacing: -0.01em;
}

.mode-desc {
  font-size: 14px;
  color: var(--text-muted);
}

.mode-arrow {
  display: flex;
  align-items: center;
  justify-content: center;
  color: var(--text-muted);
  transition: all var(--transition);
}

.mode-card:hover .mode-arrow {
  color: var(--accent);
  transform: translateX(4px);
}

@media (max-width: 640px) {
  .home {
    padding: 20px;
  }

  .home-header {
    margin-bottom: 48px;
  }

  .home-header svg {
    width: 28px;
    height: 28px;
  }

  .home-header h1 {
    font-size: 26px;
  }

  .mode-card {
    padding: 20px 24px;
    gap: 16px;
  }

  .mode-icon {
    width: 40px;
    height: 40px;
  }

  .mode-icon svg {
    width: 20px;
    height: 20px;
  }

  .mode-title {
    font-size: 16px;
  }

  .mode-desc {
    font-size: 13px;
  }
}
</style>
