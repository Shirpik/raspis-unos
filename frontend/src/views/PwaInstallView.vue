<template>
  <div class="install-page">
    <div class="install-container">
      <div class="install-header">
        <div class="header-icon">
          <Smartphone :size="48" />
        </div>
        <h1>Установка приложения</h1>
        <p>Следуйте инструкциям для вашей платформы</p>
      </div>

      <div class="platforms">
        <!-- iOS Safari -->
        <div class="platform-card">
          <div class="platform-header">
            <div class="platform-icon ios">
              <Apple :size="24" />
            </div>
            <h2>iOS (iPhone/iPad)</h2>
          </div>
          <ol class="install-steps">
            <li>
              <span class="step-number">1</span>
              <div class="step-content">
                <span>Откройте сайт в браузере Safari</span>
              </div>
            </li>
            <li>
              <span class="step-number">2</span>
              <div class="step-content">
                <span>Нажмите кнопку «Поделиться» <Share2 :size="16" class="inline-icon" /></span>
              </div>
            </li>
            <li>
              <span class="step-number">3</span>
              <div class="step-content">
                <span>Выберите «На экран "Домой"» или «Добавить на главный экран»</span>
              </div>
            </li>
            <li>
              <span class="step-number">4</span>
              <div class="step-content">
                <span>Нажмите «Добавить»</span>
              </div>
            </li>
          </ol>
        </div>

        <!-- Android Chrome -->
        <div class="platform-card">
          <div class="platform-header">
            <div class="platform-icon android">
              <Chrome :size="24" />
            </div>
            <h2>Android (Chrome)</h2>
          </div>
          <ol class="install-steps">
            <li>
              <span class="step-number">1</span>
              <div class="step-content">
                <span>Откройте сайт в браузере Chrome</span>
              </div>
            </li>
            <li>
              <span class="step-number">2</span>
              <div class="step-content">
                <span>Нажмите меню <MoreVertical :size="16" class="inline-icon" /> (три точки)</span>
              </div>
            </li>
            <li>
              <span class="step-number">3</span>
              <div class="step-content">
                <span>Выберите «Установить приложение» или «Добавить на главный экран»</span>
              </div>
            </li>
            <li>
              <span class="step-number">4</span>
              <div class="step-content">
                <span>Подтвердите установку</span>
              </div>
            </li>
          </ol>
        </div>

        <!-- Desktop -->
        <div class="platform-card">
          <div class="platform-header">
            <div class="platform-icon desktop">
              <Monitor :size="24" />
            </div>
            <h2>Компьютер (Chrome/Edge)</h2>
          </div>
          <ol class="install-steps">
            <li>
              <span class="step-number">1</span>
              <div class="step-content">
                <span>Откройте сайт в Chrome или Edge</span>
              </div>
            </li>
            <li>
              <span class="step-number">2</span>
              <div class="step-content">
                <span>Найдите иконку установки <Plus :size="16" class="inline-icon" /> в адресной строке</span>
              </div>
            </li>
            <li>
              <span class="step-number">3</span>
              <div class="step-content">
                <span>Нажмите «Установить» в появившемся окне</span>
              </div>
            </li>
          </ol>
        </div>
      </div>

      <div class="benefits-section">
        <h3>Преимущества приложения</h3>
        <div class="benefits-grid">
          <div class="benefit-card">
            <div class="benefit-icon">
              <Bell :size="24" />
            </div>
            <h4>Уведомления</h4>
            <p>Получайте push-уведомления о новостях и изменениях</p>
          </div>
          <div class="benefit-card">
            <div class="benefit-icon">
              <Zap :size="24" />
            </div>
            <h4>Быстрый доступ</h4>
            <p>Открывайте расписание одним нажатием</p>
          </div>
          <div class="benefit-card">
            <div class="benefit-icon">
              <Wifi :size="24" />
            </div>
            <h4>Офлайн режим</h4>
            <p>Просматривайте расписание без интернета</p>
          </div>
        </div>
      </div>

      <div class="back-link">
        <button @click="$router.back()" class="btn btn-secondary">
          <ArrowLeft :size="18" />
          <span>Вернуться назад</span>
        </button>
      </div>
    </div>
  </div>
</template>

<script setup>
import { Smartphone, Apple, Chrome, Monitor, Share2, MoreVertical, Plus, Bell, Zap, Wifi, ArrowLeft } from 'lucide-vue-next'
</script>

<style scoped>
.install-page {
  min-height: 100vh;
  padding: 32px 20px;
  background: var(--bg-primary);
}

.install-container {
  max-width: 900px;
  margin: 0 auto;
}

.install-header {
  text-align: center;
  margin-bottom: 48px;
}

.header-icon {
  width: 80px;
  height: 80px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: var(--accent-light);
  border-radius: var(--radius-lg);
  color: var(--accent);
  margin: 0 auto 24px;
}

.install-header h1 {
  font-size: clamp(28px, 5vw, 36px);
  font-weight: 600;
  color: var(--text-primary);
  margin: 0 0 12px;
  letter-spacing: -0.02em;
}

.install-header p {
  font-size: 16px;
  color: var(--text-muted);
  margin: 0;
}

.platforms {
  display: flex;
  flex-direction: column;
  gap: 24px;
  margin-bottom: 48px;
}

.platform-card {
  background: var(--bg-secondary);
  border: 1px solid var(--border);
  border-radius: var(--radius-lg);
  padding: 24px;
  transition: all var(--transition);
}

.platform-card:hover {
  border-color: var(--border-strong);
}

.platform-header {
  display: flex;
  align-items: center;
  gap: 16px;
  margin-bottom: 20px;
}

.platform-icon {
  width: 48px;
  height: 48px;
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: var(--radius);
  flex-shrink: 0;
}

.platform-icon.ios {
  background: rgba(0, 122, 255, 0.1);
  color: #007aff;
}

.platform-icon.android {
  background: rgba(52, 168, 83, 0.1);
  color: #34a853;
}

.platform-icon.desktop {
  background: rgba(251, 146, 60, 0.1);
  color: var(--accent);
}

.platform-header h2 {
  font-size: 20px;
  font-weight: 600;
  color: var(--text-primary);
  margin: 0;
}

.install-steps {
  list-style: none;
  display: flex;
  flex-direction: column;
  gap: 16px;
  counter-reset: step;
}

.install-steps li {
  display: flex;
  gap: 16px;
  align-items: flex-start;
}

.step-number {
  width: 32px;
  height: 32px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: var(--bg-tertiary);
  border: 1px solid var(--border);
  border-radius: 50%;
  color: var(--accent);
  font-weight: 600;
  font-size: 14px;
  flex-shrink: 0;
}

.step-content {
  flex: 1;
  padding-top: 6px;
}

.step-content span {
  font-size: 15px;
  color: var(--text-secondary);
  line-height: 1.6;
  display: inline-flex;
  align-items: center;
  gap: 6px;
  flex-wrap: wrap;
}

.inline-icon {
  vertical-align: middle;
  color: var(--accent);
}

.benefits-section {
  margin-bottom: 48px;
}

.benefits-section h3 {
  font-size: 24px;
  font-weight: 600;
  color: var(--text-primary);
  margin: 0 0 24px;
  text-align: center;
}

.benefits-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
  gap: 20px;
}

.benefit-card {
  background: var(--bg-secondary);
  border: 1px solid var(--border);
  border-radius: var(--radius-lg);
  padding: 24px;
  text-align: center;
  transition: all var(--transition);
}

.benefit-card:hover {
  border-color: var(--border-strong);
  transform: translateY(-2px);
}

.benefit-icon {
  width: 56px;
  height: 56px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: var(--accent-light);
  border-radius: var(--radius);
  color: var(--accent);
  margin: 0 auto 16px;
}

.benefit-card h4 {
  font-size: 18px;
  font-weight: 600;
  color: var(--text-primary);
  margin: 0 0 8px;
}

.benefit-card p {
  font-size: 14px;
  color: var(--text-muted);
  line-height: 1.6;
  margin: 0;
}

.back-link {
  text-align: center;
}

@media (max-width: 768px) {
  .install-page {
    padding: 24px 16px;
  }

  .platform-card {
    padding: 20px;
  }

  .benefits-grid {
    grid-template-columns: 1fr;
  }
}
</style>
