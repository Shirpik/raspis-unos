"""Shared calendar and reservation semantics for short-period CP-SAT inputs."""
from collections import defaultdict
from datetime import date, timedelta


def course(group):
    explicit = group.get('course_year', 0)
    tail = group['name'].rsplit('-', 1)[-1]
    return explicit or (int(tail[0]) if tail and tail[0].isdigit() else 0)


def balances(data, before):
    records = {}
    for row in data.get('teaching_ledger', []):
        status = row.get('status')
        if status not in ('confirmed', 'planned') or row.get('is_class_hour'):
            continue
        if not row.get('date') or row['date'] >= before.isoformat():
            continue
        if row.get('lesson_id', -1) < 0 or not 1 <= row.get('slot', 0) <= 7 or row.get('hours', 0) <= 0:
            continue
        key = row['lesson_id'], row['date'], row['slot']
        if key not in records or status == 'confirmed':
            records[key] = row
    confirmed, reserved = defaultdict(int), defaultdict(int)
    for row in records.values():
        (confirmed if row['status'] == 'confirmed' else reserved)[row['lesson_id']] += row['hours']
    return confirmed, reserved, list(records.values())


def deadline(group, settings):
    first = settings['semester_start_date']
    end = date.fromisoformat(settings.get('first_course_semester_end_date', settings['semester_end_date'])
                            if course(group) == 1 else settings['semester_end_date'])
    if group.get('teaching_deadline'):
        end = min(end, date.fromisoformat(group['teaching_deadline']))
    for period in group.get('practice_periods', []):
        if period['to'] >= first and period['from'] <= end.isoformat():
            end = min(end, date.fromisoformat(period['from']) - timedelta(days=1))
    calendar = group.get('academic_calendar', [])
    for week in calendar:
        if week.get('pp_hours', 0) > 0 and week['to'] >= first and week['from'] <= end.isoformat():
            end = min(end, date.fromisoformat(week['from']) - timedelta(days=1))
    theory = [min(end, date.fromisoformat(w['to'])) for w in calendar
              if w['to'] >= first and w['from'] <= end.isoformat() and
              w.get('theory_hours', 0) > 0 and not w.get('vacation') and not w.get('pp_hours')]
    if theory:
        end = max(theory)
    return end


def calendar_allows(group, current):
    if not group.get('academic_calendar'):
        return True
    return any(w['from'] <= current.isoformat() <= w['to'] and w.get('theory_hours', 0) > 0 and
               not w.get('vacation') and not w.get('pp_hours') for w in group['academic_calendar'])
