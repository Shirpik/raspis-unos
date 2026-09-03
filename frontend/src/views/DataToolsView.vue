<template>
  <div class="page">
    <div class="page-header"><div><h1 class="page-title">📊 Данные и контроль</h1><p class="subtitle">Импорт, проверка конфликтов, часы, недоступность и откат изменений.</p></div><button class="btn btn-secondary" :disabled="tabLoading" @click="refreshAll">{{ tabLoading ? 'Обновляю…' : '↻ Обновить' }}</button></div>
    <div class="tabs">
      <button v-for="item in tabs" :key="item.id" :class="['tab', {active:tab===item.id}]" @click="tab=item.id">{{ item.label }}</button>
    </div>
    <div v-if="tabLoading" class="notice loading-notice">Загружаю данные выбранного раздела…</div>

    <section v-if="tab==='transfer'" class="section">
      <div class="transfer-grid">
        <article class="card transfer-card">
          <div class="transfer-icon">⬇️</div>
          <div>
            <h2>Скачать полный пакет</h2>
            <p class="help">Для переноса с локального компьютера на сайт или создания полной резервной копии.</p>
          </div>
          <ul class="transfer-list">
            <li>группы, преподаватели, занятия и аудитории;</li>
            <li>ограничения, настройки решателя и недоступность;</li>
            <li>автоматическое, ручное и опубликованное расписание;</li>
            <li>замены, корректировки учёта и отчёты решателя.</li>
          </ul>
          <button class="btn btn-primary transfer-button" :disabled="transferExporting" @click="exportTransferBundle">
            {{ transferExporting ? 'Собираю пакет…' : 'Скачать всё одним файлом' }}
          </button>
        </article>

        <article class="card transfer-card">
          <div class="transfer-icon">⬆️</div>
          <div>
            <h2>Загрузить пакет на сайт</h2>
            <p class="help">Выберите файл <code>*.raspis.json</code>, скачанный из desktop-приложения.</p>
          </div>
          <label class="file-drop transfer-file"><input type="file" accept=".json,application/json" @change="selectTransferBundle" /><span>📦 {{ transferFile?.name || 'Выбрать полный пакет' }}</span></label>
          <div v-if="transferError" class="notice error">{{ transferError }}</div>
          <template v-if="transferBundle">
            <div class="metric-row transfer-metrics">
              <div class="metric"><span>Группы</span><strong>{{ transferSummary.groups }}</strong></div>
              <div class="metric"><span>Преподаватели</span><strong>{{ transferSummary.teachers }}</strong></div>
              <div class="metric"><span>Занятия</span><strong>{{ transferSummary.lessons }}</strong></div>
              <div class="metric"><span>Аудитории</span><strong>{{ transferSummary.rooms }}</strong></div>
              <div class="metric"><span>Замены</span><strong>{{ transferSummary.substitutions }}</strong></div>
            </div>
            <label class="form-group"><span>Какое расписание сделать основным на сайте</span><select v-model="transferPrimary" class="form-select"><option v-for="item in transferScheduleOptions" :key="item.value" :value="item.value">{{ item.label }}</option></select></label>
            <label class="transfer-check"><input v-model="transferPublish" type="checkbox" /> <span>Сразу опубликовать выбранное расписание для студентов</span></label>
            <button class="btn btn-success transfer-button" :disabled="transferImporting || !transferPrimary" @click="transferConfirm=true">Проверить и загрузить на сайт</button>
          </template>
        </article>
      </div>
      <div class="notice transfer-note"><strong>Учёт часов переносится полностью.</strong> Он автоматически пересчитывается на сайте по выбранному расписанию, нагрузке, заменам и корректировкам. Пароли из файла не импортируются.</div>
    </section>

    <section v-if="tab==='import'" class="card section">
      <h2>Импорт «Вклеек» из Excel</h2>
      <p class="help">Сначала строится предпросмотр. Текущие настройки, аудитории и недоступность сохраняются. Перед заменой сервер автоматически создаёт версию для отката.</p>
      <div class="import-controls">
        <label class="file-drop"><input type="file" accept=".xlsx,.xls" @change="selectFile" /><span>📎 {{ selectedFile?.name || 'Выбрать Excel-файл' }}</span></label>
        <select v-model.number="semester" class="form-select"><option :value="1">Осень (1/3/5/7 семестр)</option><option :value="2">Весна (2/4/6/8 семестр)</option></select>
        <button class="btn btn-primary" :disabled="!selectedFile || importing" @click="previewImport">{{ importing ? 'Читаю…' : 'Построить предпросмотр' }}</button>
      </div>
      <div v-if="preview" class="preview">
        <div class="metric-row">
          <div v-for="(v,k) in preview.changes" :key="k" class="metric"><span>{{ labels[k] }}</span><strong>{{ v.before }} → {{ v.after }}</strong><small :class="v.after-v.before < 0 ? 'negative':'positive'">{{ signed(v.after-v.before) }}</small></div>
          <div class="metric"><span>Листов</span><strong>{{ preview.sheetCount }}</strong><small>прочитано</small></div>
        </div>
        <div v-if="preview.errors.length" class="notice error"><strong>Ошибки — импорт заблокирован</strong><div v-for="e in preview.errors" :key="e.sheet">{{ e.sheet }}: {{ e.message }}</div></div>
        <div v-if="preview.warnings.length" class="notice warning"><strong>Предупреждения</strong><div v-for="w in preview.warnings" :key="w">{{ w }}</div></div>
        <div v-if="preview.audit" :class="['notice', preview.audit.ok ? 'success' : 'error']">Предварительный аудит: {{ preview.audit.summary.errors }} ошибок, {{ preview.audit.summary.warnings }} предупреждений, {{ preview.audit.summary.info }} замечаний.</div>
        <button class="btn btn-success" :disabled="preview.errors.length || committing" @click="commitImport">{{ committing ? 'Сохраняю…' : 'Применить импорт' }}</button>
      </div>
    </section>

    <section v-if="tab==='audit'" class="section">
      <div class="metric-row" v-if="audit.summary"><div class="metric error-box"><span>Ошибки</span><strong>{{ audit.summary.errors }}</strong></div><div class="metric warning-box"><span>Предупреждения</span><strong>{{ audit.summary.warnings }}</strong></div><div class="metric"><span>Информация</span><strong>{{ audit.summary.info }}</strong></div><div class="metric"><span>Занятия</span><strong>{{ audit.summary.lessons }}</strong></div></div>
      <div v-if="!audit.issues?.length" class="notice success">Критических конфликтов входных данных не найдено.</div>
      <div v-else class="table-wrap"><table><thead><tr><th>Уровень</th><th>Код</th><th>Проблема</th><th>Объект</th></tr></thead><tbody><tr v-for="(i,n) in audit.issues" :key="n"><td><span :class="['badge','badge-'+severity(i.severity)]">{{ severityLabel(i.severity) }}</span></td><td><code>{{ i.code }}</code></td><td>{{ i.message }}</td><td>{{ i.entity_type || '—' }} {{ i.entity_id ?? '' }}</td></tr></tbody></table></div>
    </section>

    <section v-if="tab==='hours'" class="section">
      <div class="section-head"><div><h2>План, расписание и зачёт часов</h2><p class="help">Недельные колонки соответствуют структуре старой таблицы учёта.</p></div><div class="export-actions"><button class="btn btn-primary" :disabled="templateExporting" @click="exportReferenceHours">{{ templateExporting ? 'Собираю файл…' : 'Экспорт по образцу' }}</button><button class="btn btn-secondary" @click="exportHours">Универсальный Excel</button></div></div>
      <div v-if="hours.schedule_found===false" class="notice warning">Расписание ещё не сгенерировано — в колонке «поставлено» будет 0.</div>
      <div class="subtabs"><button :class="{active:hoursMode==='groups'}" @click="hoursMode='groups'">По группам</button><button :class="{active:hoursMode==='teachers'}" @click="hoursMode='teachers'">По преподавателям</button><button :class="{active:hoursMode==='lessons'}" @click="hoursMode='lessons'">По занятиям</button></div>
      <div class="metric-row hours-summary"><div class="metric"><span>План</span><strong>{{ hoursSummary.planned }} ч</strong></div><div class="metric"><span>Поставлено</span><strong>{{ hoursSummary.scheduled }} ч</strong></div><div class="metric"><span>Зачтено</span><strong>{{ hoursSummary.credited }} ч</strong></div><div class="metric"><span>Осталось</span><strong :class="{negative:hoursSummary.remaining>0}">{{ hoursSummary.remaining }} ч</strong></div><div class="metric warning-box"><span>Без поставленных пар</span><strong>{{ hoursSummary.notStarted }}</strong></div><div class="metric error-box"><span>Перегруз</span><strong>{{ hoursSummary.overrun }}</strong></div></div>
      <div class="hours-controls card"><input v-model.trim="hoursSearch" class="form-input" placeholder="Поиск по преподавателю, группе или дисциплине" /><select v-model="hoursStatus" class="form-select"><option value="all">Все состояния</option><option value="not_started">Не начато</option><option value="in_progress">Есть остаток</option><option value="completed">Выполнено</option><option value="overrun">Перегруз</option></select><select v-model="hoursSort" class="form-select"><option value="name">По названию</option><option value="remaining">Сначала большой остаток</option><option value="scheduled">Сначала больше поставлено</option><option value="progress">Сначала высокий процент</option></select><select v-model.number="hoursPageSize" class="form-select page-size"><option :value="25">25 строк</option><option :value="50">50 строк</option><option :value="100">100 строк</option><option :value="200">200 строк</option></select><span class="result-count">{{ hoursRange }}</span></div>
      <div v-if="!hourRows.length" class="notice warning">По выбранным фильтрам строк нет.</div>
      <div v-else class="table-wrap"><table><thead><tr><th>Наименование</th><th>План, ч</th><th>Поставлено, ч</th><th>Зачтено, ч</th><th v-if="hoursMode==='teachers'">Замены +/−</th><th>Осталось, ч</th><th>Выполнение</th><th v-for="w in hoursMode!=='lessons' ? hours.weeks : []" :key="w.index">Нед. {{ w.index }}<small>{{ w.from }}<br>{{ w.to }}</small></th></tr></thead><tbody><tr v-for="row in pagedHourRows" :key="hourKey(row)" :class="hourStatus(row)"><td><strong>{{ hourName(row) }}</strong><button v-if="hoursMode!=='lessons'" class="workload-link" @click="openWorkload(row)">Состав нагрузки</button><small v-if="row.teacher_name&&hoursMode!=='teachers'">{{ row.teacher_name }}</small></td><td>{{ row.planned_hours }}</td><td><button class="occurrence-button" :disabled="!row.scheduled_occurrences?.length" @click="openOccurrences(row,'scheduled')"><strong>{{ row.scheduled_hours }}</strong><small>{{ row.scheduled_occurrences?.length ? `Даты: ${row.scheduled_occurrences.length}` : 'дат нет' }}</small></button></td><td><button class="occurrence-button" :disabled="hoursMode!=='teachers'||!row.credited_occurrences?.length" @click="openOccurrences(row,'credited')"><strong>{{ row.credited_hours ?? row.scheduled_hours }}</strong><small v-if="hoursMode==='teachers'">{{ row.credited_occurrences?.length ? `Факт: ${row.credited_occurrences.length}` : 'дат нет' }}</small></button></td><td v-if="hoursMode==='teachers'"><span class="positive">+{{ row.substitution_in_hours||0 }}</span> / <span class="negative">−{{ row.substitution_out_hours||0 }}</span><small v-if="row.adjustment_hours">корр.: {{ signed(row.adjustment_hours) }}</small></td><td :class="{negative:row.remaining_hours>0,positive:row.remaining_hours===0}">{{ row.remaining_hours }}</td><td><div class="progress"><span :style="{width:percent(row)+'%'}" /></div><small>{{ percent(row) }}%</small></td><td v-for="(w,i) in hoursMode!=='lessons' ? hours.weeks : []" :key="w.index"><button v-if="row.weekly_hours?.[i]" class="week-button" @click="openOccurrences(row,'scheduled',w.index)">{{ row.weekly_hours[i] }}</button><span v-else>0</span></td></tr></tbody></table></div>
      <div v-if="hourRows.length>hoursPageSize" class="pagination"><button class="btn btn-secondary btn-sm" :disabled="hoursPage===1" @click="hoursPage--">← Назад</button><span>Страница {{ hoursPage }} из {{ hoursPageCount }}</span><button class="btn btn-secondary btn-sm" :disabled="hoursPage===hoursPageCount" @click="hoursPage++">Вперёд →</button></div>
    </section>

    <section v-if="tab==='substitutions'" class="section">
      <div class="section-head"><div><h2>Журнал замен</h2><p class="help">Активная замена переносит зачёт часов с отсутствующего преподавателя на заменяющего, не меняя план из «вклеек».</p></div><a class="btn btn-secondary" :href="api.substitutions.csvUrl" download>Скачать CSV</a></div>
      <div class="card replacement-form">
        <select v-model.number="replacementForm.lesson_id" class="form-select" @change="syncAbsentTeacher"><option :value="-1">Выберите занятие</option><option v-for="l in store.lessons" :key="l.id" :value="l.id">{{ groupName(l.group) }} — {{ l.name }}</option></select>
        <input v-model="replacementForm.date" type="date" class="form-input" />
        <select v-model.number="replacementForm.slot" class="form-select"><option v-for="n in 7" :key="n" :value="n">{{ n }} пара</option></select>
        <select v-model.number="replacementForm.absent_teacher" class="form-select"><option v-for="t in store.teachers" :key="t.id" :value="t.id">Нет: {{ t.name }}</option></select>
        <select v-model.number="replacementForm.substitute_teacher" class="form-select"><option v-for="t in store.teachers" :key="t.id" :value="t.id">Замена: {{ t.name }}</option></select>
        <input v-model.number="replacementForm.hours" type="number" min="1" max="8" class="form-input" placeholder="Часы" />
        <input v-model="replacementForm.reason" class="form-input" placeholder="Причина" />
        <button class="btn btn-primary" @click="addSubstitution">Добавить замену</button>
      </div>
      <div v-if="!store.substitutions.length" class="empty-state card"><h3>Замен пока нет</h3><p>Добавьте замену для конкретной даты, пары и занятия.</p></div>
      <div v-else class="table-wrap"><table><thead><tr><th>Дата/пара</th><th>Занятие</th><th>Отсутствует</th><th>Заменяет</th><th>Часы</th><th>Причина</th><th></th></tr></thead><tbody><tr v-for="s in store.substitutions" :key="s.id"><td>{{ s.date }} · {{ s.slot }}</td><td>{{ lessonName(s.lesson_id) }}</td><td>{{ teacherName(s.absent_teacher) }}</td><td>{{ teacherName(s.substitute_teacher) }}</td><td>{{ s.hours }}</td><td>{{ s.reason || '—' }}</td><td><button class="btn btn-ghost btn-sm danger" @click="removeSubstitution(s)">Удалить</button></td></tr></tbody></table></div>
    </section>

    <section v-if="tab==='occupancy'" class="section">
      <div class="section-head"><div><h2>Занятость преподавателей</h2><p class="help">Матрица «преподаватель × день × пара» с учётом замен.</p></div><select v-model="occupancyWeek" class="form-select date-filter"><option v-for="w in occupancyWeeks" :key="w" :value="w">Неделя с {{ w }}</option></select></div>
      <div v-if="!occupancyBlocks.length" class="notice warning">На выбранной неделе занятий нет.</div>
      <div v-else class="table-wrap occupancy-table"><table><thead><tr><th>Преподаватель</th><th>Пара</th><th v-for="day in 6" :key="day">{{ weekdayName(day) }}</th></tr></thead><tbody><template v-for="block in occupancyBlocks" :key="block.teacher_id"><tr v-for="slot in occupancySlots" :key="`${block.teacher_id}-${slot}`"><td :class="{teacherStart:slot===0}"><strong v-if="slot===0">{{ block.teacher_name }}</strong></td><td>{{ slot }}</td><td v-for="day in 6" :key="day"><div v-for="e in occupancyCell(block,day,slot)" :key="`${e.lesson_id}-${e.group_id}`" :class="['busy-cell',{replacement:e.is_substitution}]"><strong>{{ e.group_name }}</strong><span>{{ e.lesson_name }}</span><small>{{ e.room || 'кабинет не задан' }}<template v-if="e.is_substitution"> · замена</template></small></div></td></tr></template></tbody></table></div>
    </section>

    <section v-if="tab==='unavailable'" class="section">
      <div class="card inline-form"><select v-model.number="unavailableForm.teacher" class="form-select"><option v-for="t in store.teachers" :key="t.id" :value="t.id">{{ t.name }}</option></select><input v-model="unavailableForm.from" type="date" class="form-input" /><input v-model="unavailableForm.to" type="date" class="form-input" /><input v-model="unavailableForm.reason" class="form-input" placeholder="Причина (необязательно)" /><button class="btn btn-primary" @click="addUnavailable">Добавить</button></div>
      <div v-if="!store.teacherUnavailable.length" class="empty-state card"><h3>Нет ограничений преподавателей</h3><p>Укажите отпуск, больничный, методический день или любую недоступность диапазоном дат.</p></div>
      <div v-else class="table-wrap"><table><thead><tr><th>Преподаватель</th><th>С</th><th>По</th><th>Причина</th><th></th></tr></thead><tbody><tr v-for="u in store.teacherUnavailable" :key="u.id"><td>{{ teacherName(u.teacher) }}</td><td>{{ u.from }}</td><td>{{ u.to }}</td><td>{{ u.reason || '—' }}</td><td><button class="btn btn-ghost btn-sm danger" @click="removeUnavailable(u)">Удалить</button></td></tr></tbody></table></div>
    </section>

    <section v-if="tab==='history'" class="section">
      <div class="notice">Хранятся последние 50 изменений. Откат также создаёт новую резервную версию.</div>
      <div v-if="!versions.length" class="empty-state card"><h3>История пока пуста</h3><p>Первая версия появится после изменения данных.</p></div>
      <div v-else class="table-wrap"><table><thead><tr><th>Дата</th><th>Причина</th><th>Размер</th><th></th></tr></thead><tbody><tr v-for="v in versions" :key="v.filename"><td>{{ formatDate(v.created_at) }}</td><td>{{ v.reason || 'Изменение данных' }}</td><td>{{ Math.round((v.size||0)/1024) }} КБ</td><td><button class="btn btn-ghost btn-sm" @click="restore(v)">↶ Откатить</button></td></tr></tbody></table></div>
    </section>

    <Modal v-model="transferConfirm" title="Применить полный пакет?">
      <div class="notice warning"><strong>Текущая база и расписание на этом сервере будут заменены.</strong><br />Перед применением сервер автоматически сохранит полный резервный пакет.</div>
      <div v-if="transferBundle" class="transfer-confirm-summary">
        <p><strong>Файл:</strong> {{ transferFile?.name }}</p>
        <p><strong>Экспортирован:</strong> {{ formatDate(transferBundle.exported_at) }}</p>
        <p><strong>Основное расписание:</strong> {{ transferScheduleLabel(transferPrimary) }}</p>
        <p><strong>Публикация студентам:</strong> {{ transferPublish ? 'да' : 'нет' }}</p>
      </div>
      <template #footer><button class="btn btn-secondary" :disabled="transferImporting" @click="transferConfirm=false">Отмена</button><button class="btn btn-success" :disabled="transferImporting" @click="applyTransferBundle">{{ transferImporting ? 'Проверяю и применяю…' : 'Создать резервную копию и применить' }}</button></template>
    </Modal>

    <Modal v-model="workloadModal" size="wide" :title="`Состав нагрузки — ${workloadName}`">
      <div class="workload-summary"><div><span>Дисциплин</span><strong>{{ workloadSummary.count }}</strong></div><div><span>План</span><strong>{{ workloadSummary.planned }} ч</strong></div><div><span>Поставлено</span><strong>{{ workloadSummary.scheduled }} ч</strong></div><div><span>Осталось</span><strong>{{ workloadSummary.remaining }} ч</strong></div><div><span>Ещё не начато</span><strong>{{ workloadSummary.notStarted }}</strong></div></div>
      <div class="workload-controls"><input v-model.trim="workloadSearch" class="form-input" placeholder="Поиск по дисциплине, группе или преподавателю" /><select v-model="workloadStatus" class="form-select"><option value="all">Все дисциплины</option><option value="not_started">Ещё не начаты</option><option value="in_progress">Частично проведены</option><option value="completed">План выполнен</option><option value="overrun">Сверх плана</option></select></div>
      <div v-if="!workloadLessons.length" class="notice warning">По выбранным фильтрам дисциплин нет.</div>
      <div v-else class="table-wrap workload-table"><table><thead><tr><th>Дисциплина</th><th>{{ workloadMode==='groups' ? 'Преподаватель' : 'Группа' }}</th><th>План</th><th>Поставлено</th><th>Осталось</th><th>Выполнение</th><th>Конкретные пары</th></tr></thead><tbody><tr v-for="lesson in workloadLessons" :key="lesson.lesson_id" :class="hourStatus(lesson)"><td><strong>{{ lesson.name }}</strong></td><td>{{ workloadMode==='groups' ? lesson.teacher_name : lesson.group_name }}</td><td>{{ lesson.planned_hours }} ч</td><td>{{ lesson.scheduled_hours }} ч</td><td :class="{negative:lesson.remaining_hours>0,positive:lesson.remaining_hours===0}">{{ lesson.remaining_hours }} ч</td><td><div class="progress"><span :style="{width:percent(lesson)+'%'}" /></div><small>{{ percent(lesson) }}%</small></td><td><button class="occurrence-button" :disabled="!lesson.scheduled_occurrences?.length" @click="openWorkloadOccurrences(lesson)"><strong>{{ lesson.scheduled_occurrences?.length || 0 }} пар</strong><small>{{ lessonDatesPreview(lesson) }}</small></button></td></tr></tbody></table></div>
      <template #footer><button class="btn btn-primary" @click="workloadModal=false">Закрыть</button></template>
    </Modal>

    <Modal v-model="occurrenceModal" size="wide" :title="`Даты занятий — ${occurrenceTarget ? hourName(occurrenceTarget) : ''}`">
      <div v-if="hoursMode==='teachers' && occurrenceTarget?.lesson_id===undefined" class="subtabs"><button :class="{active:occurrenceKind==='scheduled'}" @click="occurrenceKind='scheduled'">Поставлено преподавателю</button><button :class="{active:occurrenceKind==='credited'}" @click="occurrenceKind='credited'">Зачтено фактически</button></div>
      <div class="occurrence-summary"><strong>{{ visibleOccurrences.length }} занятий · {{ visibleOccurrences.reduce((sum,e)=>sum+(e.hours||2),0) }} ч</strong><span v-if="occurrenceWeek">Неделя {{ occurrenceWeek }}</span><button v-if="occurrenceWeek" class="btn btn-ghost btn-sm" @click="occurrenceWeek=null">Показать весь семестр</button></div>
      <div v-if="!visibleOccurrences.length" class="notice warning">Для этого объекта дат нет.</div>
      <div v-else class="table-wrap occurrence-table"><table><thead><tr><th>Дата</th><th>День</th><th>Пара</th><th>Группа</th><th>Дисциплина</th><th>Преподаватель</th><th>Кабинет</th></tr></thead><tbody><tr v-for="e in visibleOccurrences" :key="`${e.date}-${e.slot}-${e.lesson_id}-${e.group_id}`"><td><strong>{{ formatShortDate(e.date) }}</strong></td><td>{{ weekdayFromDate(e.date) }}</td><td>{{ e.slot }}</td><td>{{ e.group_name }}</td><td>{{ e.lesson_name }}</td><td><span :class="{negative:e.is_substitution}">{{ e.teacher_name }}</span><template v-if="e.is_substitution"> → <span class="positive">{{ e.actual_teacher_name }}</span><small>замена</small></template></td><td>{{ e.room || 'не задан' }}</td></tr></tbody></table></div>
      <template #footer><button class="btn btn-primary" @click="occurrenceModal=false">Закрыть</button></template>
    </Modal>
  </div>
</template>

<script setup>
import { computed, onMounted, ref, watch } from 'vue'
import { api } from '../api/index.js'
import Modal from '../components/Modal.vue'
import { useDataStore } from '../stores/data.js'
import { useToast } from '../composables/useToast.js'

const store=useDataStore(), toast=useToast()
const tabs=[{id:'transfer',label:'Перенос на сайт'},{id:'import',label:'Импорт Excel'},{id:'audit',label:'Аудит'},{id:'hours',label:'Учёт часов'},{id:'substitutions',label:'Замены'},{id:'occupancy',label:'Занятость'},{id:'unavailable',label:'Недоступность'},{id:'history',label:'История'}]
const tab=ref('transfer'), semester=ref(1), selectedFile=ref(null), preview=ref(null), importing=ref(false), committing=ref(false)
const transferFile=ref(null),transferBundle=ref(null),transferError=ref(''),transferExporting=ref(false),transferImporting=ref(false),transferConfirm=ref(false),transferPrimary=ref(''),transferPublish=ref(true)
const audit=ref({summary:{},issues:[]}), hours=ref({groups:[],teachers:[],lessons:[]}), versions=ref([]), hoursMode=ref('groups')
const hoursSearch=ref(''),hoursStatus=ref('all'),hoursSort=ref('name')
const hoursPage=ref(1),hoursPageSize=ref(50)
const templateExporting=ref(false)
const occurrenceModal=ref(false),occurrenceTarget=ref(null),occurrenceKind=ref('scheduled'),occurrenceWeek=ref(null)
const workloadModal=ref(false),workloadTarget=ref(null),workloadMode=ref('groups'),workloadSearch=ref(''),workloadStatus=ref('all')
const tabLoading=ref(false),loadedTabs=new Set()
const labels={groups:'Группы',teachers:'Преподаватели',lessons:'Занятия'}
const unavailableForm=ref({teacher:0,from:'',to:'',reason:''})
const replacementForm=ref({lesson_id:-1,date:'',slot:1,absent_teacher:-1,substitute_teacher:-1,hours:2,reason:'',comment:'',status:'active'})
const occupancy=ref({entries:[]}), occupancyWeek=ref('')
const occupancySlots=[0,1,2,3,4,5,6,7]
const transferSummary=computed(()=>{const data=transferBundle.value?.data||{},summary=transferBundle.value?.summary||{};return{groups:summary.groups??data.groups?.length??0,teachers:summary.teachers??data.teachers?.length??0,lessons:summary.lessons??data.lessons?.length??0,rooms:summary.rooms??data.rooms?.length??0,substitutions:summary.substitutions??data.substitutions?.length??0}})
const transferScheduleOptions=computed(()=>{const schedules=transferBundle.value?.schedules||{};return[{value:'manual',label:'Ручное из конструктора — с правками диспетчера'},{value:'auto',label:'Автоматически сгенерированное'},{value:'published',label:'Последнее опубликованное'}].filter(item=>schedules[item.value]&&Array.isArray(schedules[item.value].groups))})
const hoursSourceRows=computed(()=>hours.value[hoursMode.value]||[])
const searchedHourRows=computed(()=>{const q=hoursSearch.value.toLocaleLowerCase('ru');return hoursSourceRows.value.filter(row=>!q||`${hourName(row)} ${row.teacher_name||''} ${row.group_name||''}`.toLocaleLowerCase('ru').includes(q))})
const hourRows=computed(()=>{const rows=searchedHourRows.value.filter(row=>{const credited=row.credited_hours??row.scheduled_hours,planned=row.planned_hours||0;if(hoursStatus.value==='not_started')return row.scheduled_hours===0&&planned>0;if(hoursStatus.value==='in_progress')return credited>0&&credited<planned;if(hoursStatus.value==='completed')return planned>0&&credited===planned;if(hoursStatus.value==='overrun')return credited>planned;return true});return [...rows].sort((a,b)=>{if(hoursSort.value==='remaining')return b.remaining_hours-a.remaining_hours;if(hoursSort.value==='scheduled')return b.scheduled_hours-a.scheduled_hours;if(hoursSort.value==='progress')return percent(b)-percent(a);return hourName(a).localeCompare(hourName(b),'ru')})})
const hoursPageCount=computed(()=>Math.max(1,Math.ceil(hourRows.value.length/hoursPageSize.value)))
const pagedHourRows=computed(()=>hourRows.value.slice((hoursPage.value-1)*hoursPageSize.value,hoursPage.value*hoursPageSize.value))
const hoursRange=computed(()=>hourRows.value.length?`Показано ${(hoursPage.value-1)*hoursPageSize.value+1}–${Math.min(hoursPage.value*hoursPageSize.value,hourRows.value.length)} из ${hourRows.value.length}`:'Показано: 0')
const hoursSummary=computed(()=>searchedHourRows.value.reduce((sum,row)=>{const credited=row.credited_hours??row.scheduled_hours,planned=row.planned_hours||0;sum.planned+=planned;sum.scheduled+=row.scheduled_hours||0;sum.credited+=credited||0;sum.remaining+=row.remaining_hours||0;if(planned>0&&row.scheduled_hours===0)sum.notStarted++;if(credited>planned)sum.overrun++;return sum},{planned:0,scheduled:0,credited:0,remaining:0,notStarted:0,overrun:0}))
const workloadName=computed(()=>workloadTarget.value?hourName(workloadTarget.value):'')
const workloadSource=computed(()=>{if(!workloadTarget.value)return[];return (hours.value.lessons||[]).filter(l=>workloadMode.value==='groups'?l.group_id===workloadTarget.value.group_id:l.teacher_id===workloadTarget.value.teacher_id)})
const workloadLessons=computed(()=>{const q=workloadSearch.value.toLocaleLowerCase('ru');return workloadSource.value.filter(l=>{if(q&&!`${l.name} ${l.group_name} ${l.teacher_name}`.toLocaleLowerCase('ru').includes(q))return false;const scheduled=l.scheduled_hours||0,planned=l.planned_hours||0;if(workloadStatus.value==='not_started')return planned>0&&scheduled===0;if(workloadStatus.value==='in_progress')return scheduled>0&&scheduled<planned;if(workloadStatus.value==='completed')return planned>0&&scheduled===planned;if(workloadStatus.value==='overrun')return scheduled>planned;return true}).sort((a,b)=>a.name.localeCompare(b.name,'ru')||a.group_name.localeCompare(b.group_name,'ru'))})
const workloadSummary=computed(()=>workloadSource.value.reduce((sum,l)=>{sum.count++;sum.planned+=l.planned_hours||0;sum.scheduled+=l.scheduled_hours||0;sum.remaining+=l.remaining_hours||0;if((l.planned_hours||0)>0&&!l.scheduled_hours)sum.notStarted++;return sum},{count:0,planned:0,scheduled:0,remaining:0,notStarted:0}))
const visibleOccurrences=computed(()=>{if(!occurrenceTarget.value)return[];const key=occurrenceKind.value==='credited'?'credited_occurrences':'scheduled_occurrences';const items=occurrenceTarget.value[key]||[];return occurrenceWeek.value?items.filter(e=>e.week_index===occurrenceWeek.value):items})
const weekStart=date=>{if(!date)return'';const d=new Date(`${date}T12:00:00`),offset=(d.getDay()+6)%7;d.setDate(d.getDate()-offset);return d.toISOString().slice(0,10)}
const occupancyWeeks=computed(()=>[...new Set((occupancy.value.entries||[]).map(e=>weekStart(e.date)))].sort())
const occupancyBlocks=computed(()=>{const entries=(occupancy.value.entries||[]).filter(e=>weekStart(e.date)===occupancyWeek.value),map=new Map();for(const e of entries){if(!map.has(e.teacher_id))map.set(e.teacher_id,{teacher_id:e.teacher_id,teacher_name:e.teacher_name,cells:{}});const key=`${e.weekday}-${e.slot}`;(map.get(e.teacher_id).cells[key]??=[]).push(e)}return [...map.values()].sort((a,b)=>a.teacher_name.localeCompare(b.teacher_name,'ru'))})
const occupancyCell=(block,day,slot)=>block.cells[`${day}-${slot}`]||[]
onMounted(()=>loadTabData(tab.value))
watch(tab,value=>loadTabData(value))
watch([hoursMode,hoursSearch,hoursStatus,hoursSort,hoursPageSize],()=>{hoursPage.value=1})
watch(hoursPageCount,count=>{if(hoursPage.value>count)hoursPage.value=count})
async function loadTabData(target,force=false){if(!force&&loadedTabs.has(target))return;tabLoading.value=true;try{if(target==='audit'){const r=await api.data.audit();if(r.ok)audit.value=r.data}else if(target==='hours'){const r=await api.data.hours();if(r.ok)hours.value=r.data}else if(target==='history'){const r=await api.data.versions();if(r.ok)versions.value=r.data}else if(target==='occupancy'){const r=await api.data.teacherOccupancy();if(r.ok){occupancy.value=r.data;if(!occupancyWeeks.value.includes(occupancyWeek.value))occupancyWeek.value=occupancyWeeks.value[0]||''}}else if(target==='substitutions'){await Promise.all([store.loadTeachers(),store.loadGroups(),store.loadLessons(),store.loadSubstitutions()]);initTeacherForms()}else if(target==='unavailable'){await Promise.all([store.loadTeachers(),store.loadTeacherUnavailable()]);initTeacherForms()}loadedTabs.add(target)}finally{tabLoading.value=false}}
function initTeacherForms(){if(store.teachers.length&&!store.teachers.some(t=>t.id===unavailableForm.value.teacher))unavailableForm.value.teacher=store.teachers[0].id;if(store.teachers.length&&replacementForm.value.substitute_teacher<0)replacementForm.value.substitute_teacher=store.teachers[0].id}
async function refreshAll(){await loadTabData(tab.value,true)}
async function exportTransferBundle(){transferExporting.value=true;try{const r=await api.transfer.exportBundle();if(!r.ok)throw new Error(r.data?.message||'Сервер не сформировал пакет');const blob=new Blob([JSON.stringify(r.data,null,2)],{type:'application/json;charset=utf-8'}),url=URL.createObjectURL(blob),link=document.createElement('a'),stamp=new Date().toISOString().slice(0,19).replaceAll(':','-');link.href=url;link.download=`raspis-full-${stamp}.raspis.json`;document.body.appendChild(link);link.click();link.remove();setTimeout(()=>URL.revokeObjectURL(url),1000);toast.success('Полный пакет скачан. Его можно загрузить на сайт.')}catch(e){toast.error(`Не удалось скачать пакет: ${e.message}`)}finally{transferExporting.value=false}}
async function selectTransferBundle(event){transferFile.value=event.target.files?.[0]||null;transferBundle.value=null;transferError.value='';transferPrimary.value='';if(!transferFile.value)return;try{const parsed=JSON.parse(await transferFile.value.text());if(parsed?.format!=='raspis-transfer-bundle'||Number(parsed?.schema_version)!==1)throw new Error('Выбранный файл не является полным пакетом расписания');if(!parsed.data||!Array.isArray(parsed.data.groups)||!Array.isArray(parsed.data.teachers)||!Array.isArray(parsed.data.lessons))throw new Error('В пакете отсутствует полная база данных');if(!parsed.schedules||!['auto','manual','published'].every(key=>parsed.schedules[key]===null||Array.isArray(parsed.schedules[key]?.groups)))throw new Error('В пакете повреждён раздел расписаний');transferBundle.value=parsed;transferPrimary.value=parsed.schedules.manual?'manual':parsed.schedules.auto?'auto':parsed.schedules.published?'published':'';if(!transferPrimary.value)throw new Error('В пакете нет ни одного расписания')}catch(e){transferBundle.value=null;transferError.value=e.message||'Файл не удалось прочитать'}}
function transferScheduleLabel(value){return transferScheduleOptions.value.find(item=>item.value===value)?.label||'не выбрано'}
async function applyTransferBundle(){if(!transferBundle.value||!transferPrimary.value)return;transferImporting.value=true;const r=await api.transfer.importBundle(transferBundle.value,transferPrimary.value,transferPublish.value);transferImporting.value=false;if(r.ok){transferConfirm.value=false;toast.success('Пакет применён. Данные, расписание и учёт часов обновлены.');loadedTabs.clear();setTimeout(()=>window.location.reload(),900)}else toast.error(r.data?.message||'Пакет не удалось применить')}
function selectFile(e){selectedFile.value=e.target.files?.[0]||null;preview.value=null}
async function previewImport(){importing.value=true;const raw=await api.data.get();if(!raw.ok){toast.error(raw.data?.message||'Не удалось загрузить текущие данные');importing.value=false;return}try{const {parseVkleyki}=await import('../utils/excelImport.js');const result=await parseVkleyki(selectedFile.value,raw.data,semester.value);const check=await api.data.audit(result.data);if(check.ok){result.audit=check.data;for(const issue of check.data.issues.filter(i=>i.severity==='error').slice(0,50))result.errors.push({sheet:'Аудит',message:issue.message})}preview.value=result}catch(e){toast.error(`Excel не прочитан: ${e.message}`)}finally{importing.value=false}}
async function commitImport(){committing.value=true;const r=await api.data.replace(preview.value.data);committing.value=false;if(r.ok){toast.success('Данные импортированы. Перед генерацией проверьте аудит.');preview.value=null;loadedTabs.clear()}else toast.error(r.data?.message||'Ошибка импорта')}
const signed=n=>n>0?`+${n}`:`${n}`
const severity=s=>s==='error'?'error':s==='warning'?'warning':'muted'
const severityLabel=s=>s==='error'?'Ошибка':s==='warning'?'Предупреждение':'Информация'
const hourName=r=>r.lesson_id!==undefined?`${r.group_name}: ${r.name}`:(r.group_name||r.teacher_name||r.name)
const hourKey=r=>`${r.group_id??''}-${r.teacher_id??''}-${r.lesson_id??''}`
const percent=r=>r.planned_hours?Math.max(0,Math.min(100,Math.round((r.credited_hours??r.scheduled_hours)/r.planned_hours*100))):0
const hourStatus=r=>{const credited=r.credited_hours??r.scheduled_hours;if(credited>r.planned_hours)return'row-overrun';if(r.planned_hours>0&&r.scheduled_hours===0)return'row-not-started';if(r.planned_hours>0&&credited===r.planned_hours)return'row-completed';return''}
function openOccurrences(row,kind='scheduled',week=null){occurrenceTarget.value=row;occurrenceKind.value=kind;occurrenceWeek.value=week;occurrenceModal.value=true}
function openWorkload(row){workloadTarget.value=row;workloadMode.value=hoursMode.value;workloadSearch.value='';workloadStatus.value='all';workloadModal.value=true}
function openWorkloadOccurrences(lesson){workloadModal.value=false;openOccurrences(lesson,'scheduled')}
const lessonDatesPreview=lesson=>{const dates=lesson.scheduled_occurrences||[];if(!dates.length)return'дат нет';const shown=dates.slice(0,2).map(e=>`${formatShortDate(e.date)}, ${e.slot} пара`).join(' · ');return dates.length>2?`${shown} · ещё ${dates.length-2}`:shown}
const formatShortDate=s=>s?new Date(`${s}T12:00:00`).toLocaleDateString('ru-RU'):'—'
const weekdayFromDate=s=>s?new Date(`${s}T12:00:00`).toLocaleDateString('ru-RU',{weekday:'short'}).toUpperCase():'—'
const teacherName=id=>store.teachers.find(t=>t.id===id)?.name||`ID ${id}`
const groupName=id=>store.groups.find(g=>g.id===id)?.name||`Группа ${id}`
const lessonName=id=>store.lessons.find(l=>l.id===id)?.name||`Занятие ${id}`
const weekdayName=n=>['','ПН','ВТ','СР','ЧТ','ПТ','СБ','ВС'][n]||'—'
async function exportHours(){try{const [,o]=await Promise.all([store.loadSubstitutions(),api.data.teacherOccupancy()]);if(o.ok)occupancy.value=o.data;const {exportAccountingWorkbook}=await import('../utils/accountingExport.js');exportAccountingWorkbook({hours:hours.value,substitutions:store.substitutions,occupancy:occupancy.value,teacherName,lessonName});toast.success('Отчёт Excel сформирован')}catch(e){toast.error(`Не удалось сформировать Excel: ${e.message}`)}}
async function exportReferenceHours(){templateExporting.value=true;try{const {exportReferenceAccountingWorkbook}=await import('../utils/referenceAccountingExport.js');const result=await exportReferenceAccountingWorkbook({hours:hours.value});toast.success(`Файл по образцу готов: ${result.groups} групп, ${result.sheets} листов`)}catch(e){toast.error(`Не удалось собрать файл по образцу: ${e.message}`)}finally{templateExporting.value=false}}
function syncAbsentTeacher(){const lesson=store.lessons.find(l=>l.id===replacementForm.value.lesson_id);if(lesson)replacementForm.value.absent_teacher=lesson.teacher}
async function addSubstitution(){const f=replacementForm.value;if(f.lesson_id<0||!f.date||f.absent_teacher<0||f.substitute_teacher<0){toast.error('Заполните занятие, дату и обоих преподавателей');return}if(f.absent_teacher===f.substitute_teacher){toast.error('Заменяющий должен отличаться от отсутствующего');return}const r=await store.createSubstitution({...f});if(r.ok){toast.success('Замена добавлена, зачёт часов пересчитан');replacementForm.value={...f,lesson_id:-1,date:'',reason:''};loadedTabs.delete('hours');loadedTabs.delete('occupancy');await refreshAll()}else toast.error(r.data?.message||'Ошибка')}
async function removeSubstitution(s){const r=await store.deleteSubstitution(s.id);if(r.ok){toast.success('Замена удалена');loadedTabs.delete('hours');loadedTabs.delete('occupancy');await refreshAll()}else toast.error(r.data?.message||'Ошибка')}
async function addUnavailable(){const f=unavailableForm.value;if(!f.from||!f.to){toast.error('Укажите обе даты');return}if(f.to<f.from){toast.error('Дата окончания раньше начала');return}const r=await store.createTeacherUnavailable({...f});if(r.ok){toast.success('Недоступность добавлена');unavailableForm.value={...f,from:'',to:'',reason:''};await refreshAll()}else toast.error(r.data?.message||'Ошибка')}
async function removeUnavailable(u){const r=await store.deleteTeacherUnavailable(u.id);if(r.ok){toast.success('Ограничение удалено');await refreshAll()}else toast.error(r.data?.message||'Ошибка')}
async function restore(v){if(!confirm(`Откатить данные к версии ${formatDate(v.created_at)}?`))return;const r=await api.data.restore(v.filename);if(r.ok){toast.success('Версия восстановлена');loadedTabs.clear();await refreshAll()}else toast.error(r.data?.message||'Ошибка отката')}
const formatDate=s=>s?new Date(s).toLocaleString('ru-RU'): '—'
</script>

<style scoped>
.subtitle,.help,small{color:var(--text-muted);font-size:13px}.tabs,.subtabs{display:flex;gap:6px;flex-wrap:wrap;margin-bottom:18px}.tab,.subtabs button{border:1px solid var(--border);background:var(--bg-secondary);color:var(--text-secondary);padding:8px 13px;border-radius:8px;cursor:pointer}.tab.active,.subtabs button.active{color:var(--accent);border-color:var(--accent);background:var(--accent-light)}.section{margin-bottom:20px}.section h2{font-size:18px;margin-bottom:6px}.section-head{display:flex;justify-content:space-between;align-items:flex-start;gap:12px;margin-bottom:14px}.import-controls{display:grid;grid-template-columns:minmax(260px,1fr) 220px auto;gap:10px;margin:18px 0}.file-drop{border:1px dashed var(--border-strong);border-radius:8px;padding:9px 12px;color:var(--text-secondary);cursor:pointer}.file-drop input{display:none}.metric-row{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px;margin:14px 0}.metric{background:var(--bg-secondary);border:1px solid var(--border);padding:12px;border-radius:10px;display:flex;flex-direction:column}.metric strong{font-size:22px}.notice{border:1px solid var(--border);background:var(--bg-secondary);border-radius:9px;padding:12px;margin:12px 0}.notice.error,.error-box{border-color:var(--error);background:var(--error-light)}.notice.warning,.warning-box{border-color:var(--warning);background:var(--warning-light)}.notice.success{border-color:var(--success);background:var(--success-light)}.positive{color:var(--success)}.negative,.danger{color:var(--error)}code{color:var(--text-secondary)}.progress{width:110px;height:6px;background:var(--bg-tertiary);border-radius:5px;display:inline-block;margin-right:6px;overflow:hidden}.progress span{display:block;height:100%;background:var(--success)}.inline-form{display:grid;grid-template-columns:1.2fr 160px 160px 1fr auto;gap:8px;margin-bottom:16px}.replacement-form{display:grid;grid-template-columns:minmax(250px,2fr) 150px 110px minmax(180px,1fr) minmax(180px,1fr) 90px minmax(150px,1fr) auto;gap:8px;margin-bottom:16px}.date-filter{max-width:190px}th small,td small,.occurrence-button small{display:block;white-space:nowrap;font-weight:400;margin-top:3px}.hours-summary{grid-template-columns:repeat(6,minmax(130px,1fr))}.hours-controls{display:grid;grid-template-columns:minmax(260px,1fr) 180px 220px auto;gap:8px;align-items:center;margin-bottom:12px}.result-count{color:var(--text-muted);font-size:13px;white-space:nowrap}.occurrence-button,.week-button{border:0;background:transparent;color:var(--accent);cursor:pointer;padding:2px;text-align:left}.occurrence-button:disabled{color:var(--text-primary);cursor:default}.week-button{text-decoration:underline}.row-not-started{background:var(--warning-light)}.row-overrun{background:var(--error-light)}.row-completed{background:var(--success-light)}.occurrence-summary{display:flex;align-items:center;gap:10px;padding:10px 12px;margin-bottom:10px;background:var(--bg-secondary);border-radius:8px}.occurrence-summary span{color:var(--text-secondary)}.occurrence-table{max-height:58vh}.occurrence-table th{position:sticky;top:0;background:var(--bg-secondary);z-index:1}.occupancy-table td{min-width:150px;vertical-align:top}.occupancy-table td:nth-child(1),.occupancy-table td:nth-child(2){min-width:auto}.teacherStart{border-top:2px solid var(--border-strong)}.busy-cell{display:flex;flex-direction:column;gap:2px;background:var(--accent-light);border-left:3px solid var(--accent);border-radius:5px;padding:5px;margin:2px 0;font-size:12px}.busy-cell.replacement{border-color:var(--warning);background:var(--warning-light)}.busy-cell span{color:var(--text-secondary)}@media(max-width:1200px){.hours-summary{grid-template-columns:repeat(3,1fr)}.hours-controls{grid-template-columns:1fr 1fr}.replacement-form{grid-template-columns:1fr 1fr}}@media(max-width:900px){.import-controls,.inline-form,.replacement-form,.hours-controls{grid-template-columns:1fr}.hours-summary{grid-template-columns:repeat(2,1fr)}.section-head{flex-direction:column}}
.loading-notice{color:var(--text-secondary);animation:pulse 1.2s ease-in-out infinite}.page-size{min-width:110px}.workload-link{display:block;border:0;background:transparent;color:var(--accent);font-size:12px;padding:4px 0 0;cursor:pointer;text-decoration:underline}.pagination{display:flex;justify-content:center;align-items:center;gap:12px;margin:14px 0;color:var(--text-secondary);font-size:13px}.workload-summary{display:grid;grid-template-columns:repeat(5,minmax(110px,1fr));gap:8px;margin-bottom:12px}.workload-summary div{display:flex;flex-direction:column;padding:10px;border:1px solid var(--border);background:var(--bg-secondary);border-radius:8px}.workload-summary span{color:var(--text-muted);font-size:12px}.workload-summary strong{font-size:18px}.workload-controls{display:grid;grid-template-columns:minmax(260px,1fr) 210px;gap:8px;margin-bottom:12px}.workload-table{max-height:58vh}.workload-table th{position:sticky;top:0;background:var(--bg-secondary);z-index:1}@keyframes pulse{50%{opacity:.65}}@media(max-width:900px){.workload-summary{grid-template-columns:repeat(2,1fr)}.workload-controls{grid-template-columns:1fr}}
.export-actions{display:flex;gap:8px;flex-wrap:wrap;justify-content:flex-end}
.transfer-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:18px}.transfer-card{display:flex;flex-direction:column;gap:14px;padding:22px}.transfer-card h2{margin:0 0 5px}.transfer-icon{display:grid;place-items:center;width:48px;height:48px;border-radius:12px;background:var(--accent-light);font-size:24px}.transfer-list{margin:0;padding-left:20px;color:var(--text-secondary);font-size:14px;line-height:1.7}.transfer-button{align-self:flex-start}.transfer-file{display:block}.transfer-metrics{grid-template-columns:repeat(5,minmax(90px,1fr));margin:0}.transfer-metrics .metric{padding:9px}.transfer-metrics .metric strong{font-size:18px}.transfer-check{display:flex;gap:9px;align-items:flex-start;color:var(--text-secondary);font-size:14px;cursor:pointer}.transfer-check input{margin-top:3px}.transfer-note{margin-top:18px}.transfer-confirm-summary{display:grid;gap:7px;margin:14px 0}.transfer-confirm-summary p{margin:0;color:var(--text-secondary)}
@media(max-width:1000px){.transfer-grid{grid-template-columns:1fr}.transfer-metrics{grid-template-columns:repeat(3,1fr)}}
</style>
