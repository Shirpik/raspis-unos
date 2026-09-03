"""Локальный русскоязычный менеджер дедлайнов."""
import json
import shutil
import sys
import uuid
from datetime import datetime, timedelta
from pathlib import Path
import tkinter as tk
from tkinter import filedialog, messagebox

import customtkinter as ctk


# При запуске EXE данные лежат рядом с программой, а не во временной папке PyInstaller.
DATA_FILE = (Path(sys.executable) if getattr(sys, "frozen", False) else Path(__file__)).with_name("deadlines.json")
DATE_FORMAT = "%d.%m.%Y %H:%M"
PRIORITIES = {"Высокий": "#fb7185", "Средний": "#fbbf24", "Низкий": "#60a5fa"}
CATEGORIES = ["Учёба", "Работа", "Личное", "Проект", "Другое"]
FILTERS = ["Все", "Активные", "На сегодня", "Выполненные", "Просроченные"]


class DeadlineTracker(ctk.CTk):
    def __init__(self):
        super().__init__()
        ctk.set_appearance_mode("dark")
        ctk.set_default_color_theme("blue")
        self.title("Дедлайн-трекер")
        self.geometry("1220x780")
        self.minsize(960, 640)
        self.configure(fg_color="#0d1018")
        self.items = self.load_items()
        self.current_filter = "Все"
        self.search = tk.StringVar()
        self.sort = tk.StringVar(value="По дате")
        self.stats = {}
        self.build_ui()
        self.bind("<Control-n>", lambda _: self.open_editor())
        self.bind("<Control-f>", lambda _: self.search_entry.focus_set())
        self.bind("<Escape>", lambda _: self.search_entry.delete(0, "end"))
        self.refresh()
        self.live_refresh()

    # ===== Persistent storage =====
    def load_items(self):
        try:
            data = json.loads(DATA_FILE.read_text(encoding="utf-8"))
            if not isinstance(data, list):
                return []
            return self.normalize(data)
        except (OSError, json.JSONDecodeError):
            return []

    def normalize(self, records):
        for item in records:
            item.setdefault("id", str(uuid.uuid4()))
            item.setdefault("done", False)
            item.setdefault("category", "Другое")
            item.setdefault("notes", "")
            item.setdefault("created", datetime.now().isoformat())
        return records

    def save_items(self):
        DATA_FILE.write_text(json.dumps(self.items, ensure_ascii=False, indent=2), encoding="utf-8")

    def get_item(self, item_id):
        return next((x for x in self.items if x["id"] == item_id), None)

    def deadline(self, item):
        return datetime.fromisoformat(item["deadline"])

    # ===== Derived task information =====
    def task_state(self, item):
        if item.get("done"):
            return "done", "Выполнено", "#4ade80"
        seconds = (self.deadline(item) - datetime.now()).total_seconds()
        if seconds < 0:
            return "overdue", "Просрочено", "#fb7185"
        if seconds < 86400:
            return "today", "Сегодня", "#fb923c"
        if seconds < 172800:
            return "soon", "Скоро", "#fbbf24"
        return "normal", "В плане", "#60a5fa"

    def left_text(self, item):
        if item.get("done"):
            return "Задача завершена"
        seconds = int((self.deadline(item) - datetime.now()).total_seconds())
        prefix = "Просрочено на " if seconds < 0 else "Осталось "
        days, rest = divmod(abs(seconds), 86400)
        hours, rest = divmod(rest, 3600)
        minutes = rest // 60
        return f"{prefix}{days} д. {hours} ч." if days else f"{prefix}{hours} ч. {minutes} мин."

    def date_text(self, item):
        dt = self.deadline(item)
        today = datetime.now().date()
        if dt.date() == today:
            return f"Сегодня, {dt:%H:%M}"
        if dt.date() == today + timedelta(days=1):
            return f"Завтра, {dt:%H:%M}"
        return dt.strftime(DATE_FORMAT)

    # ===== Application view =====
    def build_ui(self):
        self.grid_columnconfigure(0, weight=1)
        self.grid_rowconfigure(3, weight=1)
        self.build_header()
        self.build_statistics_bar()
        self.build_toolbar()
        self.build_workspace()
        self.footer = ctk.CTkLabel(self, text="Автосохранение включено  •  Ctrl+N — новая задача  •  Ctrl+F — поиск", text_color="#6f7890")
        self.footer.grid(row=4, column=0, sticky="w", padx=32, pady=12)

    def build_header(self):
        frame = ctk.CTkFrame(self, fg_color="transparent")
        frame.grid(row=0, column=0, sticky="ew", padx=30, pady=(22, 12))
        frame.grid_columnconfigure(1, weight=1)
        ctk.CTkLabel(frame, text="◈  Дедлайн-трекер", font=ctk.CTkFont(size=29, weight="bold")).grid(row=0, column=0, sticky="w")
        self.clock = ctk.CTkLabel(frame, text="", text_color="#b6c0d3")
        self.clock.grid(row=0, column=1, sticky="e", padx=20)
        ctk.CTkButton(frame, text="☼", width=38, command=self.toggle_theme, fg_color="#232b3d", hover_color="#34405a").grid(row=0, column=2, padx=(0, 8))
        ctk.CTkButton(frame, text="＋ Новый дедлайн", command=self.open_editor, height=38, fg_color="#6366f1", hover_color="#4f46e5", font=ctk.CTkFont(weight="bold")).grid(row=0, column=3)

    def build_statistics_bar(self):
        frame = ctk.CTkFrame(self, fg_color="transparent")
        frame.grid(row=1, column=0, sticky="ew", padx=30, pady=(0, 14))
        for n in range(4): frame.grid_columnconfigure(n, weight=1, uniform="stat")
        definitions = [("all", "Всего задач", "◌", "#a5b4fc"), ("active", "Активных", "↗", "#60a5fa"), ("soon", "Ближайшие 2 дня", "◷", "#fbbf24"), ("done", "Выполнено", "✓", "#4ade80")]
        for col, (key, label, icon, color) in enumerate(definitions):
            card = ctk.CTkFrame(frame, fg_color="#181d2a", corner_radius=14)
            card.grid(row=0, column=col, sticky="ew", padx=(0 if col == 0 else 6, 0 if col == 3 else 6))
            ctk.CTkLabel(card, text=icon, text_color=color, font=ctk.CTkFont(size=19, weight="bold")).grid(row=0, column=0, rowspan=2, padx=(15, 10), pady=14)
            value = ctk.CTkLabel(card, text="0", font=ctk.CTkFont(size=23, weight="bold"))
            value.grid(row=0, column=1, sticky="w", pady=(10, 0))
            ctk.CTkLabel(card, text=label, text_color="#929bb0", font=ctk.CTkFont(size=12)).grid(row=1, column=1, sticky="w", pady=(0, 10))
            self.stats[key] = value

    def build_toolbar(self):
        frame = ctk.CTkFrame(self, fg_color="transparent")
        frame.grid(row=2, column=0, sticky="ew", padx=30, pady=(0, 12))
        frame.grid_columnconfigure(0, weight=1)
        self.search_entry = ctk.CTkEntry(frame, textvariable=self.search, placeholder_text="⌕  Поиск по названию, категории или заметкам…", height=36, width=340)
        self.search_entry.grid(row=0, column=0, sticky="w")
        self.search.trace_add("write", lambda *_: self.refresh_list())
        self.filters = ctk.CTkSegmentedButton(frame, values=FILTERS, command=self.set_filter, height=36)
        self.filters.set("Все")
        self.filters.grid(row=0, column=1, padx=15)
        ctk.CTkOptionMenu(frame, values=["По дате", "Сначала важные", "По названию"], variable=self.sort, command=lambda _: self.refresh_list(), width=160, height=36).grid(row=0, column=2)

    def build_workspace(self):
        frame = ctk.CTkFrame(self, fg_color="transparent")
        frame.grid(row=3, column=0, sticky="nsew", padx=30)
        frame.grid_columnconfigure(0, weight=1)
        frame.grid_columnconfigure(1, minsize=270)
        frame.grid_rowconfigure(0, weight=1)
        self.list_frame = ctk.CTkScrollableFrame(frame, fg_color="#131722", corner_radius=16)
        self.list_frame.grid(row=0, column=0, sticky="nsew", padx=(0, 14))
        self.list_frame.grid_columnconfigure(0, weight=1)
        side = ctk.CTkFrame(frame, fg_color="#181d2a", corner_radius=16)
        side.grid(row=0, column=1, sticky="nsew")
        ctk.CTkLabel(side, text="Ближайшие", font=ctk.CTkFont(size=17, weight="bold")).pack(anchor="w", padx=18, pady=(18, 2))
        ctk.CTkLabel(side, text="Следующие дедлайны", text_color="#929bb0", font=ctk.CTkFont(size=12)).pack(anchor="w", padx=18, pady=(0, 10))
        self.upcoming = ctk.CTkScrollableFrame(side, fg_color="transparent", height=250)
        self.upcoming.pack(fill="x", padx=10)
        ctk.CTkFrame(side, height=1, fg_color="#30384b").pack(fill="x", padx=18, pady=14)
        ctk.CTkLabel(side, text="Быстрые действия", font=ctk.CTkFont(size=16, weight="bold")).pack(anchor="w", padx=18, pady=(0, 7))
        self.side_button(side, "⌁  Экспортировать JSON", self.export_data)
        self.side_button(side, "↥  Импортировать JSON", self.import_data)
        self.side_button(side, "▦  Моя статистика", self.show_statistics)
        self.side_button(side, "✦  Добавить примеры", self.add_examples)
        self.side_button(side, "?  Горячие клавиши", self.show_help)
        self.side_button(side, "✓  Очистить выполненные", self.clear_done, "#342633")

    def side_button(self, parent, text, command, color="#252d40"):
        ctk.CTkButton(parent, text=text, command=command, fg_color=color, hover_color="#3a465d", height=31).pack(fill="x", padx=18, pady=4)

    # ===== Filter and cards =====
    def set_filter(self, value):
        self.current_filter = value
        self.refresh_list()

    def visible(self, item):
        state, _, _ = self.task_state(item)
        is_today = self.deadline(item).date() == datetime.now().date()
        accepted = {"Все": True, "Активные": not item["done"], "На сегодня": is_today and not item["done"], "Выполненные": item["done"], "Просроченные": state == "overdue"}[self.current_filter]
        query = self.search.get().strip().lower()
        haystack = " ".join((item["title"], item.get("category", ""), item.get("notes", ""))).lower()
        return accepted and (not query or query in haystack)

    def sorted_items(self, items):
        if self.sort.get() == "Сначала важные":
            levels = {"Высокий": 0, "Средний": 1, "Низкий": 2}
            return sorted(items, key=lambda x: (x["done"], levels.get(x["priority"], 3), x["deadline"]))
        if self.sort.get() == "По названию":
            return sorted(items, key=lambda x: (x["done"], x["title"].lower()))
        return sorted(items, key=lambda x: (x["done"], x["deadline"]))

    def refresh(self):
        active = [x for x in self.items if not x["done"]]
        number_soon = sum((self.deadline(x) - datetime.now()).total_seconds() < 172800 for x in active)
        values = {"all": len(self.items), "active": len(active), "soon": number_soon, "done": len(self.items) - len(active)}
        for key, value in values.items(): self.stats[key].configure(text=str(value))
        self.refresh_list()
        self.refresh_upcoming()

    def refresh_list(self):
        for child in self.list_frame.winfo_children(): child.destroy()
        records = self.sorted_items([x for x in self.items if self.visible(x)])
        if not records:
            self.empty_state()
        for row, item in enumerate(records): self.task_card(row, item)

    def empty_state(self):
        box = ctk.CTkFrame(self.list_frame, fg_color="transparent")
        box.grid(row=0, column=0, pady=100)
        ctk.CTkLabel(box, text="☷", font=ctk.CTkFont(size=42), text_color="#59647a").pack()
        ctk.CTkLabel(box, text="Ничего не найдено", font=ctk.CTkFont(size=18, weight="bold")).pack(pady=(6, 2))
        ctk.CTkLabel(box, text="Добавьте дедлайн или измените фильтр", text_color="#929bb0").pack()
        if not self.items: ctk.CTkButton(box, text="Добавить первую задачу", command=self.open_editor, fg_color="#6366f1").pack(pady=16)

    def task_card(self, row, item):
        _, caption, color = self.task_state(item)
        card = ctk.CTkFrame(self.list_frame, fg_color="#1b2030", corner_radius=13)
        card.grid(row=row, column=0, sticky="ew", padx=10, pady=7)
        card.grid_columnconfigure(1, weight=1)
        ctk.CTkFrame(card, width=6, fg_color=color, corner_radius=3).grid(row=0, column=0, rowspan=3, sticky="ns", padx=(0, 14))
        title_color = "#758097" if item["done"] else "#f3f4f6"
        ctk.CTkLabel(card, text=item["title"], text_color=title_color, font=ctk.CTkFont(size=16, weight="bold")).grid(row=0, column=1, sticky="w", pady=(13, 1))
        ctk.CTkLabel(card, text=f"{item['category']}   •   {self.date_text(item)}", text_color="#929bb0", font=ctk.CTkFont(size=12)).grid(row=1, column=1, sticky="w")
        ctk.CTkLabel(card, text=self.left_text(item), text_color=color, font=ctk.CTkFont(size=12, weight="bold")).grid(row=2, column=1, sticky="w", pady=(2, 13))
        ctk.CTkLabel(card, text=item["priority"], fg_color=PRIORITIES[item["priority"]], corner_radius=8, width=82, height=24, font=ctk.CTkFont(size=11, weight="bold")).grid(row=0, column=2, padx=6, pady=(10, 1))
        ctk.CTkLabel(card, text=caption, text_color=color, font=ctk.CTkFont(size=11)).grid(row=1, column=2, padx=6)
        actions = ctk.CTkFrame(card, fg_color="transparent")
        actions.grid(row=0, column=3, rowspan=3, padx=(10, 10))
        ctk.CTkButton(actions, text="✓" if not item["done"] else "↩", width=32, height=30, command=lambda: self.toggle_done(item["id"]), fg_color="#263a33", hover_color="#365245").grid(row=0, column=0, padx=2)
        ctk.CTkButton(actions, text="✎", width=32, height=30, command=lambda: self.open_editor(item), fg_color="#263041", hover_color="#34415a").grid(row=0, column=1, padx=2)
        ctk.CTkButton(actions, text="i", width=32, height=30, command=lambda: self.show_details(item), fg_color="#263041", hover_color="#34415a").grid(row=0, column=2, padx=2)
        more = ctk.CTkButton(actions, text="⋮", width=32, height=30, fg_color="#263041", hover_color="#34415a")
        more.configure(command=lambda: self.card_menu(more, item["id"]))
        more.grid(row=0, column=3, padx=2)

    def card_menu(self, button, item_id):
        menu = tk.Menu(self, tearoff=0, bg="#202738", fg="white", activebackground="#6366f1", activeforeground="white", borderwidth=0)
        menu.add_command(label="Дублировать", command=lambda: self.duplicate(item_id))
        menu.add_command(label="Удалить", command=lambda: self.delete(item_id))
        menu.tk_popup(button.winfo_rootx(), button.winfo_rooty() + button.winfo_height())

    def refresh_upcoming(self):
        for child in self.upcoming.winfo_children(): child.destroy()
        items = sorted((x for x in self.items if not x["done"]), key=lambda x: x["deadline"])[:5]
        if not items: ctk.CTkLabel(self.upcoming, text="Активных задач нет", text_color="#929bb0").pack(pady=20)
        for item in items:
            _, _, color = self.task_state(item)
            row = ctk.CTkFrame(self.upcoming, fg_color="#22293a", corner_radius=9)
            row.pack(fill="x", pady=3)
            ctk.CTkLabel(row, text="●", text_color=color, width=22).pack(side="left", padx=(7, 0))
            ctk.CTkLabel(row, text=item["title"], anchor="w", font=ctk.CTkFont(size=12)).pack(side="left", fill="x", expand=True, pady=8)

    # ===== Editing =====
    def open_editor(self, item=None):
        win = ctk.CTkToplevel(self)
        win.title("Редактирование дедлайна" if item else "Новый дедлайн")
        win.geometry("500x510"); win.resizable(False, False); win.grab_set()
        ctk.CTkLabel(win, text="Редактировать дедлайн" if item else "Новый дедлайн", font=ctk.CTkFont(size=22, weight="bold")).pack(anchor="w", padx=30, pady=(24, 14))
        title = self.field(win, "Название задачи", "Например, подготовить презентацию")
        date = self.field(win, "Дата и время — ДД.ММ.ГГГГ ЧЧ:ММ", "15.09.2026 18:00")
        ctk.CTkLabel(win, text="Категория и приоритет", text_color="#c4cada").pack(anchor="w", padx=30, pady=(13, 5))
        selects = ctk.CTkFrame(win, fg_color="transparent"); selects.pack(fill="x", padx=30)
        category = ctk.CTkOptionMenu(selects, values=CATEGORIES); category.pack(side="left", fill="x", expand=True, padx=(0, 8))
        priority = ctk.CTkOptionMenu(selects, values=list(PRIORITIES)); priority.pack(side="left", fill="x", expand=True)
        ctk.CTkLabel(win, text="Заметка (необязательно)", text_color="#c4cada").pack(anchor="w", padx=30, pady=(13, 5))
        notes = ctk.CTkTextbox(win, height=74); notes.pack(fill="x", padx=30)
        if item:
            title.insert(0, item["title"]); date.insert(0, self.deadline(item).strftime(DATE_FORMAT)); category.set(item["category"]); priority.set(item["priority"]); notes.insert("1.0", item["notes"])
        else:
            date.insert(0, (datetime.now() + timedelta(days=1)).replace(minute=0, second=0, microsecond=0).strftime(DATE_FORMAT)); category.set("Учёба"); priority.set("Средний")
        def save():
            try: dt = datetime.strptime(date.get().strip(), DATE_FORMAT)
            except ValueError:
                messagebox.showerror("Неверная дата", "Используйте формат ДД.ММ.ГГГГ ЧЧ:ММ.", parent=win); return
            if not title.get().strip():
                messagebox.showerror("Нет названия", "Введите название задачи.", parent=win); return
            record = {"id": item["id"] if item else str(uuid.uuid4()), "title": title.get().strip(), "deadline": dt.isoformat(), "category": category.get(), "priority": priority.get(), "notes": notes.get("1.0", "end-1c").strip(), "done": item["done"] if item else False, "created": item.get("created") if item else datetime.now().isoformat()}
            self.items = [record if x["id"] == record["id"] else x for x in self.items] if item else self.items + [record]
            self.save_items(); self.refresh(); win.destroy()
        ctk.CTkButton(win, text="Сохранить дедлайн", command=save, height=38, fg_color="#6366f1", hover_color="#4f46e5", font=ctk.CTkFont(weight="bold")).pack(fill="x", padx=30, pady=22)

    def field(self, parent, label, placeholder):
        ctk.CTkLabel(parent, text=label, text_color="#c4cada").pack(anchor="w", padx=30, pady=(8, 5))
        entry = ctk.CTkEntry(parent, placeholder_text=placeholder, height=35); entry.pack(fill="x", padx=30)
        return entry

    def show_details(self, item):
        """Окно чтения: заметку можно быстро просмотреть без редактирования."""
        win = ctk.CTkToplevel(self)
        win.title("Детали задачи")
        win.geometry("470x400")
        win.minsize(470, 400)
        win.grab_set()
        _, state_caption, state_color = self.task_state(item)
        ctk.CTkLabel(win, text=item["title"], wraplength=400, justify="left", font=ctk.CTkFont(size=21, weight="bold")).pack(anchor="w", padx=28, pady=(25, 7))
        badge = ctk.CTkLabel(win, text=f"{item['category']}  •  {item['priority']}  •  {state_caption}", text_color=state_color)
        badge.pack(anchor="w", padx=28)
        ctk.CTkFrame(win, height=1, fg_color="#30384b").pack(fill="x", padx=28, pady=16)
        ctk.CTkLabel(win, text="Срок", text_color="#929bb0").pack(anchor="w", padx=28)
        ctk.CTkLabel(win, text=f"{self.deadline(item).strftime(DATE_FORMAT)}\n{self.left_text(item)}", justify="left", font=ctk.CTkFont(size=15)).pack(anchor="w", padx=28, pady=(2, 13))
        ctk.CTkLabel(win, text="Заметка", text_color="#929bb0").pack(anchor="w", padx=28)
        note = ctk.CTkTextbox(win, height=90)
        note.insert("1.0", item.get("notes") or "Заметки нет.")
        note.configure(state="disabled")
        note.pack(fill="both", expand=True, padx=28, pady=(2, 20))

    def add_examples(self):
        """Добавляет демонстрационные записи только по явному нажатию пользователя."""
        if self.items and not messagebox.askyesno("Примеры", "Добавить примеры к текущему списку?"):
            return
        now = datetime.now().replace(second=0, microsecond=0)
        examples = [
            ("Подготовить презентацию", now + timedelta(hours=20), "Учёба", "Высокий", "Проверить структуру и добавить иллюстрации."),
            ("Отправить отчёт команде", now + timedelta(days=2, hours=4), "Работа", "Средний", "Согласовать финальную версию."),
            ("Забронировать билеты", now + timedelta(days=7), "Личное", "Низкий", "Сравнить стоимость на разных датах."),
        ]
        for title, deadline, category, priority, notes in examples:
            self.items.append({"id": str(uuid.uuid4()), "title": title, "deadline": deadline.isoformat(), "category": category, "priority": priority, "notes": notes, "done": False, "created": now.isoformat()})
        self.save_items()
        self.refresh()

    # ===== Commands =====
    def toggle_done(self, item_id):
        item = self.get_item(item_id)
        if item: item["done"] = not item["done"]; self.save_items(); self.refresh()

    def duplicate(self, item_id):
        item = self.get_item(item_id)
        if item:
            clone = item.copy(); clone.update(id=str(uuid.uuid4()), title=f"Копия: {item['title']}", done=False, created=datetime.now().isoformat())
            self.items.append(clone); self.save_items(); self.refresh()

    def delete(self, item_id):
        item = self.get_item(item_id)
        if item and messagebox.askyesno("Удаление", f"Удалить «{item['title']}»?"):
            self.items = [x for x in self.items if x["id"] != item_id]; self.save_items(); self.refresh()

    def clear_done(self):
        count = sum(x["done"] for x in self.items)
        if count and messagebox.askyesno("Очистка", f"Удалить выполненные задачи ({count})?"):
            self.items = [x for x in self.items if not x["done"]]; self.save_items(); self.refresh()

    def export_data(self):
        target = filedialog.asksaveasfilename(title="Экспорт дедлайнов", defaultextension=".json", initialfile="my_deadlines.json", filetypes=[("JSON", "*.json")])
        if target:
            Path(target).write_text(json.dumps(self.items, ensure_ascii=False, indent=2), encoding="utf-8")
            messagebox.showinfo("Готово", "Дедлайны экспортированы.")

    def import_data(self):
        source = filedialog.askopenfilename(title="Импорт дедлайнов", filetypes=[("JSON", "*.json")])
        if not source: return
        try:
            loaded = json.loads(Path(source).read_text(encoding="utf-8"))
            if not isinstance(loaded, list): raise ValueError
            if messagebox.askyesno("Импорт", "Заменить текущий список импортированными задачами?"):
                self.items = self.normalize(loaded); self.save_items(); self.refresh()
        except (OSError, ValueError, json.JSONDecodeError):
            messagebox.showerror("Ошибка импорта", "Выберите корректный JSON-файл.")

    def show_statistics(self):
        win = ctk.CTkToplevel(self); win.title("Статистика"); win.geometry("400x330"); win.resizable(False, False); win.grab_set()
        total = len(self.items); done = sum(x["done"] for x in self.items); overdue = sum(self.task_state(x)[0] == "overdue" for x in self.items); progress = done / total if total else 0
        ctk.CTkLabel(win, text="Ваша статистика", font=ctk.CTkFont(size=22, weight="bold")).pack(pady=(28, 10))
        ctk.CTkLabel(win, text=f"Прогресс: {round(progress * 100)}%", font=ctk.CTkFont(size=16)).pack()
        bar = ctk.CTkProgressBar(win, width=300); bar.set(progress); bar.pack(pady=(8, 20))
        ctk.CTkLabel(win, text=f"Всего задач: {total}\nВыполнено: {done}\nАктивных: {total-done}\nПросрочено: {overdue}", justify="left", font=ctk.CTkFont(size=15), text_color="#c4cada").pack(anchor="w", padx=50)

    def show_help(self):
        """Короткая встроенная памятка — приложение можно освоить без README."""
        win = ctk.CTkToplevel(self)
        win.title("Как пользоваться")
        win.geometry("500x470")
        win.resizable(False, False)
        win.grab_set()
        ctk.CTkLabel(win, text="Дедлайн-трекер", font=ctk.CTkFont(size=22, weight="bold")).pack(anchor="w", padx=30, pady=(25, 6))
        ctk.CTkLabel(win, text="Небольшая памятка по возможностям приложения", text_color="#929bb0").pack(anchor="w", padx=30, pady=(0, 15))
        tips = [
            ("Ctrl + N", "создать новый дедлайн"),
            ("Ctrl + F", "быстро перейти к строке поиска"),
            ("Esc", "очистить поиск"),
            ("✓ / ↩", "выполнить задачу или вернуть её в работу"),
            ("i", "посмотреть срок и заметку без редактирования"),
            ("⋮", "дублировать либо удалить запись"),
        ]
        body = ctk.CTkFrame(win, fg_color="#181d2a", corner_radius=12)
        body.pack(fill="both", expand=True, padx=30, pady=(0, 15))
        for key, description in tips:
            row = ctk.CTkFrame(body, fg_color="transparent")
            row.pack(fill="x", padx=14, pady=5)
            ctk.CTkLabel(row, text=key, width=90, fg_color="#303a52", corner_radius=7, font=ctk.CTkFont(size=12, weight="bold")).pack(side="left")
            ctk.CTkLabel(row, text=description, text_color="#c4cada").pack(side="left", padx=12)
        ctk.CTkButton(win, text="Понятно", command=win.destroy, fg_color="#6366f1").pack(fill="x", padx=30, pady=(0, 22))

    def toggle_theme(self):
        ctk.set_appearance_mode("light" if ctk.get_appearance_mode() == "Dark" else "dark")

    def live_refresh(self):
        self.clock.configure(text=datetime.now().strftime("%d.%m.%Y  •  %H:%M"))
        self.refresh()
        self.after(60000, self.live_refresh)


if __name__ == "__main__":
    DeadlineTracker().mainloop()
