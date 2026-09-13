"""Create teaching assets using Unreal's native asset/level APIs in the isolated lab.

Run with the 5.8.1 editor, PythonScriptPlugin and EditorScriptingUtilities enabled.
Official prerequisites remain in their original plugin; only clones are edited.
"""
import json
import os
import unreal

project_dir = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()).replace('\\', '/').rstrip('/')
if not project_dir.lower().endswith('/lyradoclabs/lyratraining'):
    raise RuntimeError('This script requires the isolated LyraDocLabs/LyraTraining project')

lib = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
    ['/Game', '/ShooterCore', '/TrainingRange'], True)
evidence = []

def clone(source, destination):
    asset = lib.load_asset(destination) if lib.does_asset_exist(destination) else lib.duplicate_asset(source, destination)
    if not asset:
        raise RuntimeError('Failed to copy ' + source)
    evidence.append({'source': source, 'destination': destination})
    return asset

def data_asset(name, folder, cls):
    path = folder + '/' + name
    if lib.does_asset_exist(path):
        return lib.load_asset(path)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property('data_asset_class', cls)
    return tools.create_asset(name, folder, cls, factory)

root = '/TrainingRange'
experience = clone('/ShooterCore/Experiences/B_ShooterGame_Elimination', root + '/Experiences/B_TrainingRange')
pawn = clone('/ShooterCore/Game/HeroData_ShooterGame', root + '/Game/HeroData_Training')
abilities = clone('/ShooterCore/Game/AbilitySet_ShooterHero', root + '/Game/AbilitySet_TrainingHero')
dash = clone('/ShooterCore/Game/Dash/GA_Hero_Dash', root + '/Game/GA_TrainingDash')
unreal.BlueprintEditorLibrary.compile_blueprint(dash)
dash_class = lib.load_blueprint_class(root + '/Game/GA_TrainingDash')
bridge = unreal.LyraDocToolsLibrary
replaced = bridge.replace_ability_in_set(abilities, lib.load_blueprint_class('/ShooterCore/Game/Dash/GA_Hero_Dash'), dash_class)
if replaced != 1:
    raise RuntimeError('Expected exactly one official Dash grant, found ' + str(replaced))
pawn_sets = list(pawn.get_editor_property('ability_sets'))
pawn.set_editor_property('ability_sets', [abilities if x.get_name() == 'AbilitySet_ShooterHero' else x for x in pawn_sets])
experience_cdo = unreal.get_default_object(lib.load_blueprint_class(root + '/Experiences/B_TrainingRange'))
experience_cdo.set_editor_property('default_pawn_data', pawn)
features = list(experience_cdo.get_editor_property('game_features_to_enable'))
if 'TrainingRange' not in features:
    features.append('TrainingRange')
experience_cdo.set_editor_property('game_features_to_enable', features)
unreal.BlueprintEditorLibrary.compile_blueprint(experience)

feature = data_asset('TrainingRange', root, unreal.GameFeatureData)
if not bridge.configure_training_feature(feature):
    raise RuntimeError('Training GameFeatureData configuration failed')
for asset in (dash, abilities, pawn, experience, feature):
    if not lib.save_loaded_asset(asset):
        raise RuntimeError('Save failed: ' + asset.get_path_name())

level = clone('/ShooterCore/Maps/L_ShooterGym', root + '/Maps/L_TrainingRange')
level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = editor.get_editor_world()
training_world_path = root + '/Maps/L_TrainingRange.L_TrainingRange'
# Do not unload and reload the same World while Python wrappers still reference it.
# This is necessary when rerunning the script from the already-open training map.
if not world or world.get_path_name() != training_world_path:
    world = None
    if not level_editor.load_level(root + '/Maps/L_TrainingRange'):
        raise RuntimeError('Training level failed to open')
    world = editor.get_editor_world()
if not bridge.configure_training_world(world, lib.load_blueprint_class(root + '/Experiences/B_TrainingRange')):
    raise RuntimeError('Training map Experience configuration failed')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
# An original sign distinguishes the tutorial map. Repeat runs update the same actor.
sign = next((a for a in actors.get_all_level_actors() if a.get_actor_label() == 'Tutorial_TrainingSign'), None)
if not sign:
    sign = actors.spawn_actor_from_class(unreal.TextRenderActor, unreal.Vector(0, 0, 280), unreal.Rotator(pitch=0, yaw=180, roll=0))
    sign.set_actor_label('Tutorial_TrainingSign')
sign.set_actor_rotation(unreal.Rotator(pitch=0, yaw=180, roll=0), False)
text = sign.get_component_by_class(unreal.TextRenderComponent)
text.set_text('LYRA TRAINING\nMOVE / DASH / SHOOT')
text.set_world_size(35)
text.set_text_render_color(unreal.Color(35, 70, 45, 255))
# The native navigation helper works in both graphical editors and commandlets.
# BUILDPATHS opens graphical progress UI and is unsafe in a Python commandlet.
if not bridge.build_training_navigation(world):
    raise RuntimeError('Training native navigation build failed')
navigation_points = []
spawner_class = unreal.load_class(None, '/Script/LyraGame.LyraWeaponSpawner')
for spawner in unreal.GameplayStatics.get_all_actors_of_class(world, spawner_class):
    projected = unreal.NavigationSystemV1.project_point_to_navigation(
        world, spawner.get_actor_location(), None, None, unreal.Vector(150, 150, 500))
    if projected is None:
        raise RuntimeError('Build Paths did not produce navigation below ' + spawner.get_path_name())
    navigation_points.append({'spawner': spawner.get_path_name(), 'projected': [projected.x, projected.y, projected.z]})
if len(navigation_points) < 3:
    raise RuntimeError('Expected the three original ShooterGym weapon spawners for navigation verification')
if not level_editor.save_current_level():
    raise RuntimeError('Training level failed to save')
report = {'project': project_dir, 'engine': unreal.SystemLibrary.get_engine_version(), 'created': evidence, 'dash_replacements': replaced, 'navigation': {'nativeOperation': 'LyraDocTools.BuildTrainingNavigation / UNavigationSystemV1::Build', 'projectedPickupPoints': navigation_points}}
report_path = os.path.join(project_dir, 'Saved', 'training-assets.json')
os.makedirs(os.path.dirname(report_path), exist_ok=True)
with open(report_path, 'w', encoding='utf-8') as out:
    json.dump(report, out, ensure_ascii=False, indent=2)
unreal.log('Training assets created; next run the gameplay and paste/compile checks')
