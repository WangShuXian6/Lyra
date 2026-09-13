"""Apply the Designer button-slot fix without rebuilding WidgetTrees or bindings.

Run inside the isolated MMORPG editor after stopping PIE. The project generator
uses the same HAlign_Fill setting for every UButton content slot.
"""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import unreal
from toolset_registry.helpers import compile_blueprint


def apply_button_slots():
    project = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    assert project.as_posix().lower().rstrip('/').endswith('/lyradoclabs/mmorpg')
    assert not unreal.EditorLevelLibrary.get_pie_worlds(True), 'Stop PIE before editing Widget Blueprints.'
    report = {'checkedAt': datetime.now(timezone.utc).isoformat(), 'passed': False,
              'change': 'Button content slot HorizontalAlignment=Fill; preserve AutoWrap, centered text, WidgetTrees and MVVM bindings.',
              'assets': []}
    names = ['WBP_MMOLayout', 'WBP_Login', 'WBP_ManaStatus', 'WBP_PlayerHUD', 'WBP_Inventory', 'WBP_Settings', 'WBP_Confirm']
    for name in names:
        bp = unreal.load_asset('/Game/UI/' + name)
        assert bp
        asset_file = project / 'Content/UI' / (name + '.uasset')
        before = hashlib.sha256(asset_file.read_bytes()).hexdigest()
        trees = {tree.get_path_name() for tree in unreal.ObjectIterator(unreal.WidgetTree) if tree.get_outer() == bp}
        buttons = [button for button in unreal.ObjectIterator(unreal.Button)
                   if button.get_outer() and button.get_outer().get_path_name() in trees]
        changed = []
        for button in buttons:
            label = button.get_child_at(0)
            assert isinstance(label, unreal.TextBlock), button.get_path_name()
            slot = label.get_editor_property('Slot')
            assert isinstance(slot, unreal.ButtonSlot)
            old = str(slot.get_editor_property('HorizontalAlignment'))
            slot.modify()
            slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_FILL)
            changed.append({'button': button.get_name(), 'label': label.get_name(), 'previous': old,
                            'current': str(slot.get_editor_property('HorizontalAlignment'))})
        compile_blueprint(bp, warnings_as_errors=True)
        assert unreal.EditorAssetLibrary.save_loaded_asset(bp, only_if_is_dirty=False)
        report['assets'].append({'asset': bp.get_path_name(), 'buttons': changed, 'buttonCount': len(buttons),
                                'compileStatus': 'BS_UP_TO_DATE', 'warningsAsErrors': True, 'saved': True,
                                'beforeSHA256': before, 'sha256': hashlib.sha256(asset_file.read_bytes()).hexdigest()})
    assert sum(row['buttonCount'] for row in report['assets']) == 21
    report['passed'] = True
    report['completedAt'] = datetime.now(timezone.utc).isoformat()
    path = Path(__file__).resolve().parents[2] / 'verification/mmorpg-button-slots.json'
    path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    unreal.log('MMO button content slots compiled and saved: ' + str(path))


apply_button_slots()
