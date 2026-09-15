"""Create one explicit Cook label with native UE APIs; never rebuild tutorial graphs."""
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path
import unreal

import runpy
project = runpy.run_path(str(Path(__file__).with_name('lesson_project.py')))['require_lesson_project']()

name = 'DA_TutorialExamples'
folder = '/Game/MMO/Labels'
asset_path = folder + '/' + name
blueprint_class_path = '/Game/Tutorial/BP_BackendHealth.BP_BackendHealth_C'
config_path = project / 'Config/DefaultGame.ini'
config = config_path.read_text(encoding='utf-8-sig')
scan_lines = [line for line in config.splitlines() if line.startswith('+PrimaryAssetTypesToScan=')
              and 'PrimaryAssetType="PrimaryAssetLabel"' in line]
if len(scan_lines) != 1 or not all(value in scan_lines[0] for value in (
        'bHasBlueprintClasses=False', 'bIsEditorOnly=False', '/Game/MMO/Labels', 'CookRule=AlwaysCook')):
    raise RuntimeError('Install the explicit PrimaryAssetLabel scan rule in DefaultGame.ini first.')

label = unreal.EditorAssetLibrary.load_asset(asset_path) if unreal.EditorAssetLibrary.does_asset_exist(asset_path) else None
if label is None:
    unreal.EditorAssetLibrary.make_directory(folder)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property('data_asset_class', unreal.PrimaryAssetLabel)
    label = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.PrimaryAssetLabel, factory)
if not isinstance(label, unreal.PrimaryAssetLabel):
    raise RuntimeError('The dedicated label path already exists with an unexpected class.')

health_class = unreal.load_class(None, blueprint_class_path)
if not health_class:
    raise RuntimeError('Compile and save BP_BackendHealth before preparing its Cook label.')
rules = unreal.PrimaryAssetRules()
rules.set_editor_property('priority', 1)
rules.set_editor_property('chunk_id', -1)
rules.set_editor_property('apply_recursively', True)
rules.set_editor_property('cook_rule', unreal.PrimaryAssetCookRule.ALWAYS_COOK)
label.modify()
label.set_editor_property('rules', rules)
label.set_editor_property('label_assets_in_my_directory', False)
label.set_editor_property('is_runtime_label', False)
label.set_editor_property('include_redirectors', False)
label.set_editor_property('explicit_assets', [])
label.set_editor_property('explicit_blueprints', [health_class])
label.set_editor_property('asset_collection', unreal.CollectionReference())
if not unreal.EditorAssetLibrary.save_loaded_asset(label, only_if_is_dirty=False):
    raise RuntimeError('Saving the explicit health Cook label failed.')

settings_class = unreal.load_class(None, '/Script/Engine.AssetManagerSettings')
if not settings_class:
    raise RuntimeError('The native AssetManagerSettings class is unavailable.')
settings = unreal.get_default_object(settings_class)
current_scan = []
scan_inspection_error = None
try:
    current_scans = [entry for entry in settings.get_editor_property('PrimaryAssetTypesToScan')
                     if str(entry.get_editor_property('primary_asset_type')) == 'PrimaryAssetLabel']
    current_scan = [{'isEditorOnly': bool(entry.get_editor_property('is_editor_only')),
                     'hasBlueprintClasses': bool(entry.get_editor_property('has_blueprint_classes')),
                     'directories': [str(directory.get_editor_property('path')) for directory in entry.get_editor_property('directories')],
                     'cookRule': str(entry.get_editor_property('rules').get_editor_property('cook_rule'))}
                    for entry in current_scans]
except Exception as error:
    # Some native DeveloperSettings properties are not exposed to the Python bridge.
    # Keep the asset check separate; a fresh editor/Cook must validate the scan.
    scan_inspection_error = str(error)
saved_rules = label.get_editor_property('rules')
references = [value.get_path_name() for value in label.get_editor_property('explicit_blueprints')]
package = project / 'Content/MMO/Labels/DA_TutorialExamples.uasset'
passed = (references == [blueprint_class_path]
          and not label.get_editor_property('label_assets_in_my_directory')
          and not label.get_editor_property('is_runtime_label')
          and not label.get_editor_property('explicit_assets')
          and saved_rules.get_editor_property('cook_rule') == unreal.PrimaryAssetCookRule.ALWAYS_COOK)
report = {
    'checkedAt': datetime.now(timezone.utc).isoformat(), 'passed': passed, 'saved': True,
    'asset': asset_path, 'primaryAssetId': unreal.SystemLibrary.conv_primary_asset_id_to_string(
        unreal.SystemLibrary.get_primary_asset_id_from_object(label)),
    'explicitBlueprints': references, 'explicitAssets': [],
    'labelAssetsInMyDirectory': False, 'isRuntimeLabel': False, 'includeRedirectors': False,
    'rules': {'priority': 1, 'chunkId': -1, 'applyRecursively': True, 'cookRule': 'AlwaysCook'},
    'sourceGraphsReconstructed': False, 'testAssetsAdded': False, 'cookExecuted': False,
    'currentEditorScan': current_scan,
    'currentEditorScanInspectionError': scan_inspection_error,
    'editorRestartRequired': not (len(current_scan) == 1 and not current_scan[0]['isEditorOnly']
                                 and current_scan[0]['directories'] == ['/Game/MMO/Labels']),
    'persistedScanLine': scan_lines[0], 'configSHA256': hashlib.sha256(config_path.read_bytes()).hexdigest(),
    'assetFile': package.as_posix(), 'assetBytes': package.stat().st_size,
    'assetSHA256': hashlib.sha256(package.read_bytes()).hexdigest(),
    'meaning': 'The label itself is editor-only; its explicit Blueprint and dependencies are AlwaysCook. Type scan IsEditorOnly=false permits this Cook inclusion.'
}
evidence = project / 'Saved/Evidence/mmorpg-health-cook-label.json'
evidence.parent.mkdir(parents=True, exist_ok=True)
evidence.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
if not passed:
    raise RuntimeError('Health Cook label properties failed verification: ' + evidence.as_posix())
unreal.log('MMO health Cook label saved; restart the editor to reload changed scan settings when report.editorRestartRequired is true.')
