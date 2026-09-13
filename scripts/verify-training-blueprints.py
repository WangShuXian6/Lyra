"""Run in the isolated LyraTraining editor's Python commandlet after building LyraDocTools.

Default evidence: <LyraDoc>/verification/training-blueprint-roundtrip.json.
Optional script arguments: --docs-root <repo> --report <absolute-json-path>.
This script never calls UBT, changes editor processes, or modifies official assets.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import sys
import uuid


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--docs-root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--report", type=Path)
    return parser.parse_args(sys.argv[1:])


def main():
    args = arguments()
    docs_root = args.docs_root.resolve()
    report_path = (args.report or docs_root / "verification/training-blueprint-roundtrip.json").resolve()
    run_id = datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S") + "_" + uuid.uuid4().hex[:8]
    report = {
        "schemaVersion": 1,
        "startedAt": utc_now(),
        "runId": run_id,
        "status": "running",
        "passed": False,
        "docsRoot": str(docs_root),
        "reportPath": str(report_path),
        "cases": [],
        "errors": [],
        "scope": "Native clipboard import and compilation in complete Blueprint duplicates; gameplay is not tested here.",
    }

    try:
        import unreal

        project_file = unreal.Paths.convert_relative_path_to_full(unreal.Paths.get_project_file_path())
        project_dir = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
        normalized_dir = project_dir.replace("\\", "/").rstrip("/")
        report["project"] = project_file.replace("\\", "/")
        report["engine"] = unreal.SystemLibrary.get_engine_version()
        if not normalized_dir.lower().endswith("/lyradoclabs/lyratraining"):
            raise RuntimeError("This script requires the isolated LyraDocLabs/LyraTraining project.")
        if Path(project_file).name.lower() != "lyrastartergame.uproject":
            raise RuntimeError("Expected the LyraStarterGame.uproject copy in the training lab.")
        if not report["engine"].startswith("5.8.1"):
            raise RuntimeError("This verification baseline requires UE 5.8.1; record a separate baseline for another engine.")
        if not (docs_root / "public/blueprints/lyra").is_dir():
            raise RuntimeError("--docs-root must point to the LyraDoc repository containing the native Blueprint text.")

        unreal.load_module("LyraDocTools")
        native = getattr(unreal, "LyraDocToolsLibrary", None)
        if native is None:
            raise RuntimeError("LyraDocToolsLibrary is unavailable. Copy, enable and build the Editor plugin first.")
        library = unreal.EditorAssetLibrary
        unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
            ["/Game", "/ShooterCore", "/TrainingRange"], True)
        cases = (
            {
                "name": "jump-event-graph",
                "source": "/Game/Characters/Heroes/Abilities/GA_Hero_Jump",
                "graph": "EventGraph",
                "testPrefix": "GA_JumpClipboardTest",
            },
            {
                "name": "dash-direction",
                "source": "/ShooterCore/Game/Dash/GA_Hero_Dash",
                "graph": "SelectDirectionalMontage",
                "testPrefix": "GA_DashClipboardTest",
            },
            {
                "name": "root-layout-registration",
                "source": "/Game/UI/W_OverallUILayout",
                "graph": "EventGraph",
                "testPrefix": "W_LayoutClipboardTest",
            },
        )
        for case in cases:
            clipboard = docs_root / "public/blueprints/lyra" / (case["name"] + ".txt")
            destination = "/TrainingRange/Tests/" + case["testPrefix"] + "_" + run_id
            result = {
                "name": case["name"],
                "source": case["source"],
                "destination": destination,
                "graph": case["graph"],
                "clipboardPath": str(clipboard),
                "passed": False,
                "saved": False,
                "errors": [],
            }
            report["cases"].append(result)
            try:
                clipboard_bytes = clipboard.read_bytes()
                result["clipboardSha256"] = hashlib.sha256(clipboard_bytes).hexdigest()
                result["clipboardBytes"] = len(clipboard_bytes)
                if not clipboard_bytes:
                    raise RuntimeError("The native clipboard text is empty.")
                source = library.load_asset(case["source"])
                if not isinstance(source, unreal.Blueprint):
                    raise RuntimeError("Official source did not load as a Blueprint: " + case["source"])
                if library.does_asset_exist(destination):
                    raise RuntimeError("Unique test destination already exists; refusing to overwrite it.")

                # Duplicate the complete Blueprint: parent, member variables, function graphs,
                # entry/result signatures and local variable definitions remain available.
                # WidgetBlueprint duplicates also retain the complete Designer WidgetTree.
                # The unique package avoids overwriting a previous successful or failed test.
                test_copy = library.duplicate_asset(case["source"], destination)
                if not isinstance(test_copy, unreal.Blueprint):
                    raise RuntimeError("Unable to create the complete training test duplicate.")
                native_report = json.loads(native.round_trip_blueprint(test_copy, case["graph"], str(clipboard)))
                result["native"] = native_report
                if native_report.get("passed") is not True or native_report.get("compiledThisCall") is not True:
                    raise RuntimeError("Native import/compile verification did not pass; see the native errors in this case.")
                if native_report.get("nodeCount", 0) <= 0:
                    raise RuntimeError("Native verification returned no graph nodes.")
                # The native compiler uses SkipSave. Save only a test that really passed.
                result["saved"] = bool(library.save_loaded_asset(test_copy))
                if not result["saved"]:
                    raise RuntimeError("Blueprint passed compilation but saving the test asset failed.")
                result["passed"] = True
                unreal.log("Training Blueprint verified: " + case["name"])
            except Exception as error:
                result["errors"].append(str(error))
                # Continue to the other graph: one failure must not hide the second result.
                unreal.log_warning("Training Blueprint verification failed: " + case["name"] + ": " + str(error))

        report["passed"] = len(report["cases"]) == len(cases) and all(case["passed"] for case in report["cases"])
    except Exception as error:
        report["errors"].append(str(error))
    finally:
        report["finishedAt"] = utc_now()
        report["status"] = "passed" if report["passed"] else "failed"
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    print("Training Blueprint verification report: " + str(report_path))
    if not report["passed"]:
        # Unreal's Python commandlet returns nonzero for an uncaught script exception.
        raise RuntimeError("Training Blueprint round-trip failed; inspect the saved report.")


if __name__ == "__main__":
    main()
