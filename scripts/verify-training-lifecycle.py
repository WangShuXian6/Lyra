"""In single-player Training PIE: fire/reload, walk a nav path through a pickup, then respawn.

Uses real input for fire/reload, AddMovementInput for bounded navigation, and Lyra's
native DamageSelfDestruct cheat for the death trigger. Never sets ammo, inventory,
health, actor transforms, or ability grants directly. Keep the PIE window focused.
"""
from datetime import datetime, timezone
import json
import math
import re
from pathlib import Path
import time
import unreal


class TrainingLifecycleVerification:
    def __init__(self):
        self.start = self.at = time.monotonic()
        self.stage = "grace"
        self.handle = None
        self.done = False
        self.forced = set()
        self.report = {"schemaVersion": 1, "startedAt": datetime.now(timezone.utc).isoformat(),
                       "passed": False, "status": "running", "checks": {}, "errors": [],
                       "method": "Native fire/reload input, navigation-guided AddMovementInput, Lyra DamageSelfDestruct",
                       "notCovered": ["packaged runtime", "network replication", "pickup input bindings"]}

    def tag(self, name):
        result = unreal.GameplayTag()
        if not result.import_text('(TagName="' + name + '")'):
            raise RuntimeError("GameplayTag is unavailable: " + name)
        return result

    def key(self, name, pressed):
        unreal.SystemLibrary.execute_console_command(self.world, "Input." + ("+key " if pressed else "-key ") + name + (" 1" if pressed else ""), self.pc)
        if pressed:
            self.forced.add(name)
        else:
            self.forced.discard(name)

    def next(self, stage):
        self.stage, self.at = stage, time.monotonic()

    def stats(self):
        return {"magazine": self.item.get_stat_tag_stack_count(self.magazine),
                "spare": self.item.get_stat_tag_stack_count(self.spare)}

    def slots(self):
        # ItemDef has no public UFUNCTION getter and Python rejects its protected property.
        # FindFragmentByClass returns the instanced fragment owned by that definition's CDO.
        definitions = []
        for item in self.quickbar.get_slots():
            fragment = item.find_fragment_by_class(self.equippable_fragment_class) if item else None
            if item and not fragment:
                raise RuntimeError("Expected an equippable weapon fragment in every occupied test slot.")
            definitions.append(fragment.get_outer().get_class().get_path_name() if fragment else None)
        return definitions

    def begin(self):
        try:
            project = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()).replace("\\", "/").rstrip("/")
            if not project.lower().endswith("/lyradoclabs/lyratraining"):
                raise RuntimeError("Use only LyraDocLabs/LyraTraining.")
            worlds = list(unreal.EditorLevelLibrary.get_pie_worlds(True))
            if len(worlds) != 1 or not re.search(r"/TrainingRange/Maps/UEDPIE_\d+_L_TrainingRange\.", worlds[0].get_path_name()):
                raise RuntimeError("Start one standalone PIE world on L_TrainingRange.")
            self.world = worlds[0]
            self.pc = unreal.GameplayStatics.get_player_controller(self.world, 0)
            if not self.pc:
                raise RuntimeError("PIE player controller has not initialized.")
            self.pawn = self.pc.get_controlled_pawn()
            if not self.pawn or not self.pawn.has_authority():
                raise RuntimeError("A local authoritative Training pawn is required.")
            self.state = self.pc.get_editor_property("PlayerState")
            self.asc = unreal.AbilitySystemLibrary.get_ability_system_component(self.pawn)
            self.quickbar = self.pc.get_component_by_class(unreal.load_class(None, "/Script/LyraGame.LyraQuickBarComponent"))
            if not self.state or not self.asc or not self.quickbar:
                raise RuntimeError("Wait for PlayerState, ASC and QuickBar initialization before starting the verifier.")
            self.item = self.quickbar.get_active_slot_item()
            if not self.item:
                raise RuntimeError("QuickBar has no active weapon item.")
            self.magazine = self.tag("Lyra.ShooterGame.Weapon.MagazineAmmo")
            self.spare = self.tag("Lyra.ShooterGame.Weapon.SpareAmmo")
            self.hud_class = unreal.load_class(None, "/Script/LyraGame.LyraHUDLayout")
            self.equippable_fragment_class = unreal.load_class(None, "/Script/LyraGame.InventoryFragment_EquippableItem")
            self.report.update(project=project, world=self.world.get_path_name(), engine=unreal.SystemLibrary.get_engine_version(),
                               oldPawn=self.pawn.get_path_name(), playerState=self.state.get_path_name(),
                               abilitySystem=self.asc.get_path_name(), ammoBeforeFire=self.stats())
            if self.report["ammoBeforeFire"]["magazine"] < 1 or self.report["ammoBeforeFire"]["spare"] < 1:
                raise RuntimeError("Start with a loaded weapon and spare ammo; this verifier does not manufacture ammo.")
            # Count the focus grace after synchronous identity/precondition checks have finished.
            self.at = time.monotonic()
            self.handle = unreal.register_slate_post_tick_callback(self.tick)
            unreal.log("Training lifecycle verification: focus the PIE window now. Maximum 70 seconds.")
        except Exception as error:
            self.finish(str(error))

    def choose_pickup(self):
        old_slots = self.slots()
        candidates = []
        navigation = next((obj for obj in unreal.ObjectIterator(unreal.NavigationSystemV1)
                           if obj.get_outer() == self.world), None)
        if not navigation:
            raise RuntimeError("The Training PIE world has no navigation system.")
        spawner_class = unreal.load_class(None, "/Script/LyraGame.LyraWeaponSpawner")
        start = self.pawn.get_actor_location()
        extent = unreal.Vector(150, 150, 500)
        projected_start = navigation.call_method("K2_ProjectPointToNavigation", (self.world, start, None, None, extent))
        if not projected_start:
            raise RuntimeError("Player position does not project onto navigation. Build Paths and save the training map before PIE.")
        for actor in unreal.GameplayStatics.get_all_actors_of_class(self.world, spawner_class):
            if not actor.get_editor_property("bIsWeaponAvailable"):
                continue
            definition = actor.get_editor_property("WeaponDefinition")
            item_class = definition.get_editor_property("InventoryItemDefinition") if definition else None
            if not item_class or item_class.get_path_name() in old_slots:
                continue
            goal = actor.get_actor_location()
            projected_goal = navigation.call_method("K2_ProjectPointToNavigation", (self.world, goal, None, None, extent))
            if not projected_goal:
                continue
            # Dispatch through the actual world's navigation UObject. UE 5.8's editor
            # script instrumentation otherwise asks the static wrapper's CDO for GetWorld.
            path = navigation.call_method("FindPathToLocationSynchronously", (self.world, projected_start, projected_goal, self.pawn, None))
            if path and path.is_valid() and not path.is_partial():
                points = list(path.path_points)
                if points:
                    length = sum(math.dist((a.x, a.y), (b.x, b.y)) for a, b in zip(points, points[1:]))
                    candidates.append((length, actor, item_class, points))
        if not candidates:
            raise RuntimeError("No available different weapon has a complete navigation path.")
        _, self.spawner, item_class, self.points = min(candidates, key=lambda row: row[0])
        self.expected_item = item_class.get_path_name()
        self.point_index = 1 if len(self.points) > 1 else 0
        self.report["pickup"] = {"spawner": self.spawner.get_path_name(), "expectedItem": self.expected_item,
                                 "slotsBefore": old_slots, "path": [[p.x, p.y, p.z] for p in self.points]}

    def tick(self, delta):
        if self.done:
            return
        try:
            if self.world not in unreal.EditorLevelLibrary.get_pie_worlds(True):
                raise RuntimeError("PIE world ended or changed.")
            if self.stage != "respawn" and self.pc.get_controlled_pawn() != self.pawn:
                raise RuntimeError("Pawn changed before the controlled death test; start a fresh single-player PIE.")
            now = time.monotonic()
            if now - self.start > 70:
                raise RuntimeError("Verification timed out in " + self.stage)
            elapsed = now - self.at
            if self.stage == "grace" and elapsed > 3:
                self.key("LeftMouseButton", True)
                self.next("fire")
            elif self.stage == "fire" and elapsed > .3:
                self.key("LeftMouseButton", False)
                self.report["ammoAfterFire"] = self.stats()
                if self.report["ammoAfterFire"]["magazine"] >= self.report["ammoBeforeFire"]["magazine"]:
                    raise RuntimeError("Mouse fire did not consume ammo; keep the PIE preview focused.")
                self.key("R", True)
                self.next("reload_key")
            elif self.stage == "reload_key" and elapsed > .15:
                self.key("R", False)
                self.next("reload")
            elif self.stage == "reload":
                current = self.stats()
                before = self.report["ammoAfterFire"]
                if current["magazine"] > before["magazine"] and current["spare"] < before["spare"]:
                    self.report["ammoAfterReload"] = current
                    self.report["checks"]["reload_consumes_spare"] = True
                    self.choose_pickup()
                    self.next("walk_pickup")
                elif elapsed > 8:
                    raise RuntimeError("Reload did not move spare ammo into the magazine.")
            elif self.stage == "walk_pickup":
                if self.pc.get_controlled_pawn() != self.pawn:
                    raise RuntimeError("Pawn changed before the controlled death test.")
                if self.expected_item in self.slots():
                    unavailable = not self.spawner.get_editor_property("bIsWeaponAvailable")
                    self.report["pickup"].update(slotsAfter=self.slots(), spawnerAvailable=not unavailable)
                    self.report["checks"]["native_overlap_weapon_pickup"] = unavailable
                    if not unavailable:
                        raise RuntimeError("Inventory changed without the selected spawner entering cooldown.")
                    unreal.SystemLibrary.execute_console_command(self.world, "DamageSelfDestruct", self.pc)
                    self.next("respawn")
                    return
                location = self.pawn.get_actor_location()
                target = self.points[self.point_index]
                dx, dy = target.x - location.x, target.y - location.y
                distance = math.hypot(dx, dy)
                if distance < 40 and self.point_index < len(self.points) - 1:
                    self.point_index += 1
                elif distance > 3:
                    self.pawn.add_movement_input(unreal.Vector(dx / distance, dy / distance, 0), 1.0, False)
                if elapsed > 35:
                    raise RuntimeError("Navigation did not reach the weapon pickup.")
            elif self.stage == "respawn":
                new_pawn = self.pc.get_controlled_pawn()
                if new_pawn and new_pawn != self.pawn:
                    new_asc = unreal.AbilitySystemLibrary.get_ability_system_component(new_pawn)
                    ability_classes = []
                    dash_instance = None
                    if new_asc:
                        for spec in new_asc.get_all_abilities():
                            result = unreal.AbilitySystemLibrary.get_gameplay_ability_from_spec_handle(new_asc, spec)
                            ability = result[0] if isinstance(result, (tuple, list)) else result
                            if ability:
                                ability_classes.append(ability.get_class().get_path_name())
                                if ability.get_class().get_path_name() == "/TrainingRange/Game/GA_TrainingDash.GA_TrainingDash_C" and isinstance(result, (tuple, list)) and result[1]:
                                    dash_instance = ability
                    dash_count = ability_classes.count("/TrainingRange/Game/GA_TrainingDash.GA_TrainingDash_C")
                    layouts = [w.get_path_name() for w in unreal.WidgetLibrary.get_all_widgets_of_class(self.world, self.hud_class, False)
                               if w.is_visible() and w.is_activated()]
                    if new_asc and dash_instance and dash_count and layouts and elapsed > 3:
                        self.report["respawn"] = {"newPawn": new_pawn.get_path_name(), "samePlayerState": self.pc.get_editor_property("PlayerState") == self.state,
                            "sameASC": new_asc == self.asc,
                            "avatarMatches": dash_instance.get_avatar_actor_from_actor_info() == new_pawn and dash_instance.get_ability_system_component_from_actor_info() == new_asc,
                            "avatarEvidence": "Training Dash instance public GetAvatarActorFromActorInfo + GetAbilitySystemComponentFromActorInfo",
                            "dashCount": dash_count, "activeHUDLayouts": layouts}
                        row = self.report["respawn"]
                        self.report["checks"]["respawn_rebinds_same_playerstate_asc"] = row["samePlayerState"] and row["sameASC"] and row["avatarMatches"]
                        self.report["checks"]["respawn_unique_dash_and_hud"] = dash_count == 1 and len(layouts) == 1
                        self.finish()
                if not self.done and elapsed > 15:
                    raise RuntimeError("Respawn did not finish with a new Pawn, bound ASC, training Dash and active HUD within 15 seconds.")
        except Exception as error:
            self.finish(str(error))

    def finish(self, error=None):
        self.done = True
        if error:
            self.report["errors"].append(error)
        for key in list(self.forced):
            try:
                self.key(key, False)
            except Exception:
                pass
        if self.handle is not None:
            try:
                unreal.unregister_slate_post_tick_callback(self.handle)
            except Exception as cleanup_error:
                self.report["errors"].append("Slate callback cleanup: " + str(cleanup_error))
            self.handle = None
        self.report["passed"] = not self.report["errors"] and len(self.report["checks"]) == 4 and all(self.report["checks"].values())
        self.report["status"] = "passed" if self.report["passed"] else "failed"
        self.report["completedAt"] = datetime.now(timezone.utc).isoformat()
        self.report["elapsedSeconds"] = time.monotonic() - self.start
        path = Path("F:/UE/LyraDoc/verification/training-lifecycle.json")
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(self.report, ensure_ascii=False, indent=2), encoding="utf-8")
        unreal.log("Training lifecycle result: " + str(path) + " passed=" + str(self.report["passed"]))


previous_training_lifecycle_verifier = globals().get("training_lifecycle_verifier")
if previous_training_lifecycle_verifier and not previous_training_lifecycle_verifier.done:
    previous_training_lifecycle_verifier.finish("Stopped because another lifecycle verification was started.")
training_lifecycle_verifier = TrainingLifecycleVerification()
training_lifecycle_verifier.begin()
