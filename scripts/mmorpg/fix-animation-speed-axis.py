"""Repair the existing editable animation graphs through UE native pin APIs.

Run in the isolated MMORPG editor, with PIE stopped. This preserves node GUIDs,
positions, animation layers and all unrelated blueprint content.
"""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import unreal
from toolset_registry.helpers import compile_blueprint


def fix_animation_speed_axis():
    project = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    assert project.as_posix().lower().rstrip('/').endswith('/lyradoclabs/mmorpg')
    assert not unreal.EditorLevelLibrary.get_pie_worlds(True), 'Stop PIE before editing animation graphs.'
    report = {'checkedAt': datetime.now(timezone.utc).isoformat(), 'passed': False,
              'change': 'UE template BS_Idle_Walk_Run uses X=Direction and Y=Speed. GroundSpeed must drive Y.',
              'assets': []}
    for name in ['ABP_MMOUnarmed', 'ABP_MMOCharacter']:
        bp = unreal.load_asset('/Game/Characters/MMO/' + name)
        assert bp
        asset_file = project / 'Content/Characters/MMO' / (name + '.uasset')
        before_sha = hashlib.sha256(asset_file.read_bytes()).hexdigest()
        editor = unreal.BlueprintGraphEditor.get_graph_editor_by_name(bp, 'Locomotion')
        assert editor
        nodes = editor.list_all_nodes()
        before_nodes = sorted(node.get_path_name() for node in nodes)
        blend_nodes = [node for node in nodes if node.get_class().get_name() == 'AnimGraphNode_BlendSpacePlayer']
        assert len(blend_nodes) == 1
        blend = blend_nodes[0]
        inputs = {str(pin.get_pin_name()): pin for pin in blend.list_all_pins()}
        speed_pins = [pin for node in nodes for pin in node.list_all_pins()
                      if str(pin.get_pin_name()) == 'GroundSpeed']
        assert len(speed_pins) == 1
        speed = speed_pins[0]
        x, y = inputs['X'], inputs['Y']
        old_links = [str(pin.get_pin_name()) for pin in speed.list_connected_pins()]
        assert all(pin.is_same_native_pin(x) or pin.is_same_native_pin(y)
                   for pin in speed.list_connected_pins()), 'Unexpected speed output consumers; inspect before changing.'
        with unreal.ScopedEditorTransaction('Connect MMO ground speed to blend-space Speed axis'):
            bp.modify()
            blend.modify()
            speed.get_owning_node().modify()
            if any(pin.is_same_native_pin(x) for pin in speed.list_connected_pins()):
                assert speed.break_single_pin_link(x)
            assert not x.list_connected_pins()
            assert x.set_pin_value('0.0')
            assert speed.try_create_connection(y)
            assert len(y.list_connected_pins()) == 1
            assert y.list_connected_pins()[0].is_same_native_pin(speed)
            assert sorted(node.get_path_name() for node in editor.list_all_nodes()) == before_nodes
            compile_blueprint(bp, warnings_as_errors=True)
            assert unreal.EditorAssetLibrary.save_loaded_asset(bp, only_if_is_dirty=False)
        report['assets'].append({'asset': bp.get_path_name(), 'graph': 'Locomotion',
                                'blendSpaceNode': blend.get_name(), 'previousSpeedConsumers': old_links,
                                'speedInput': 'Y', 'directionInput': 'X', 'directionLiteral': x.get_pin_value(),
                                'nodePathsPreserved': True, 'nodeCount': len(nodes),
                                'compileStatus': 'BS_UP_TO_DATE', 'warningsAsErrors': True,
                                'beforeSHA256': before_sha, 'sha256': hashlib.sha256(asset_file.read_bytes()).hexdigest()})
    report['passed'] = True
    report['completedAt'] = datetime.now(timezone.utc).isoformat()
    path = Path(__file__).resolve().parents[2] / 'verification/mmorpg-animation-speed-axis.json'
    path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    unreal.log('MMO animation Speed-axis repair compiled and saved: ' + str(path))


fix_animation_speed_axis()
