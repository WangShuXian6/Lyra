"""Run from the Training editor Output Log with two client PIE worlds and a dedicated server.
After starting this script, select Client 1's preview window within three seconds.
Native Enhanced Input drives gameplay; snapshots are read from all three actual worlds.
"""
import json
import math
import time
from datetime import datetime, timezone
from pathlib import Path
import unreal

class TrainingNetworkCheck:
    def __init__(self):
        self.start = time.monotonic()
        self.stage = 'grace'
        self.at = self.start
        self.handle = None
        self.done = False
        self.forced = set()
        self.report = {'startedAt': datetime.now(timezone.utc).isoformat(), 'passed': False,
            'method': 'Two native client PIE worlds with a dedicated server, Enhanced Input forced keys and replicated state sampling',
            'checks': {}, 'errors': [], 'scope': 'Local PIE networking; separate packaged executables are checked independently.'}
    def pos(self, actor):
        p = actor.get_actor_location()
        return [p.x,p.y,p.z]
    def pawn(self, world, pid):
        return next((a for a in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.Character)
            if a.get_editor_property('PlayerState') and a.get_editor_property('PlayerState').get_editor_property('PlayerId') == pid),None)
    def sample(self):
        out = []
        for world in self.worlds:
            actor = self.pawn(world,self.pid)
            if not actor or actor != self.original[world.get_path_name()]:
                raise RuntimeError('Player pawn changed during network measurement')
            out.append({'world':world.get_path_name(),'position':self.pos(actor)})
        return out
    def key(self,key,on):
        unreal.SystemLibrary.execute_console_command(self.client, 'Input.'+('+key ' if on else '-key ')+key+(' 1' if on else ''),self.pc)
        if on:self.forced.add(key)
        else:self.forced.discard(key)
    def begin(self):
        try:
            project=unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()).replace('\\','/').rstrip('/')
            if not project.lower().endswith('/lyradoclabs/lyratraining'):raise RuntimeError('Wrong project')
            self.worlds=list(unreal.EditorLevelLibrary.get_pie_worlds(True))
            if len(self.worlds)!=3 or any(not w.get_path_name().startswith('/TrainingRange/Maps/UEDPIE_') for w in self.worlds):raise RuntimeError('Expected three Training PIE worlds')
            clients=[];servers=[]
            for world in self.worlds:
                pcs=list(unreal.GameplayStatics.get_all_actors_of_class(world,unreal.PlayerController))
                local=[pc for pc in pcs if pc.is_local_controller()]
                if local:clients.append((world,local[0]))
                else:servers.append(world)
            if len(clients)!=2 or len(servers)!=1:raise RuntimeError('Expected two local client controllers and one dedicated world')
            clients.sort(key=lambda x:x[0].get_path_name())
            self.client,self.pc=clients[0]
            self.pid=self.pc.get_editor_property('PlayerState').get_editor_property('PlayerId')
            peer=clients[1][1].get_editor_property('PlayerState').get_editor_property('PlayerId')
            self.report.update(project=project,engine=unreal.SystemLibrary.get_engine_version(),playerIds=[self.pid,peer],
                viewportSizes=[list(pc.get_viewport_size()) for _,pc in clients])
            self.report['checks']['two_clients_and_server'] = self.pid != peer and all(self.pawn(w,self.pid) and self.pawn(w,peer) for w in self.worlds)
            self.report['checks']['resolution_1920x1080'] = all(pc.get_viewport_size()==(1920,1080) for _,pc in clients)
            self.original={w.get_path_name():self.pawn(w,self.pid) for w in self.worlds}
            self.report['before']=self.sample()
            self.dash_seen=False
            self.quickbar=self.pc.get_component_by_class(unreal.load_class(None,'/Script/LyraGame.LyraQuickBarComponent'))
            self.item=self.quickbar.get_active_slot_item()
            self.tag=unreal.GameplayTag();self.tag.import_text('(TagName="Lyra.ShooterGame.Weapon.MagazineAmmo")')
            self.ammo=int(self.item.get_stat_tag_stack_count(self.tag))
            self.handle=unreal.register_slate_post_tick_callback(self.tick)
            unreal.log('Training network check: select Client 1 now; input begins after three seconds.')
        except Exception as exc:self.finish(str(exc))
    def next(self,stage,now):self.stage=stage;self.at=now
    def tick(self,delta):
        if self.done:return
        try:
            now=time.monotonic();elapsed=now-self.at
            if now-self.start>20:raise RuntimeError('Network check timed out')
            if self.stage=='grace' and elapsed>3:
                self.key('W',True);self.next('move',now)
            elif self.stage=='move' and elapsed>.6:
                self.key('LeftShift',True);self.next('dash',now)
            elif self.stage=='dash':
                asc=unreal.AbilitySystemLibrary.get_ability_system_component(self.original[self.client.get_path_name()])
                for handle in asc.get_all_abilities():
                    result=unreal.AbilitySystemLibrary.get_gameplay_ability_from_spec_handle(asc,handle)
                    ability=result[0] if isinstance(result,(tuple,list)) else result
                    if ability and ability.get_class().get_path_name()=='/TrainingRange/Game/GA_TrainingDash.GA_TrainingDash_C':
                        self.dash_seen |= unreal.AbilitySystemLibrary.is_gameplay_ability_active(ability)
                if elapsed>.4:
                    self.key('LeftShift',False);self.key('W',False);self.next('settle',now)
            elif self.stage=='settle' and elapsed>1.0:
                self.key('LeftMouseButton',True);self.next('fire',now)
            elif self.stage=='fire' and elapsed>.3:
                self.key('LeftMouseButton',False);self.next('replicate',now)
            elif self.stage=='replicate' and elapsed>1:
                self.report['after']=self.sample()
                distances=[math.dist(b['position'][:2],a['position'][:2]) for b,a in zip(self.report['before'],self.report['after'])]
                positions=[r['position'] for r in self.report['after']]
                self.report['movementDistancesCm']=distances
                self.report['maximumPositionDifferenceCm']=max(math.dist(a,b) for a in positions for b in positions)
                self.report['checks']['movement_replicated_to_all_worlds']=all(d>30 for d in distances) and self.report['maximumPositionDifferenceCm']<20
                self.report['checks']['training_dash_activated']=self.dash_seen
                after=int(self.item.get_stat_tag_stack_count(self.tag))
                self.report['ammo']={'before':self.ammo,'after':after}
                self.report['checks']['client_fire_authorized_ammo_consumed']=after<self.ammo
                self.finish()
        except Exception as exc:self.finish(str(exc))
    def finish(self,error=None):
        if self.done:return
        self.done=True
        if error:self.report['errors'].append(error)
        for key in list(self.forced):
            try:self.key(key,False)
            except Exception:pass
        if self.handle:unreal.unregister_slate_post_tick_callback(self.handle)
        self.report['passed']=not self.report['errors'] and len(self.report['checks'])==5 and all(self.report['checks'].values())
        self.report['finishedAt']=datetime.now(timezone.utc).isoformat()
        dest=Path('F:/UE/LyraDoc/verification/training-network.json');dest.parent.mkdir(parents=True,exist_ok=True)
        dest.write_text(json.dumps(self.report,ensure_ascii=False,indent=2),encoding='utf-8')
        unreal.log('Training network check '+('passed' if self.report['passed'] else 'failed')+'; '+str(dest))

unreal._training_network_check=TrainingNetworkCheck()
unreal._training_network_check.begin()
