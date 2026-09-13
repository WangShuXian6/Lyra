"""Run the Training preparation twice in one fresh native Python commandlet.

The lab's existing assets are preserved. This checks a new editor process and
same-process repetition; it does not claim an empty-directory first creation.
The historical training-prepare-repeat.json failure is never overwritten.
"""

import argparse
from datetime import datetime, timezone
import gc
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import time


EXPECTED_WORLD = "/TrainingRange/Maps/L_TrainingRange.L_TrainingRange"
EXPECTED_ASSETS = (
    "/TrainingRange/Experiences/B_TrainingRange",
    "/TrainingRange/Game/HeroData_Training",
    "/TrainingRange/Game/AbilitySet_TrainingHero",
    "/TrainingRange/Game/GA_TrainingDash",
    "/TrainingRange/TrainingRange",
    "/TrainingRange/Maps/L_TrainingRange",
)


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def file_evidence(file):
    data = file.read_bytes()
    return {"path": str(file), "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def world_path(unreal):
    # Never retain the initial overview World across the first LoadLevel.
    value = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    return value.get_path_name() if value else None


def snapshot(unreal):
    library = unreal.EditorAssetLibrary
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = editor.get_editor_world()
    if not world or world.get_path_name() != EXPECTED_WORLD:
        raise RuntimeError("The preparation did not leave the expected Training editor World loaded.")
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    signs = [actor.get_path_name() for actor in actor_subsystem.get_all_level_actors()
             if actor.get_actor_label() == "Tutorial_TrainingSign"]
    if len(signs) != 1:
        raise RuntimeError("Expected exactly one original Tutorial_TrainingSign.")
    pawn = library.load_asset("/TrainingRange/Game/HeroData_Training")
    expected_set = "/TrainingRange/Game/AbilitySet_TrainingHero.AbilitySet_TrainingHero"
    ability_sets = [value.get_path_name() if value else None
                    for value in pawn.get_editor_property("ability_sets")]
    if ability_sets.count(expected_set) != 1:
        raise RuntimeError("PawnData must reference the training AbilitySet exactly once.")
    experience = unreal.get_default_object(
        library.load_blueprint_class("/TrainingRange/Experiences/B_TrainingRange"))
    configured_pawn = experience.get_editor_property("default_pawn_data")
    features = list(experience.get_editor_property("game_features_to_enable"))
    if configured_pawn != pawn or features.count("TrainingRange") != 1:
        raise RuntimeError("Experience PawnData or unique TrainingRange feature selection is incorrect.")
    spawner_class = unreal.load_class(None, "/Script/LyraGame.LyraWeaponSpawner")
    points = []
    for spawner in unreal.GameplayStatics.get_all_actors_of_class(world, spawner_class):
        projected = unreal.NavigationSystemV1.project_point_to_navigation(
            world, spawner.get_actor_location(), None, None, unreal.Vector(150, 150, 500))
        if projected is None:
            raise RuntimeError("No built navigation under " + spawner.get_path_name())
        points.append({"spawner": spawner.get_path_name(),
                       "projected": [projected.x, projected.y, projected.z]})
    if len(points) < 3:
        raise RuntimeError("Expected the original three ShooterGym weapon pickup points.")
    return {"world": world.get_path_name(), "signs": signs,
            "pawnData": pawn.get_path_name(), "abilitySets": ability_sets,
            "experienceClass": experience.get_class().get_path_name(),
            "features": features, "nativeProjectedPickupPoints": points}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--docs-root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args(sys.argv[1:])
    docs_root = args.docs_root.resolve()
    report_path = (args.report or docs_root / "verification/training-prepare-rerun.json").resolve()
    if report_path == (docs_root / "verification/training-prepare-repeat.json").resolve():
        raise RuntimeError("Preserve the historical GC failure report; choose a separate rerun report.")
    report = {"schemaVersion": 1, "startedAt": utc_now(), "finishedAt": None,
              "status": "running", "passed": False, "processId": os.getpid(),
              "scope": "Fresh native Python commandlet process using existing lab assets, then same-process preparation while retaining the first Training World reference.",
              "notCovered": ["empty-directory asset creation", "PIE gameplay", "Cook", "packaged runtime"],
              "historicalFailureReport": "verification/training-prepare-repeat.json",
              "runs": [], "errors": []}

    def persist():
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    persist()
    retained_world = None
    try:
        import unreal

        project = unreal.Paths.convert_relative_path_to_full(unreal.Paths.get_project_file_path())
        project_dir = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
        normalized = project_dir.replace("\\", "/").rstrip("/")
        report.update(project=project.replace("\\", "/"), engine=unreal.SystemLibrary.get_engine_version())
        if not normalized.lower().endswith("/lyradoclabs/lyratraining"):
            raise RuntimeError("Only the isolated LyraDocLabs/LyraTraining project may be prepared.")
        if Path(project).name.lower() != "lyrastartergame.uproject" or not report["engine"].startswith("5.8.1"):
            raise RuntimeError("Expected LyraStarterGame.uproject and the UE 5.8.1 validation baseline.")
        command_line = unreal.SystemLibrary.get_command_line()
        if not re.search(r"(?:^|\s)-run=pythonscript(?:\s|$)", command_line, re.IGNORECASE):
            raise RuntimeError("Launch a fresh -run=pythonscript commandlet; do not run this verifier inside an existing graphical editor.")
        report["pythonCommandletArgumentVerified"] = True
        editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
        if editor.get_game_world():
            raise RuntimeError("PIE must not be running during asset preparation.")
        lab_root = Path(project_dir)
        script = lab_root / "prepare_training.py"
        repo_script = docs_root / "examples/LyraTraining/prepare_training.py"
        report["labPreparationScript"] = file_evidence(script)
        report["repositoryPreparationScript"] = file_evidence(repo_script)
        if report["labPreparationScript"]["sha256"] != report["repositoryPreparationScript"]["sha256"]:
            raise RuntimeError("Copy the current original preparation script into the lab before verifying it.")
        code = compile(script.read_text(encoding="utf-8-sig"), str(script), "exec")
        report["initialEditorWorld"] = world_path(unreal)
        unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(["/TrainingRange"], True)
        report["initialAssets"] = {asset: bool(unreal.EditorAssetLibrary.does_asset_exist(asset))
                                   for asset in EXPECTED_ASSETS}
        if not all(report["initialAssets"].values()):
            raise RuntimeError("This rerun verifier requires all six existing Training assets; run the documented initial preparation separately.")
        unreal.load_module("LyraDocTools")
        if not getattr(unreal, "LyraDocToolsLibrary", None):
            raise RuntimeError("The compiled LyraDocTools Editor plugin is required.")
        generated_report = lab_root / "Saved/training-assets.json"
        for index, phase in enumerate(("fresh-commandlet-process", "same-process-repeat")):
            stage = {"phase": phase, "startedAt": utc_now(), "passed": False,
                     "worldBefore": world_path(unreal), "processId": os.getpid()}
            report["runs"].append(stage)
            persist()
            unreal.log("LYRADOC_PREPARE_VERIFY_BEGIN " + phase)
            before_ns = time.time_ns()
            namespace = {"__name__": "__training_prepare_" + str(index), "__file__": str(script)}
            try:
                exec(code, namespace, namespace)
            finally:
                # Only release this script's temporary wrappers, never global UE state.
                namespace.clear()
                gc.collect()
            if not generated_report.exists() or generated_report.stat().st_mtime_ns < before_ns:
                raise RuntimeError("This preparation pass did not write a fresh Saved/training-assets.json.")
            generated = json.loads(generated_report.read_text(encoding="utf-8"))
            if generated.get("dash_replacements") != 1:
                raise RuntimeError("Native AbilitySet replacement did not confirm exactly one Dash entry.")
            if len(generated.get("navigation", {}).get("projectedPickupPoints", [])) < 3:
                raise RuntimeError("The preparation pass did not report three native navigation projections.")
            stage["generatedReport"] = generated
            stage["generatedReportFile"] = file_evidence(generated_report)
            stage["snapshot"] = snapshot(unreal)
            current = editor.get_editor_world()
            if index == 0:
                # Deliberately keep the World wrapper that exposed the original GC bug.
                retained_world = current
                stage["retainedWorldForRepeat"] = current.get_path_name()
            else:
                stage["sameNativeWorldObject"] = current == retained_world
                if not stage["sameNativeWorldObject"]:
                    raise RuntimeError("The repeat pass replaced the existing Training World.")
                if stage["snapshot"]["signs"] != report["runs"][0]["snapshot"]["signs"]:
                    raise RuntimeError("The repeat pass replaced or duplicated the original training sign.")
            current = None
            map_file = lab_root / "Plugins/GameFeatures/TrainingRange/Content/Maps/L_TrainingRange.umap"
            stage["savedMap"] = file_evidence(map_file)
            stage.update(passed=True, finishedAt=utc_now())
            unreal.log("LYRADOC_PREPARE_VERIFY_PASS " + phase)
            persist()
        report["passed"] = len(report["runs"]) == 2 and all(stage["passed"] for stage in report["runs"])
    except Exception as error:
        report["errors"].append(str(error))
    finally:
        retained_world = None
        gc.collect()
        report.update(finishedAt=utc_now(), status="passed" if report["passed"] else "failed")
        persist()
    print("Training preparation rerun report: " + str(report_path))
    if not report["passed"]:
        raise RuntimeError("Native preparation rerun did not pass; inspect the saved report.")


if __name__ == "__main__":
    main()
