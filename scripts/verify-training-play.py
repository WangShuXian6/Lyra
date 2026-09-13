"""Bounded, nonblocking PIE input verification for the isolated LyraTraining lab.

After starting PIE on L_TrainingRange, run in Unreal's Output Log:
py exec(open(r'F:/UE/LyraDoc/scripts/verify-training-play.py', encoding='utf-8').read())

Immediately switch focus back to the PIE preview window and keep it focused.
The first 1.5 seconds send no input, allowing this switch. Leaving Output Log
in front can prevent mouse fire even when keyboard movement still works.

Results: F:/UE/LyraDoc/verification/training-play.json. No build, asset save,
teleport, direct ability activation, or gameplay variable mutation occurs here.
"""

from datetime import datetime, timezone
import json
import math
from pathlib import Path
import time

import unreal


def _training_utc():
    return datetime.now(timezone.utc).isoformat()


def _training_position(actor):
    value = actor.get_actor_location()
    return {"x": float(value.x), "y": float(value.y), "z": float(value.z)}


def _training_xy_distance(a, b):
    return math.hypot(b["x"] - a["x"], b["y"] - a["y"])


class TrainingPlayVerification:
    FOCUS_GRACE_SECONDS = 1.5
    KEYS = ("W", "LeftShift", "SpaceBar", "LeftMouseButton")
    CHECKS = ("training_pie_identity", "training_dash_granted", "weapon_initialized",
              "hud_initialized", "bots_initialized", "move_input", "jump_input",
              "dash_input_activation", "fire_ammo_consumed")

    def __init__(self, report_path):
        self.report_path = Path(report_path)
        self.started = time.monotonic()
        self.stage_started = self.started
        self.stage = "initialization"
        self.handle = None
        self.done = False
        self.world = self.pc = self.character = self.asc = self.quickbar = self.item = None
        self.dash_handles = []
        self.forced = set()
        self.events = []
        self.report = {
            "schemaVersion": 1, "startedAt": _training_utc(), "status": "running", "passed": False,
            "timeoutSeconds": 20, "focusGraceSeconds": self.FOCUS_GRACE_SECONDS,
            "method": "Slate post-tick sampling and Enhanced Input console forced keys",
            "checks": {name: {"status": "not_run", "passed": False} for name in self.CHECKS},
            "errors": [], "events": self.events,
            "scope": "Single local training PIE: movement, jump, granted Dash activation, ammo, and initialization only.",
            "notCovered": ["multiplayer replication", "packaged build", "damage correctness", "bot decision making", "UI navigation"],
        }

    def check(self, name, passed, **evidence):
        self.report["checks"][name] = {"status": "passed" if passed else "failed", "passed": bool(passed), **evidence}

    def valid_world(self):
        current = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        return current and current == self.world and current.get_path_name() == self.report["world"]

    def command(self, key, down):
        if not self.valid_world():
            raise RuntimeError("Training PIE world changed; refusing input in another world.")
        command = "Input." + ("+key " if down else "-key ") + key + (" 1" if down else "")
        unreal.SystemLibrary.execute_console_command(self.world, command, self.pc)
        if down:
            self.forced.add(key)
        else:
            self.forced.discard(key)
        self.events.append({"seconds": round(time.monotonic() - self.started, 3), "command": command})

    def ability(self, handle):
        value = unreal.AbilitySystemLibrary.get_gameplay_ability_from_spec_handle(self.asc, handle)
        # This native function has an object return plus the bIsInstance out parameter.
        return value[0] if isinstance(value, (tuple, list)) else value

    def begin(self):
        try:
            project = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()).replace("\\", "/").rstrip("/")
            self.report["project"] = project
            self.report["engine"] = unreal.SystemLibrary.get_engine_version()
            if not project.lower().endswith("/lyradoclabs/lyratraining"):
                raise RuntimeError("Run this script only in the isolated LyraDocLabs/LyraTraining editor.")
            self.world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
            if not self.world:
                raise RuntimeError("Start PIE on L_TrainingRange before running this script.")
            world_path = self.world.get_path_name()
            self.report["world"] = world_path
            # PIE packages are renamed UEDPIE_<instance>_<map>; an editor world is not enough.
            if not world_path.startswith("/TrainingRange/Maps/UEDPIE_") or "_L_TrainingRange." not in world_path:
                raise RuntimeError("The active PIE world is not the TrainingRange test map: " + world_path)
            self.check("training_pie_identity", True, world=world_path)
            self.magazine_tag = unreal.GameplayTag()
            if not self.magazine_tag.import_text('(TagName="Lyra.ShooterGame.Weapon.MagazineAmmo")'):
                raise RuntimeError("Unable to construct the registered magazine GameplayTag for read-only queries.")
            self.quickbar_class = unreal.load_class(None, "/Script/LyraGame.LyraQuickBarComponent")
            self.equipment_manager_class = unreal.load_class(None, "/Script/LyraGame.LyraEquipmentManagerComponent")
            self.equipment_class = unreal.load_class(None, "/Script/LyraGame.LyraEquipmentInstance")
            self.bot_class = unreal.load_class(None, "/Script/LyraGame.LyraPlayerBotController")
            self.hud_layout_class = unreal.load_class(None, "/Script/LyraGame.LyraHUDLayout")
            if not all((self.quickbar_class, self.equipment_manager_class, self.equipment_class, self.bot_class, self.hud_layout_class)):
                raise RuntimeError("Required Lyra reflected classes are not available in this editor.")
            self.handle = unreal.register_slate_post_tick_callback(self.tick)
            unreal.log("Training PIE verification started. Switch to the PIE preview now; inputs start after 1.5 seconds. Keep the preview focused. Maximum 20 seconds.")
        except Exception as error:
            self.finish(str(error))

    def initialize_player(self, now):
        self.pc = unreal.GameplayStatics.get_player_controller(self.world, 0)
        self.character = unreal.GameplayStatics.get_player_character(self.world, 0)
        if not self.pc or not self.character:
            return False
        self.asc = unreal.AbilitySystemLibrary.get_ability_system_component(self.character)
        self.quickbar = self.pc.get_component_by_class(self.quickbar_class)
        if not self.asc or not self.quickbar:
            return False
        self.item = self.quickbar.get_active_slot_item()
        equipment_manager = self.character.get_component_by_class(self.equipment_manager_class)
        if not self.item or not equipment_manager:
            return False
        equipment = list(equipment_manager.get_equipment_instances_of_type(self.equipment_class))
        if not equipment:
            return False

        abilities = []
        self.dash_handles = []
        for handle in self.asc.get_all_abilities():
            ability = self.ability(handle)
            if not ability:
                continue
            class_path = ability.get_class().get_path_name()
            abilities.append(class_path)
            if class_path == "/TrainingRange/Game/GA_TrainingDash.GA_TrainingDash_C":
                self.dash_handles.append(handle)
        # Experience abilities may arrive after Pawn spawn; allow only a bounded startup window.
        if not abilities:
            return False
        self.report["character"] = self.character.get_path_name()
        self.report["controller"] = self.pc.get_path_name()
        self.report["abilitySystem"] = self.asc.get_path_name()
        self.report["grantedAbilityClasses"] = abilities
        self.check("training_dash_granted", bool(self.dash_handles), matches=len(self.dash_handles),
                   expectedClass="/TrainingRange/Game/GA_TrainingDash.GA_TrainingDash_C")
        spawned = [actor.get_path_name() for instance in equipment for actor in instance.get_spawned_actors() if actor]
        self.check("weapon_initialized", bool(spawned), activeItem=self.item.get_path_name(),
                   equipment=[instance.get_path_name() for instance in equipment], spawnedActors=spawned,
                   magazineAmmo=int(self.item.get_stat_tag_stack_count(self.magazine_tag)))
        widgets = list(unreal.WidgetLibrary.get_all_widgets_of_class(self.world, self.hud_layout_class, False))
        visible_widgets = [widget.get_path_name() for widget in widgets if widget.is_visible()]
        hud_actor = self.pc.get_hud()
        self.check("hud_initialized", bool(hud_actor and visible_widgets),
                   hudActor=hud_actor.get_path_name() if hud_actor else None, visibleLayouts=visible_widgets)
        bots = list(unreal.GameplayStatics.get_all_actors_of_class(self.world, self.bot_class))
        controlled_bots = [bot for bot in bots if bot.get_controlled_pawn()]
        self.check("bots_initialized", bool(controlled_bots), controllerCount=len(bots),
                   possessedCount=len(controlled_bots), pawns=[bot.get_controlled_pawn().get_path_name() for bot in controlled_bots])
        self.report["positionBefore"] = _training_position(self.character)
        # Remove only this verifier's known forced-key slots, including a previous interrupted run.
        for key in self.KEYS:
            self.command(key, False)
        self.start_stage("move", now)
        self.command("W", True)
        return True

    def start_stage(self, name, now):
        self.stage = name
        self.stage_started = now
        self.stage_position = _training_position(self.character)
        self.events.append({"seconds": round(now - self.started, 3), "stage": name, "position": self.stage_position})

    def tick(self, _delta_seconds):
        if self.done:
            return
        try:
            now = time.monotonic()
            if now - self.started >= 20:
                raise RuntimeError("The 20-second PIE verification deadline expired.")
            if not self.valid_world():
                raise RuntimeError("PIE ended or the active game world changed before verification completed.")
            if self.stage == "initialization":
                if now - self.started < self.FOCUS_GRACE_SECONDS:
                    return
                if not self.initialize_player(now) and now - self.started > 5:
                    raise RuntimeError("Pawn, ASC, QuickBar and equipped weapon did not initialize within 5 seconds.")
                return
            current_character = unreal.GameplayStatics.get_player_character(self.world, 0)
            if current_character != self.character:
                raise RuntimeError("The tested Pawn changed or died; measurements cannot be combined across respawns.")
            position = _training_position(self.character)
            elapsed = now - self.stage_started

            if self.stage == "move" and elapsed >= 1.2:
                self.command("W", False)
                distance = _training_xy_distance(self.stage_position, position)
                self.check("move_input", distance > 30, before=self.stage_position, after=position, distanceXY=distance,
                           input="Input.+key W 1", seconds=elapsed)
                self.start_stage("settle_before_jump", now)
            elif self.stage == "settle_before_jump" and elapsed >= 0.35:
                self.start_stage("jump", now)
                self.jump_max_z = position["z"]
                self.jump_released = False
                self.command("SpaceBar", True)
            elif self.stage == "jump":
                self.jump_max_z = max(self.jump_max_z, position["z"])
                if elapsed >= 0.18 and not self.jump_released:
                    self.command("SpaceBar", False)
                    self.jump_released = True
                if elapsed >= 1.7:
                    rise = self.jump_max_z - self.stage_position["z"]
                    self.check("jump_input", rise > 30, before=self.stage_position, after=position,
                               maxZ=self.jump_max_z, rise=rise, input="Input.+key SpaceBar 1")
                    self.start_stage("dash", now)
                    self.dash_seen = False
                    self.dash_released = False
                    self.command("LeftShift", True)
            elif self.stage == "dash":
                for handle in self.dash_handles:
                    ability = self.ability(handle)
                    if ability and unreal.AbilitySystemLibrary.is_gameplay_ability_active(ability):
                        self.dash_seen = True
                if elapsed >= 0.2 and not self.dash_released:
                    self.command("LeftShift", False)
                    self.dash_released = True
                if elapsed >= 1.2:
                    self.check("dash_input_activation", self.dash_seen, grantedTrainingDash=bool(self.dash_handles),
                               activationObserved=self.dash_seen, before=self.stage_position, after=position,
                               distanceXY=_training_xy_distance(self.stage_position, position), input="Input.+key LeftShift 1",
                               criterion="The actual GA_TrainingDash instance was sampled active after input; movement alone is insufficient.")
                    self.start_stage("settle_before_fire", now)
            elif self.stage == "settle_before_fire" and elapsed >= 0.3:
                self.item = self.quickbar.get_active_slot_item()
                if not self.item:
                    raise RuntimeError("The active weapon item disappeared before the firing measurement.")
                self.ammo_before = int(self.item.get_stat_tag_stack_count(self.magazine_tag))
                self.ammo_min = self.ammo_before
                self.fire_item_path = self.item.get_path_name()
                self.start_stage("fire", now)
                self.command("LeftMouseButton", True)
            elif self.stage == "fire":
                if self.quickbar.get_active_slot_item() != self.item:
                    raise RuntimeError("The active weapon changed during the ammunition measurement.")
                ammo_now = int(self.item.get_stat_tag_stack_count(self.magazine_tag))
                self.ammo_min = min(self.ammo_min, ammo_now)
                if elapsed >= 0.9:
                    self.command("LeftMouseButton", False)
                    self.check("fire_ammo_consumed", self.ammo_before > 0 and self.ammo_min < self.ammo_before,
                               item=self.fire_item_path, before=self.ammo_before, after=ammo_now, minimum=self.ammo_min,
                               tag="Lyra.ShooterGame.Weapon.MagazineAmmo", input="Input.+key LeftMouseButton 1")
                    self.report["positionAfter"] = position
                    self.finish()
        except Exception as error:
            self.finish(str(error))

    def finish(self, error=None):
        if self.done:
            return
        self.done = True
        if error:
            self.report["errors"].append(error)
        try:
            # Release on the original, still-active training world only. If PIE already ended,
            # its local-player subsystem is gone; never send input commands into another world.
            if self.world and self.pc and self.valid_world():
                for key in self.KEYS:
                    try:
                        self.command(key, False)
                    except Exception as cleanup_error:
                        self.report["errors"].append("Input cleanup: " + str(cleanup_error))
            elif self.forced:
                self.report["cleanupNote"] = "Original PIE world ended/changed; its forced-input subsystem was not addressed through another world."
        finally:
            if self.handle is not None:
                unreal.unregister_slate_post_tick_callback(self.handle)
                self.handle = None
            for value in self.report["checks"].values():
                if value["status"] == "not_run":
                    value["reason"] = error or "No observation was recorded before completion."
            self.report["finishedAt"] = _training_utc()
            self.report["durationSeconds"] = round(time.monotonic() - self.started, 3)
            self.report["passed"] = not self.report["errors"] and all(value["passed"] for value in self.report["checks"].values())
            self.report["status"] = "passed" if self.report["passed"] else "failed"
            self.report_path.parent.mkdir(parents=True, exist_ok=True)
            self.report_path.write_text(json.dumps(self.report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
            unreal.log("Training PIE verification " + self.report["status"] + "; report: " + str(self.report_path))
            self.world = self.pc = self.character = self.asc = self.quickbar = self.item = None


def start_training_play_verification(report_path="F:/UE/LyraDoc/verification/training-play.json"):
    previous = getattr(unreal, "_lyra_training_play_verification", None)
    if previous is not None and not previous.done:
        previous.finish("Superseded by a new explicit verification run.")
    runner = TrainingPlayVerification(report_path)
    unreal._lyra_training_play_verification = runner
    runner.begin()
    return runner


start_training_play_verification()



