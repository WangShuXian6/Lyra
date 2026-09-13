"""Optional editor-only observation extension for UE's native ToolsetRegistry.

Load once from the MMORPG editor Output Log before focusing the game window:
  py import sys; sys.path.insert(0,'F:/UE/LyraDoc/scripts/mmorpg'); import runtime_ui_toolset

Then rediscover list_toolsets / describe_toolset and call observe_ui through the
native MCP endpoint. Observation does not activate the Output Log or steal the
game's keyboard focus. This is original teaching instrumentation, not an Epic
built-in toolset, and is never loaded by the game's packaged runtime.
"""
import json
from pathlib import Path
import runpy
import toolset_registry
import unreal


def _verify_project():
    project = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()).replace('\\', '/').rstrip('/')
    if not project.lower().endswith('/lyradoclabs/mmorpg'):
        raise RuntimeError('MMO runtime observation is restricted to the isolated MMORPG lab.')
    return project


_verify_project()


@unreal.uclass()
class MMORuntimeObservationToolset(unreal.ToolsetDefinition):
    """Original read-only UMG/animation observations for the isolated MMORPG tutorial."""

    @toolset_registry.tool_call
    @staticmethod
    def observe_ui(stage: str) -> str:
        """Save native state after Slate input without changing the focused window.

        Args:
            stage: Lowercase letters, digits and hyphens identifying this observation.

        Returns:
            Compact JSON summary. The complete observation is saved under verification.
        """
        _verify_project()
        script = Path(__file__).with_name('inspect-runtime-ui.py')
        result = runpy.run_path(str(script), init_globals={'MMO_UI_STAGE': stage})['MMO_UI_OBSERVATION']
        return json.dumps({'stage': stage, 'world': result['world'], 'culture': result['culture'],
            'viewport': result['viewport'], 'rootCount': result['rootCount'],
            'focusedWidgets': [item['name'] for item in result['widgets'] if item['focus']],
            'activePanels': [item['name'] for item in result['widgets'] if item.get('activated')],
            'pawn': result.get('pawn'), 'dead': result.get('dead'),
            'abilityCount': result.get('abilityCount'), 'animation': result.get('animation'),
            'viewModel': result.get('viewModel')}, ensure_ascii=False)


unreal.ToolsetRegistry.register_toolset_class(MMORuntimeObservationToolset)
unreal.log('Registered original MMO runtime observation toolset; rediscover its current MCP schema before calling.')
