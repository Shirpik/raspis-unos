# Руководство по компонентам

## Использование утилитарных классов

### Кнопки
```vue
<button class="btn btn-primary">Основная кнопка</button>
<button class="btn btn-secondary">Вторичная кнопка</button>
<button class="btn btn-ghost">Ghost кнопка</button>
<button class="btn btn-danger">Опасное действие</button>
<button class="btn btn-sm">Маленькая</button>
<button class="btn btn-icon"><Icon /></button>
```

### Формы
```vue
<div class="form-group">
  <label class="form-label">Название</label>
  <input class="form-input" placeholder="Введите значение">
</div>

<select class="form-select">
  <option>Вариант 1</option>
</select>

<textarea class="form-textarea"></textarea>
```

### Карточки
```vue
<div class="card">
  <div class="card-title">Заголовок</div>
  <p>Содержимое карточки</p>
</div>
```

### Бейджи
```vue
<span class="badge badge-accent">Акцент</span>
<span class="badge badge-success">Успешно</span>
<span class="badge badge-error">Ошибка</span>
<span class="badge badge-warning">Предупреждение</span>
<span class="badge badge-muted">Нейтральный</span>
```

### Спиннеры
```vue
<span class="spinner"></span>
<span class="spinner spinner-sm"></span>
<span class="spinner spinner-lg"></span>
```

### Утилиты
```vue
<!-- Индикатор статуса -->
<span class="status-dot" style="color: var(--success)"></span>

<!-- Skeleton loader -->
<div class="skeleton" style="width: 200px; height: 20px;"></div>

<!-- Разделитель -->
<div class="divider"></div>

<!-- Градиентный текст -->
<h1 class="text-gradient">Заголовок</h1>

<!-- Размытая поверхность -->
<div class="blur-surface">Содержимое</div>
```

## Компоненты

### Modal
```vue
<Modal v-model="isOpen" title="Заголовок" size="normal">
  <p>Содержимое модального окна</p>
  <template #footer>
    <button class="btn btn-secondary" @click="isOpen = false">Отмена</button>
    <button class="btn btn-primary" @click="save">Сохранить</button>
  </template>
</Modal>
```

### Toast (из composable)
```vue
import { useToast } from '../composables/useToast'
const { showToast } = useToast()

showToast('Успешно сохранено', 'success')
showToast('Произошла ошибка', 'error')
showToast('Внимание!', 'warning')
showToast('Информация', 'info')
```

## CSS переменные

Доступные переменные из `style.css`:
- `--bg-primary`, `--bg-secondary`, `--bg-tertiary` - фоны
- `--text-primary`, `--text-secondary`, `--text-muted` - текст
- `--accent`, `--accent-hover`, `--accent-light` - акцентные цвета
- `--border`, `--border-strong` - границы
- `--success`, `--error`, `--warning` - состояния
- `--radius`, `--radius-sm`, `--radius-lg` - скругления
- `--transition` - длительность переходов

## Иконки

Используйте lucide-vue-next:
```vue
<script setup>
import { Check, X, AlertCircle } from 'lucide-vue-next'
</script>

<template>
  <Check :size="18" />
</template>
```
