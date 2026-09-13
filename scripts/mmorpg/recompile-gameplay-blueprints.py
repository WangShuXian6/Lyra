"""Refresh only the three gameplay Blueprint subclasses after native health changes.

Run through UnrealEditor-Cmd -run=pythonscript -script=<this file>. The native
bridge compiles and saves existing assets; it does not reconstruct graph nodes.
"""
import json
from pathlib import Path

import unreal

project = Path(unreal.Paths.get_project_file_path()).resolve()
if project.as_posix().lower() != "f:/ue/lyradoclabs/mmorpg/mmorpg.uproject":
    raise RuntimeError(f"Unexpected experiment project: {project}")

report = json.loads(unreal.MMODocExperienceTools.recompile_gameplay_blueprints())
output = project.parent / "Saved/Evidence/mmorpg-death-blueprints.json"
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
if not report.get("passed"):
    raise RuntimeError(f"Gameplay Blueprint compilation/default verification failed: {output}")
if len(report.get("assets", [])) != 3 or report.get("graphsReconstructed") is not False:
    raise RuntimeError("Unexpected compile scope; the three existing gameplay subclasses are required")
unreal.log(f"MMO_DEATH_BLUEPRINTS_PASSED {output}")
