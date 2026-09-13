"""Check recorded native input observations; this does not drive or simulate UE.

The observations were captured after native SlateInspector key/mouse events in
the real PIE client. Assertions use resulting UObjects, focus, layers and MVVM.
"""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FOLDER = ROOT / 'verification/mmorpg-ui-observations'


def read(stage):
    return json.loads((FOLDER / (stage + '.json')).read_text(encoding='utf-8'))


def active(data):
    return {w['name'].split('_C_')[0] for w in data['widgets'] if w.get('activated')}


def focused(data, name):
    return any(w['name'] == name and w.get('focus') for w in data['widgets'])


def vm(data):
    return data['viewModel']


def binding(data):
    return any(w['name'].startswith('WBP_PlayerHUD_C_') and w.get('manaBinding') is True
               for w in data['widgets'])


checks = []


def check(name, stages, predicate):
    rows = [read(stage) for stage in stages]
    passed = all(d['rootCount'] == 1 for d in rows) and bool(predicate(*rows))
    checks.append({'name': name, 'passed': passed, 'observations': [
        {'path': str((FOLDER / (stage + '.json')).relative_to(ROOT)).replace('\\', '/'),
         'sha256': hashlib.sha256((FOLDER / (stage + '.json')).read_bytes()).hexdigest(),
         'observedAt': row['observedAt'], 'viewport': row['viewport']}
        for stage, row in zip(stages, rows)]})


HUD = {'WBP_PlayerHUD'}
MENU = HUD | {'WBP_Settings'}
MODAL = MENU | {'WBP_Confirm'}
INVENTORY = HUD | {'WBP_Inventory'}
check('Keyboard P opens Settings and focuses Medium', ['keyboard-settings'],
      lambda d: active(d) == MENU and focused(d, 'MediumQualityButton'))
check('Native mouse opens confirmation with Cancel focused', ['mouse-open-modal'],
      lambda d: active(d) == MODAL and focused(d, 'CancelButton'))
check('Escape dismisses modal and restores invoking button', ['keyboard-back-restores-settings'],
      lambda d: active(d) == MENU and focused(d, 'LogoutButton'))
check('Escape dismisses settings and restores gameplay layer', ['keyboard-back-to-hud'], lambda d: active(d) == HUD)
check('Gamepad Menu opens Settings and changes input hints', ['gamepad-menu'],
      lambda d: active(d) == MENU and focused(d, 'MediumQualityButton') and 'Left stick' in vm(d)['InputHint'])
check('Gamepad D-pad navigates to language button', ['gamepad-navigation'],
      lambda d: active(d) == MENU and focused(d, 'ChineseButton'))
check('Gamepad B closes Settings', ['gamepad-back-to-hud'], lambda d: active(d) == HUD)
check('Gamepad confirm opens modal with Cancel focused', ['gamepad-confirm-opens-modal'],
      lambda d: active(d) == MODAL and focused(d, 'CancelButton'))
check('Gamepad B returns focus to Settings logout button', ['gamepad-back-restores-settings'],
      lambda d: active(d) == MENU and focused(d, 'LogoutButton'))
check('Gamepad View opens Inventory with potion focused', ['gamepad-inventory'],
      lambda d: active(d) == INVENTORY and focused(d, 'PotionButton'))
check('Gamepad potion use updates real inventory and Mana binding', ['gamepad-inventory', 'gamepad-potion-mvvm'],
      lambda a, b: vm(a)['Mana'] == 40 and vm(b)['Mana'] == 80
      and vm(a)['InventoryLabel'] == 'Mana potion × 1' and 'empty' in vm(b)['InventoryLabel']
      and binding(b) and active(b) == INVENTORY and focused(b, 'PotionButton'))
check('Keyboard I restores keyboard hints and opens Inventory', ['keyboard-inventory'],
      lambda d: active(d) == INVENTORY and focused(d, 'PotionButton') and 'WASD' in vm(d)['InputHint'])
check('Logout removes HUD and old pawn/abilities', ['gamepad-potion-mvvm', 'logout-clears-ui'],
      lambda a, b: active(b) == {'WBP_Login'} and not b.get('pawn') and b['abilityCount'] == 0
      and vm(a)['object'] != vm(b)['object'])
check('Selecting another actual character creates a fresh bound ViewModel', ['gamepad-potion-mvvm', 'changed-character-joined'],
      lambda a, b: a['playerName'] == '教学旅人·二' and b['playerName'] == '教学旅人·一'
      and a['xp'] == 25 and b['xp'] == 0 and vm(a)['Mana'] == 80 and vm(b)['Mana'] == 100
      and 'empty' in vm(b)['InventoryLabel'] and vm(a)['object'] != vm(b)['object']
      and active(b) == HUD and binding(b) and b['abilityCount'] == 2)
check('New character ability ActorInfo points to its current pawn/ASC', ['changed-character-joined'],
      lambda d: len(d['abilityActorInfo']) == 2 and all(
          row['avatar'] == d['pawn'] and row['asc'] == d['asc'] for row in d['abilityActorInfo']))
check('New character subsequently receives its own combat/MVVM updates', ['changed-character-joined', 'changed-character-after-showcase'],
      lambda a, b: vm(a)['object'] == vm(b)['object'] and b['playerName'] == '教学旅人·一'
      and b['xp'] == 25 and vm(b)['Mana'] == 40 and vm(b)['InventoryLabel'] == 'Mana potion × 1' and binding(b))
check('Natural second logout retains keyboard navigation on login', ['returned-login-keyboard-verified'],
      lambda d: active(d) == {'WBP_Login'} and focused(d, 'LoginButton')
      and not d.get('pawn') and d['abilityCount'] == 0)

actions_path = ROOT / 'verification/mmorpg-native-input-actions.jsonl'
actions = [json.loads(line) for line in actions_path.read_text(encoding='utf-8').splitlines()]
keys = {row.get('arguments', {}).get('key') for row in actions if row['tool'] == 'PressKey'}
required = {'Gamepad_Special_Right', 'Gamepad_Special_Left', 'Gamepad_FaceButton_Bottom',
            'Gamepad_FaceButton_Right', 'Gamepad_DPad_Down', 'Tab'}
assert required <= keys, required - keys
report = {'schemaVersion': 1, 'summarizedAt': datetime.now(timezone.utc).isoformat(),
          'engine': 'UE 5.8.1', 'editorPid': 37428, 'passed': all(c['passed'] for c in checks),
          'passedChecks': sum(c['passed'] for c in checks), 'totalChecks': len(checks),
          'method': 'Native SlateInspector key/mouse dispatch followed by read-only native UObject/UMG observations in the real MMORPG PIE client connected to a dedicated server.',
          'actions': {'path': str(actions_path.relative_to(ROOT)).replace('\\', '/'),
                      'sha256': hashlib.sha256(actions_path.read_bytes()).hexdigest(), 'count': len(actions)},
          'checks': checks,
          'limitations': [
              'Gamepad events were injected through native Slate; physical USB/Bluetooth controller hardware was not tested.',
              'Click returning true is dispatch evidence only. Checks above assert resulting focus/layers/game data.',
              'Credential fixture fields were filled through an ignored local script; no password contents are recorded. After Output Log intervention, one login required explicitly restoring input focus. It is not counted as natural focus restoration.',
              'The independent natural second logout/Tab path succeeded without Output Log or Type intervention. EditableTextBox inner Slate focus must be distinguished from HasAnyUserFocus on its outer UWidget.',
              'Cached geometry can be zero for tickless UMG; those sizes are not rendered screenshot dimensions.',
              'PIE localization is separate from the standalone bilingual showcase; these input observations do not certify language switching.',
              'The final natural-login observation is 1914x1042 after the old quality handler reapplied resolution. Quality/1920x1080 acceptance requires the later fixed-handler rendered showcase.',
              'This run precedes the button content-slot and animation Speed-axis fixes. It validates input/lifecycle at its recorded revision; final rendered and packaged regressions are separate.'
          ], 'toolSourceReview': 'verification/slate-input-tool-source-review.json'}
output = ROOT / 'verification/mmorpg-native-ui-input.json'
output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(f"Native recorded UI checks: {report['passedChecks']}/{len(checks)}; {output}")
assert report['passed'], [c['name'] for c in checks if not c['passed']]
