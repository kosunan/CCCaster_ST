"""独立した既存ReplayPlaybackコピーで標準リプレイを実パッド経路から再生する。"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
from steam_runtime import module_base, preferred

ROOT = Path(__file__).resolve().parents[3]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', type=Path, help='試験専用の独立コピー。機器INIを試験用に置換する')
    parser.add_argument('--steam-input-slot', type=int, choices=(1,2,3,4), required=True)
    args = parser.parse_args()
    game = args.game.resolve()
    if not game.is_relative_to((ROOT/'test/runtime').resolve()) or not game.name.startswith('ReplayPlayback_'):
        parser.error('test/runtime/ReplayPlayback_* の試験専用コピーだけを指定する')
    running = json.loads(subprocess.check_output(['powershell','-NoProfile','-Command',
        "@(Get-CimInstance Win32_Process -Filter \"Name='MBAA.exe'\" | Select-Object -ExpandProperty ExecutablePath) | ConvertTo-Json -Compress"],
        text=True).strip() or '[]')
    if isinstance(running,str): running=[running]
    if any(p and Path(p).resolve()==game/'MBAA.exe' for p in running):
        parser.error('試験コピーのゲームが既に起動中。既存プロセスを保全して中止')
    caster = game/'cccaster'
    out = ROOT/'test/logs'/time.strftime('replay_native_%Y%m%d_%H%M%S')
    out.mkdir()
    import vgamepad as vg
    pad = vg.VDS4Gamepad()
    name = 'Controller (XBOX 360 For Windows)'
    guid = f'11FF28DE-28DE-000{args.steam_input_slot}-0000-504944564944'
    (caster/'cccaster_steam.ini').write_text(f'[Settings]\nP1Device={name}\nP1DeviceGuid={guid}\n',encoding='utf-8')
    (caster/f'{name}__{guid}.ini').write_text('[Mapping]\nA=B2\nB=B0\nStart=B5\nFN1=B6\nFN2=B7\n',encoding='utf-8')
    for f in ('CCCaster_Steam.exe','libcccaster_steam_hook.dll'):
        shutil.copy2(ROOT/'build/bin'/f,caster/f)
    log = caster/'cccaster_hook_log.txt'
    if log.exists(): log.rename(out/'previous_game.log')
    k = C.WinDLL('kernel32',use_last_error=True)
    k.OpenProcess.argtypes=[W.DWORD,W.BOOL,W.DWORD];k.OpenProcess.restype=W.HANDLE
    k.ReadProcessMemory.argtypes=[W.HANDLE,C.c_void_p,C.c_void_p,C.c_size_t,C.c_void_p]
    k.CloseHandle.argtypes=[W.HANDLE]
    proc = None; handle = None
    result = dict(game=str(game),samples=[],passed=False)
    try:
        env={k:v for k,v in os.environ.items() if not k.startswith('CCCASTER_')}
        env['CCCASTER_INPUT_DIAGNOSTIC']='1'
        stream=(out/'launcher.log').open('w',encoding='utf-8')
        proc=subprocess.Popen([str(caster/'CCCaster_Steam.exe'),'--replay'],cwd=caster,env=env,
                              stdout=stream,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
        print('Logs:',out,flush=True)
        time.sleep(3)
        pid=int(subprocess.check_output(['powershell','-NoProfile','-Command',
            f"Get-CimInstance Win32_Process -Filter \"Name='MBAA.exe'\" | Where-Object ParentProcessId -eq {proc.pid} | Select-Object -ExpandProperty ProcessId"],text=True).strip())
        base=module_base(pid); result['image_base']=hex(base)
        handle=k.OpenProcess(0x10,False,pid)
        def read(legacy,size=4):
            buf=C.create_string_buffer(size)
            if not k.ReadProcessMemory(handle,base+preferred(legacy)-0x400000,buf,size,None):
                raise C.WinError(C.get_last_error())
            return int.from_bytes(buf.raw,'little')
        deadline=time.monotonic()+35
        while time.monotonic()<deadline:
            if read(0x54eee8)==1: break
            if log.exists() and '[FastBoot] Replay reached' in log.read_text(errors='replace'):
                pad.press_button(vg.DS4_BUTTONS.DS4_BUTTON_SQUARE);pad.update();time.sleep(.15)
                pad.reset();pad.update()
            time.sleep(.4)
        else: raise RuntimeError('標準リプレイの戦闘再生に未到達')
        for i in range(16):
            result['samples'].append(dict(mode=read(0x54eee8),intro=read(0x55d20b,1),
                timer=read(0x562a40),p1x=read(0x555238),p2x=read(0x555d34),p1seq=read(0x555140)))
            time.sleep(1)
        frames=[s for s in result['samples'] if s['mode']==1 and s['intro']==0]
        result['passed']=len(frames)>5 and len({s['timer'] for s in frames})>5 and len({s['p1x'] for s in frames})>1
        if not result['passed']: result['error']='戦闘中の時計/キャラ移動を確認できない'
    except Exception as exc: result['error']=str(exc)
    finally:
        pad.reset();pad.update()
        if handle: k.CloseHandle(handle)
        if proc:
            subprocess.run(['powershell','-NoProfile','-Command',
                f"Get-CimInstance Win32_Process -Filter \"Name='MBAA.exe'\" | Where-Object ParentProcessId -eq {proc.pid} | ForEach-Object {{ Stop-Process -Id $_.ProcessId -Force }}"],check=True)
            try: proc.wait(timeout=4)
            except subprocess.TimeoutExpired: proc.terminate();proc.wait(timeout=4)
            stream.close()
        if log.exists(): shutil.copy2(log,out/'game.log')
        (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
        print(json.dumps(result,ensure_ascii=False),flush=True)
    return int(not result['passed'])

if __name__=='__main__': raise SystemExit(main())
