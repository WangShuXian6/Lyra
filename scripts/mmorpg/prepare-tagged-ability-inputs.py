"""Change only the two input fields of the existing tutorial AbilitySet.

Run with the newly compiled MMORPG editor, with PIE stopped. This script neither
recreates ability arrays nor compiles/reconstructs any Blueprint graph.
"""
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path

import unreal


EXPECTED_PROJECT = "f:/ue/lyradoclabs/mmorpg/mmorpg.uproject"
ASSET_PATH = "/Game/MMO/Abilities/DA_MMOAbilities"
OBJECT_PATH = ASSET_PATH + ".DA_MMOAbilities"
EXPECTED_GRANTS = (
    ("/Script/MMORPG.MMOAttackAbility", "InputTag.MMO.Attack"),
    ("/Game/Tutorial/GA_ArcaneBolt.GA_ArcaneBolt_C", "InputTag.MMO.Spell"),
)


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def fingerprint(path):
    data = path.read_bytes()
    return {"path": path.as_posix(), "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest()}


def main():
    project = Path(unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.get_project_file_path())).resolve()
    if project.as_posix().lower() != EXPECTED_PROJECT:
        raise RuntimeError("Open the exact isolated MMORPG lab project before editing tagged input grants.")
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():
        raise RuntimeError("Stop PIE before changing the AbilitySet.")
    asset_file = project.parent / "Content/MMO/Abilities/DA_MMOAbilities.uasset"
    report_file = project.parent / "Saved/Evidence/mmorpg-tagged-ability-input-assets.json"
    report = {"schemaVersion": 1, "startedAt": utc_now(), "finishedAt": None,
              "processId": os.getpid(), "project": project.as_posix(),
              "engine": unreal.SystemLibrary.get_engine_version(),
              "asset": OBJECT_PATH, "passed": False, "saved": False,
              "graphsReconstructed": False, "gameplayExecuted": False,
              "scope": "Only InputTag and InputID of the two existing DA_MMOAbilities grants.",
              "errors": []}
    try:
        report["beforeAssetFile"] = fingerprint(asset_file)
        unreal.load_module("MMODocTools")
        library = unreal.EditorAssetLibrary
        if not library.does_asset_exist(ASSET_PATH):
            raise RuntimeError("The existing DA_MMOAbilities is required; this operation does not create it.")
        asset = library.load_asset(ASSET_PATH)
        if not asset or asset.get_path_name() != OBJECT_PATH:
            raise RuntimeError("Loaded an unexpected or missing AbilitySet.")
        if asset.get_class().get_path_name() != "/Script/MMOFramework.MMOAbilitySet":
            raise RuntimeError("The tutorial asset must be a native MMOAbilitySet instance.")
        native = json.loads(unreal.MMODocExperienceTools.configure_tagged_ability_inputs(asset))
        report["native"] = native
        if not native.get("passed") or native.get("saved") is not False:
            raise RuntimeError("Native precise input change failed, or unexpectedly saved an asset.")
        if native.get("asset") != OBJECT_PATH or not native.get("onlyInputFieldsChanged"):
            raise RuntimeError("Native report did not confirm the precise asset and preserved unrelated fields.")
        grants = native.get("after", {}).get("grantedAbilities", [])
        if len(grants) != 2:
            raise RuntimeError("Expected exactly two grants after the native operation.")
        for grant, (ability, tag) in zip(grants, EXPECTED_GRANTS):
            if grant.get("ability") != ability or grant.get("inputTag") != tag or grant.get("inputID") != -1:
                raise RuntimeError("Unexpected class, input tag or numeric input ID after configuration.")
        if not native.get("uniqueInputTags") or not native.get("inputIDsAreNone"):
            raise RuntimeError("Native tag uniqueness/input-ID checks did not pass.")
        if not library.save_loaded_asset(asset, only_if_is_dirty=False):
            raise RuntimeError("Saving the single configured AbilitySet failed.")
        report["saved"] = True
        report["savedAssetCount"] = 1
        report["afterAssetFile"] = fingerprint(asset_file)
        report["passed"] = True
    except Exception as error:
        report["errors"].append(str(error))
    finally:
        report["finishedAt"] = utc_now()
        report_file.parent.mkdir(parents=True, exist_ok=True)
        report_file.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if not report["passed"]:
        raise RuntimeError("Tagged AbilitySet configuration failed; inspect " + str(report_file))
    unreal.log("MMO tagged ability inputs saved and verified: " + str(report_file))


main()
