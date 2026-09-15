"""Validate existing original animation graphs; never invoke the graph builder."""
import json
from pathlib import Path
import unreal

import runpy
project = runpy.run_path(str(Path(__file__).with_name('lesson_project.py')))['require_lesson_project']()
result = json.loads(unreal.MMOAnimationTools.validate_character_animation())
evidence = project / 'Saved/Evidence/mmorpg-animation-assets.json'
evidence.parent.mkdir(parents=True, exist_ok=True)
evidence.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
if not result.get('passed') or not result.get('saved') or not result.get('originalNodeGuidsPreserved'):
    raise RuntimeError('Existing animation graph validation failed: ' + str(evidence))
unreal.log('MMO existing animation compiled and round-tripped with original node GUIDs preserved.')
