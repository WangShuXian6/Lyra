"""Read the loaded scan configuration and existing label after an editor restart."""
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path
import unreal

project = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
if project.as_posix().rstrip('/').lower() != 'f:/ue/lyradoclabs/mmorpg':
    raise RuntimeError('Validate the health Cook label only in the isolated MMORPG lab.')
report = {'checkedAt': datetime.now(timezone.utc).isoformat(), 'passed': False,
          'assetsModified': False, 'cookExecuted': False, 'scan': []}
try:
    unreal.AssetRegistryHelpers.get_asset_registry().wait_for_completion()
    settings = unreal.get_default_object(unreal.load_class(None, '/Script/Engine.AssetManagerSettings'))
    labels = [entry for entry in settings.get_editor_property('PrimaryAssetTypesToScan')
              if str(entry.get_editor_property('primary_asset_type')) == 'PrimaryAssetLabel']
    for entry in labels:
        report['scan'].append({
            'hasBlueprintClasses': bool(entry.get_editor_property('has_blueprint_classes')),
            'isEditorOnly': bool(entry.get_editor_property('is_editor_only')),
            'directories': [str(value.get_editor_property('path')) for value in entry.get_editor_property('directories')],
            'alwaysCook': entry.get_editor_property('rules').get_editor_property('cook_rule') == unreal.PrimaryAssetCookRule.ALWAYS_COOK})
    if (len(report['scan']) != 1 or report['scan'][0] != {
            'hasBlueprintClasses': False, 'isEditorOnly': False,
            'directories': ['/Game/MMO/Labels'], 'alwaysCook': True}):
        raise RuntimeError('The running editor has not loaded the intended label scan configuration.')
    label = unreal.EditorAssetLibrary.load_asset('/Game/MMO/Labels/DA_TutorialExamples')
    if not isinstance(label, unreal.PrimaryAssetLabel):
        raise RuntimeError('The explicit tutorial label is missing.')
    primary_id = unreal.SystemLibrary.get_primary_asset_id_from_object(label)
    report['primaryAssetId'] = unreal.SystemLibrary.conv_primary_asset_id_to_string(primary_id)
    report['explicitBlueprints'] = [value.get_path_name() for value in label.get_editor_property('explicit_blueprints')]
    report['runtimeLabel'] = bool(label.get_editor_property('is_runtime_label'))
    report['directoryLabel'] = bool(label.get_editor_property('label_assets_in_my_directory'))
    if (report['primaryAssetId'] != 'PrimaryAssetLabel:DA_TutorialExamples'
            or report['explicitBlueprints'] != ['/Game/Tutorial/BP_BackendHealth.BP_BackendHealth_C']
            or report['runtimeLabel'] or report['directoryLabel']
            or label.get_editor_property('explicit_assets')
            or label.get_editor_property('rules').get_editor_property('cook_rule') != unreal.PrimaryAssetCookRule.ALWAYS_COOK):
        raise RuntimeError('The existing label no longer has the expected explicit Cook scope.')
    package = project / 'Content/MMO/Labels/DA_TutorialExamples.uasset'
    report['assetSHA256'] = hashlib.sha256(package.read_bytes()).hexdigest()
    report['configSHA256'] = hashlib.sha256((project / 'Config/DefaultGame.ini').read_bytes()).hexdigest()
    report['passed'] = True
except Exception as error:
    report['error'] = str(error)
    raise
finally:
    output = project / 'Saved/Evidence/mmorpg-health-cook-label-scan.json'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
unreal.log('MMO explicit label scan verified in the current process without modifying assets.')
