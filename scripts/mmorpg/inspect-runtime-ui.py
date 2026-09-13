"""Read an actual MMORPG PIE client after a native Slate interaction.

Output Log: py import runpy; runpy.run_path('F:/UE/LyraDoc/scripts/mmorpg/inspect-runtime-ui.py',init_globals={'MMO_UI_STAGE':'english-settings'})
This observes widgets, focus, animation and replicated state. It does not press
buttons, change attributes, or mark an interaction test as passed. Editable text
contents (including passwords) are deliberately excluded from the evidence.
"""
import json
import re
from datetime import datetime, timezone
from pathlib import Path
import unreal


def mmo_ui_observe(stage):
    project = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()).replace('\\', '/').rstrip('/')
    if not project.lower().endswith('/lyradoclabs/mmorpg'):
        raise RuntimeError('Use the isolated MMORPG editor, never the official Lyra baseline.')
    if not re.fullmatch(r'[a-z0-9-]{1,64}', stage):
        raise ValueError('Stage must contain only lowercase letters, digits and hyphens.')
    pairs = []
    for world in unreal.EditorLevelLibrary.get_pie_worlds(True):
        for pc in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.PlayerController):
            if pc.is_local_controller():
                pairs.append((world, pc))
    if len(pairs) != 1:
        raise RuntimeError('Expected exactly one local MMORPG PIE client for this focus observation.')
    world, pc = pairs[0]
    out = {'stage': stage, 'observedAt': datetime.now(timezone.utc).isoformat(),
           'project': project, 'engine': unreal.SystemLibrary.get_engine_version(),
           'world': world.get_path_name(), 'viewport': list(pc.get_viewport_size()),
           'culture': unreal.InternationalizationLibrary.get_current_culture(),
           'method': 'Read-only native UObject/UMG observation following separately recorded Slate input.',
           'widgets': []}
    pawn = pc.get_controlled_pawn()
    state = pc.get_editor_property('PlayerState')
    if state:
        out['playerState'] = state.get_path_name()
        out['playerName'] = state.get_player_name()
        out['xp'] = state.get_editor_property('XP')
        out['level'] = state.get_editor_property('Level')
        out['sessionReady'] = state.get_editor_property('bSessionReady')
        asc = state.get_editor_property('ASC')
        out['asc'] = asc.get_path_name()
        abilities = list(asc.get_all_abilities())
        out['abilityCount'] = len(abilities)
        out['abilityActorInfo'] = []
        for handle in abilities:
            ability, is_instance = unreal.AbilitySystemLibrary.get_gameplay_ability_from_spec_handle(asc, handle)
            if ability and is_instance:
                avatar = ability.get_avatar_actor_from_actor_info()
                ability_asc = ability.get_ability_system_component_from_actor_info()
                out['abilityActorInfo'].append({'ability': ability.get_class().get_path_name(),
                    'avatar': avatar.get_path_name() if avatar else None,
                    'asc': ability_asc.get_path_name() if ability_asc else None})
    if pawn:
        pos = pawn.get_actor_location()
        out['pawn'] = pawn.get_path_name()
        out['position'] = [pos.x, pos.y, pos.z]
        health = pawn.get_component_by_class(unreal.load_class(None, '/Script/MMORPG.MMOHealthComponent'))
        out['dead'] = health.is_dead() if health else None
        mesh = pawn.get_editor_property('Mesh')
        skeletal_mesh = mesh.get_skinned_asset()
        anim = mesh.get_anim_instance()
        out['mesh'] = skeletal_mesh.get_path_name() if skeletal_mesh else None
        out['animation'] = None
        if anim:
            layer_class = unreal.load_class(None, '/Game/Characters/MMO/ABP_MMOUnarmed.ABP_MMOUnarmed_C')
            linked = anim.get_linked_anim_layer_instance_by_class(layer_class, False)
            out['animation'] = {'class': anim.get_class().get_path_name(),
                'groundSpeed': anim.get_editor_property('GroundSpeed'),
                'falling': anim.get_editor_property('bIsFalling'),
                'linkedLayer': linked.get_class().get_path_name() if linked else None,
                'linkedGroundSpeed': linked.get_editor_property('GroundSpeed') if linked else None}
    ui_class = unreal.load_class(None, '/Script/MMORPG.MMORootLayout')
    roots = unreal.WidgetLibrary.get_all_widgets_of_class(world, ui_class, False)
    out['rootCount'] = len(roots)
    out['layers'] = []
    for root in roots:
        for name in ('GameLayer', 'MenuLayer', 'ModalLayer'):
            layer = root.get_editor_property(name)
            active = layer.get_active_widget()
            out['layers'].append({'name': name, 'duration': layer.get_transition_duration(),
                                  'active': active.get_name() if active else None})
        vm = root.get_editor_property('ViewModel')
        if vm:
            out['viewModel'] = {'object': vm.get_path_name(),
                **{key: str(vm.get_editor_property(key)) for key in ('StatusText', 'ManaLabel', 'InventoryLabel', 'TargetLabel', 'CooldownLabel', 'InputHint')},
                **{key: float(vm.get_editor_property(key)) for key in ('Health', 'Mana', 'Cooldown', 'ManaFraction', 'TargetFraction')}}
    widgets = unreal.WidgetLibrary.get_all_widgets_of_class(world, unreal.UserWidget, False)
    widget_paths = {widget.get_path_name() for widget in widgets}
    tree_paths = {tree.get_path_name() for tree in unreal.ObjectIterator(unreal.WidgetTree)
                  if tree.get_outer() and tree.get_outer().get_path_name() in widget_paths}
    children = [widget for widget in unreal.ObjectIterator(unreal.Widget)
                if widget.get_outer() and widget.get_outer().get_path_name() in tree_paths]
    seen = set()

    def visit(widget):
        if not widget or widget.get_path_name() in seen:
            return
        seen.add(widget.get_path_name())
        item = {'name': widget.get_name(), 'class': widget.get_class().get_path_name(),
                'object': widget.get_path_name(), 'visibility': str(widget.get_visibility()),
                'focus': widget.has_any_user_focus(), 'focusedDescendants': widget.has_focused_descendants(),
                'enabled': widget.get_is_enabled()}
        size = unreal.SlateLibrary.get_local_size(widget.get_cached_geometry())
        desired = widget.get_desired_size()
        item['size'] = [size.x, size.y]
        item['desiredSize'] = [desired.x, desired.y]
        if isinstance(widget, unreal.Button):
            item['clickDelegateBound'] = widget.get_editor_property('OnClicked').is_bound()
        if isinstance(widget, unreal.TextBlock):
            item['text'] = str(widget.get_text())
        if isinstance(widget, unreal.ProgressBar):
            item['percent'] = widget.get_editor_property('Percent')
        if isinstance(widget, unreal.CommonActivatableWidget):
            item['activated'] = widget.is_activated()
            desired_focus = widget.get_desired_focus_target()
            item['desiredFocus'] = desired_focus.get_name() if desired_focus else None
        if isinstance(widget, unreal.UserWidget):
            owner = widget.get_owning_player()
            item['owningPlayer'] = owner.get_path_name() if owner else None
            item['manaBinding'] = widget.validate_mana_binding() if hasattr(widget, 'validate_mana_binding') else None
        out['widgets'].append(item)
        if isinstance(widget, unreal.PanelWidget):
            for index in range(widget.get_children_count()):
                visit(widget.get_child_at(index))

    for widget in list(widgets) + children:
        visit(widget)
    folder = Path(__file__).resolve().parents[2] / 'verification' / 'mmorpg-ui-observations'
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / (stage + '.json')
    path.write_text(json.dumps(out, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    unreal.log('MMO UI observation saved: ' + str(path))
    return out


MMO_UI_OBSERVATION = mmo_ui_observe(globals().get('MMO_UI_STAGE', 'initial'))
