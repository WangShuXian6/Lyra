"""Validate existing original animation graphs; never invoke the graph builder."""
import json
from pathlib import Path
import unreal

project = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
if project.as_posix().rstrip('/').lower() != 'f:/ue/lyradoclabs/mmorpg':
    raise RuntimeError('Animation validation must run in the isolated MMORPG lab.')
result = json.loads(unreal.MMOAnimationTools.validate_character_animation())
evidence = project / 'Saved/Evidence/mmorpg-animation-assets.json'
evidence.parent.mkdir(parents=True, exist_ok=True)
evidence.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
if not result.get('passed') or not result.get('saved') or not result.get('originalNodeGuidsPreserved'):
    raise RuntimeError('Existing animation graph validation failed: ' + str(evidence))
unreal.log('MMO existing animation compiled and round-tripped with original node GUIDs preserved.')
