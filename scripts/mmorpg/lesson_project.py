"""Resolve the original lab or an explicitly enrolled copy/paste lesson project."""
from pathlib import Path
import json

def require_lesson_project():
    import unreal
    project = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
    project_file = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.get_project_file_path())).resolve()
    if project_file.name.lower() != 'mmorpg.uproject' or project_file.parent != project:
        raise RuntimeError('Open the expected MMORPG.uproject before using tutorial asset tools.')
    if str(project).replace('\\', '/').lower() == 'f:/ue/lyradoclabs/mmorpg':
        return project
    marker = project / '.lesson-state.json'
    if not marker.exists():
        raise RuntimeError('This project is not registered by Advance-Lesson.ps1.')
    state = json.loads(marker.read_text(encoding='utf-8-sig'))
    if state.get('stage', -1) < 3 or Path(state.get('destination', '')).resolve() != project:
        raise RuntimeError('Lesson marker must name this directory and completed source stage 3.')
    return project
