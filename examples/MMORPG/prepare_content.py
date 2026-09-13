"""Run inside the MMORPG editor after C++ compilation. Creates original tutorial assets.

UnrealEditor-Cmd.exe MMORPG.uproject -run=pythonscript -script=.../prepare_content.py
Never run this script in the original Lyra editor: the project check is intentional.
"""
import json
import os
import unreal

if unreal.Paths.get_project_file_path().replace("\\", "/").split("/")[-1] != "MMORPG.uproject":
    raise RuntimeError("Open MMORPG.uproject before generating tutorial assets")

unreal.load_module("MMOCore")
tools = unreal.AssetToolsHelpers.get_asset_tools()
library = unreal.EditorAssetLibrary


def blueprint(name, folder, parent):
    path = folder + "/" + name
    if library.does_asset_exist(path):
        return library.load_asset(path)
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.load_class(None, parent))
    asset = tools.create_asset(name, folder, unreal.Blueprint, factory)
    if not asset:
        raise RuntimeError("Blueprint creation failed: " + path)
    unreal.BlueprintEditorLibrary.compile_blueprint(asset)
    return asset


# Native animation generation uses real editor graph APIs and compiles both
# source graphs and clipboard round-trip copies before character configuration.
unreal.load_module("MMODocTools")
evidence_dir = os.path.join(unreal.Paths.project_saved_dir(), "Evidence")
os.makedirs(evidence_dir, exist_ok=True)
animation_report = json.loads(unreal.MMOAnimationTools.prepare_character_animation())
with open(os.path.join(evidence_dir, "mmorpg-animation-assets.json"), "w", encoding="utf-8") as output:
    json.dump(animation_report, output, ensure_ascii=False, indent=2)
if not animation_report.get("passed"):
    raise RuntimeError("Character animation generation failed: " + json.dumps(animation_report))

for name, parent in (
    ("BP_MMOCharacter", "/Script/MMORPG.MMOCharacter"),
    ("GA_ArcaneBolt", "/Script/MMORPG.MMOSpellAbility"),
    ("BP_MMOTrainingTarget", "/Script/MMORPG.MMOTarget"),
):
    library.save_loaded_asset(blueprint(name, "/Game/Tutorial", parent))

character_report = json.loads(unreal.MMOAnimationTools.configure_character(library.load_asset("/Game/Tutorial/BP_MMOCharacter")))
with open(os.path.join(evidence_dir, "mmorpg-character-defaults.json"), "w", encoding="utf-8") as output:
    json.dump(character_report, output, ensure_ascii=False, indent=2)
if not character_report.get("passed") or not character_report.get("defaultsRetainedAfterCompile"):
    raise RuntimeError("Character Mesh/AnimClass defaults failed native validation: " + json.dumps(character_report))


def data_asset(name, folder, class_name):
    path = folder + "/" + name
    if library.does_asset_exist(path):
        return library.load_asset(path)
    asset_class = unreal.load_class(None, class_name)
    if not asset_class:
        raise RuntimeError("Build and enable the asset class before generation: " + class_name)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", asset_class)
    asset = tools.create_asset(name, folder, asset_class, factory)
    if not asset:
        raise RuntimeError("Data Asset creation failed: " + path)
    return asset


experience = data_asset("DA_MMOExperience", "/Game/MMO/Experiences", "/Script/MMOFramework.MMOExperienceDefinition")
pawn_data = data_asset("DA_MMOPlayer", "/Game/MMO/Pawns", "/Script/MMOFramework.MMOPawnData")
ability_set = data_asset("DA_MMOAbilities", "/Game/MMO/Abilities", "/Script/MMOFramework.MMOAbilitySet")
experience_report = json.loads(unreal.MMODocExperienceTools.configure_experience_assets(experience, pawn_data, ability_set))
if not experience_report.get("passed"):
    raise RuntimeError("Experience asset configuration failed: " + json.dumps(experience_report))
for asset in (experience, pawn_data, ability_set):
    if not library.save_loaded_asset(asset):
        raise RuntimeError("Could not save Experience dependency: " + asset.get_path_name())
experience_report["saved"] = True
with open(experience_report["reportPath"], "w", encoding="utf-8") as output:
    json.dump(experience_report, output, ensure_ascii=False, indent=2)

game_mode = blueprint("BP_MMOGameMode", "/Game/MMO", "/Script/MMORPG.MMOGameMode")
unreal.BlueprintEditorLibrary.compile_blueprint(game_mode)
game_mode_class = library.load_blueprint_class("/Game/MMO/BP_MMOGameMode")
selected_experience = unreal.get_default_object(game_mode_class).get_editor_property("default_experience")
experience_id = unreal.SystemLibrary.conv_primary_asset_id_to_string(selected_experience)
if experience_id != "MMOExperience:DA_MMOExperience":
    raise RuntimeError("BP_MMOGameMode Default Experience is unexpected: " + experience_id)
if not library.save_loaded_asset(game_mode):
    raise RuntimeError("Could not save BP_MMOGameMode")
with open(os.path.join(evidence_dir, "mmorpg-game-mode.json"), "w", encoding="utf-8") as output:
    json.dump({"passed": True, "saved": True, "gameplayExecuted": False,
               "asset": "/Game/MMO/BP_MMOGameMode", "parent": "/Script/MMORPG.MMOGameMode",
               "defaultExperience": experience_id}, output, ensure_ascii=False, indent=2)

# Real Designer assets own every visual tree. C++ native parents only own
# business callbacks, CommonUI routing and the Manual PlayerVM lifecycle.
table_path = "/Game/Localization/ST_MMO"
if library.does_asset_exist(table_path):
    string_table = library.load_asset(table_path)
else:
    string_table = tools.create_asset("ST_MMO", "/Game/Localization", unreal.StringTable, unreal.StringTableFactory())
strings_report = json.loads(unreal.MMODocToolsLibrary.configure_designer_strings(string_table))
if not strings_report.get("passed") or not library.save_loaded_asset(string_table):
    raise RuntimeError("Designer StringTable failed: " + json.dumps(strings_report))
strings_report["saved"] = True

with open(os.path.join(unreal.Paths.project_dir(), "ui-designer-contract.json"), encoding="utf-8") as source:
    designer_contract = json.load(source)
designer_reports = []
for entry in designer_contract["assets"]:
    name, folder, parent = entry["name"], entry["path"], entry["parent"]
    path = folder + "/" + name
    if library.does_asset_exist(path):
        widget = library.load_asset(path)
    else:
        factory = unreal.WidgetBlueprintFactory()
        factory.set_editor_property("parent_class", unreal.load_class(None, parent))
        widget = tools.create_asset(name, folder, unreal.WidgetBlueprint, factory)
    # Required BindWidget children must exist before the explicit compile.
    report = json.loads(unreal.MMODocToolsLibrary.configure_designer_widget(widget))
    if not report.get("passed"):
        raise RuntimeError("Designer widget failed compilation: " + json.dumps(report))
    found = {row["name"]: row for row in report["widgets"]}
    for required_name in entry["widgets"]:
        if required_name not in found or not found[required_name]["isVariable"]:
            raise RuntimeError("Missing variable widget: " + name + "." + required_name)
    if report["bindingCount"] != len(entry.get("bindings", {})):
        raise RuntimeError("MVVM binding count mismatch: " + name)
    if not library.save_loaded_asset(widget):
        raise RuntimeError("Could not save Designer asset: " + path)
    report["saved"] = True
    with open(report["reportPath"], "w", encoding="utf-8") as output:
        json.dump(report, output, ensure_ascii=False, indent=2)
    designer_reports.append(report)
    if name == "WBP_ManaStatus":
        with open(os.path.join(unreal.Paths.project_saved_dir(), "Evidence/mana-status-bindings.json"), "w", encoding="utf-8") as output:
            json.dump(report, output, ensure_ascii=False, indent=2)

policy = blueprint("BP_MMOUIPolicy", "/Game/UI", "/Script/MMORPG.MMOUIPolicy")
policy_class = library.load_blueprint_class("/Game/UI/BP_MMOUIPolicy")
layout_class = library.load_blueprint_class("/Game/UI/WBP_MMOLayout")
unreal.get_default_object(policy_class).set_editor_property("layout_class", layout_class)
unreal.BlueprintEditorLibrary.compile_blueprint(policy)
if not library.save_loaded_asset(policy):
    raise RuntimeError("Could not save Designer UI Policy")
with open(os.path.join(unreal.Paths.project_saved_dir(), "Evidence/ui-designer-assets.json"), "w", encoding="utf-8") as output:
    json.dump({"passed": True, "saved": True, "gameplayExecuted": False,
               "stringTable": strings_report, "widgets": designer_reports,
               "policy": {"asset": "/Game/UI/BP_MMOUIPolicy", "layoutClass": layout_class.get_path_name()}},
              output, ensure_ascii=False, indent=2)

path = "/MMOCore/MMOCore"
if library.does_asset_exist(path):
    data = library.load_asset(path)
else:
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.GameFeatureData)
    data = tools.create_asset("MMOCore", "/MMOCore", unreal.GameFeatureData, factory)
unreal.MMOContentLibrary.configure_feature_data(data)
library.save_loaded_asset(data)

# This editor-only bridge builds native K2 nodes, exports their actual clipboard
# text, then imports that file into a separate Actor Blueprint and compiles it.
health = blueprint("BP_BackendHealth", "/Game/Tutorial", "/Script/Engine.Actor")
report = json.loads(unreal.MMODocToolsLibrary.configure_backend_tutorial(health))
if not report.get("passed"):
    raise RuntimeError("Backend health graph failed native validation: " + json.dumps(report))
library.save_loaded_asset(health)
library.save_asset(report["testAsset"])
report["saved"] = True
with open(report["reportPath"], "w", encoding="utf-8") as output:
    json.dump(report, output, ensure_ascii=False, indent=2)
unreal.log("MMORPG original tutorial assets created and Blueprint compilation requested")


