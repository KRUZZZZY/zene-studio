"""Owner-defined feature categories and explicit difficulty; never changes release gates."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent
POLICY = ROOT / 'FEATURE-CATEGORIES.json'


def categories():
    if not POLICY.exists():
        return {}
    return {row['id']: row for row in json.loads(POLICY.read_text())['categories']}


def difficulty(unit):
    value = unit.get('owner_difficulty')
    if value is None:
        value = categories().get(unit.get('category'), {}).get('difficulty')
    if value is not None and (isinstance(value, bool) or not isinstance(value, (int, float)) or not 1 <= value <= 3):
        raise ValueError('Owner difficulty must be a number from 1 to 3')
    return value


def difficulty_pct(unit):
    value = difficulty(unit)
    return None if value is None else (value - 1) / 2


def annotate(unit):
    category = categories().get(unit.get('category'), {})
    result = dict(unit)
    if category:
        result.update(category_label=category['title'], category_order=category['order'])
    value = difficulty(unit)
    if value is not None:
        result.update(owner_difficulty=value, difficulty_score=value, difficulty_source='owner-assigned')
    return result


def order(unit):
    category = categories().get(unit.get('category'), {})
    priority = str(unit.get('priority', 'P99')).removeprefix('P')
    return (category.get('order', 100), int(priority) if priority.isdigit() else 99,
            unit.get('category_sequence', 999999), unit.get('rank', 999999),
            str(unit.get('cluster') or unit.get('queue_id', '')))
