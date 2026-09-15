"""Fresh Editor startup entry: validate existing assets without rebuilding their graphs."""
import hashlib
import json
import os
from pathlib import Path
import runpy
from datetime import datetime, timezone
import unreal

import runpy
project = runpy.run_path(str(Path(__file__).with_name('lesson_project.py')))['require_lesson_project']()

started = datetime.now(timezone.utc)
evidence_dir = project / 'Saved/Evidence'
evidence_dir.mkdir(parents=True, exist_ok=True)
output = evidence_dir / 'mmorpg-existing-content-validation.json'
report = {'processId': os.getpid(), 'startedAt': started.isoformat(),
          'project': project.as_posix(), 'passed': False,
          'sourceGraphsReconstructed': False, 'steps': []}


def save_report():
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


save_report()
try:
    # The reflected Python name is EditorPythonScripting (UCLASS ScriptName metadata).
    # Without keep-alive, -ExecutePythonScript requests QUIT_EDITOR on the next tick.
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    unreal.AssetRegistryHelpers.get_asset_registry().wait_for_completion()
    for script_name, report_name in (
        ('validate-animation.py', 'mmorpg-animation-assets.json'),
        ('validate-health-cook-label.py', 'mmorpg-health-cook-label-scan.json'),
    ):
        step_started = datetime.now(timezone.utc)
        runpy.run_path(str(Path(__file__).with_name(script_name)), run_name='__main__')
        child_path = evidence_dir / report_name
        if child_path.stat().st_mtime < step_started.timestamp():
            raise RuntimeError('Validation produced no fresh report: ' + report_name)
        child_report = json.loads(child_path.read_text(encoding='utf-8-sig'))
        if not child_report.get('passed'):
            raise RuntimeError('Native validation did not pass: ' + report_name)
        child_report['validationProcessId'] = os.getpid()
        child_report['validationStartedAt'] = step_started.isoformat()
        child_report['validationCompletedAt'] = datetime.now(timezone.utc).isoformat()
        child_path.write_text(json.dumps(child_report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        report['steps'].append({'script': script_name, 'report': child_path.as_posix(),
                                'passed': True, 'sha256': hashlib.sha256(child_path.read_bytes()).hexdigest()})
        save_report()
    report['passed'] = True
except Exception as error:
    report['error'] = str(error)
    unreal.log_error('MMO existing-content validation failed: ' + str(error))
    raise
finally:
    report['completedAt'] = datetime.now(timezone.utc).isoformat()
    save_report()
unreal.log('MMO_EXISTING_CONTENT_VALIDATED processId=' + str(os.getpid()))
