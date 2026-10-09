"""実ゲームの場面別終了操作。仮想DS4とWin32メッセージを使い、ゲームメモリは読取りのみ。"""
import ctypes as C
from ctypes import wintypes as W
import json
import os
from pathlib import Path
import random
import re
import shutil
import subprocess
import sys
import threading
import time
import traceback
import psutil
from run_training_character import device_guid
from steam_runtime import module_base, preferred
from run_training_corner import digest
from bench_startup import protected_files
from real_game_checkpoint import clean_environment
from run_stage_rematch import free_match_port
from test_p2p_service import Service

ROOT = Path(__file__).resolve().parents[3]
RUNTIME = ROOT / 'test/runtime'
U = C.WinDLL('user32', use_last_error=True)
K = C.WinDLL('kernel32', use_last_error=True)
K.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
K.OpenProcess.restype = W.HANDLE
K.ReadProcessMemory.argtypes = [W.HANDLE, C.c_void_p, C.c_void_p, C.c_size_t, C.c_void_p]
K.CloseHandle.argtypes = [W.HANDLE]
K.WaitForSingleObject.argtypes = [W.HANDLE, W.DWORD]
K.WaitForSingleObject.restype = W.DWORD
K.GetExitCodeProcess.argtypes = [W.HANDLE, C.POINTER(W.DWORD)]
K.TerminateProcess.argtypes = [W.HANDLE, W.UINT]
U.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
U.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
U.IsWindowVisible.argtypes = [W.HWND]
U.SetForegroundWindow.argtypes = [W.HWND]
U.GetForegroundWindow.restype = W.HWND
ENUM = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
U.EnumWindows.argtypes = [ENUM, W.LPARAM]


def wait(predicate, label, seconds=30):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        value = predicate()
        if value:
            return value
        time.sleep(.03)
    raise RuntimeError('時間切れ: ' + label)


def process_live(pid):
    """保持済みの終了processも列挙されるWindowsで、signal状態を調べる。"""
    handle = K.OpenProcess(0x101000, False, pid)
    if not handle:
        error = C.get_last_error()
        if error == 87:  # PIDが存在しない。
            return False
        if error == 5:
            # psutilが終了済みobjectを返す場合でも、生きているアクセス拒否を無視しない。
            query=subprocess.run(['pwsh','-NoProfile','-Command',
                f"Get-CimInstance Win32_Process -Filter 'ProcessId={int(pid)}' | Select-Object -ExpandProperty ProcessId"],
                capture_output=True,text=True,timeout=10)
            if query.returncode==0 and not query.stdout.strip():
                return False
        raise C.WinError(error)
    try:
        state = K.WaitForSingleObject(handle, 0)
        if state == 0:
            return False
        if state != 258:
            raise C.WinError(C.get_last_error())
        return True
    finally:
        K.CloseHandle(handle)


def processes(path, owner=None, started=0):
    matches = []
    psutil.process_iter.cache_clear()
    for p in psutil.process_iter(['exe', 'create_time']):
        try:
            if not p.info['exe'] or Path(p.info['exe']).resolve() != path.resolve():
                continue
            if p.info['create_time'] < started or not process_live(p.pid):
                continue
            if owner is not None and owner.pid not in [parent.pid for parent in p.parents()]:
                continue
            matches.append(p)
        except psutil.NoSuchProcess:
            pass
        except psutil.AccessDenied:
            # exeが取得できない別プロセスは対象外。既知の対象の権限エラーは隠さない。
            if p.info.get('exe') and Path(p.info['exe']).resolve() == path.resolve():
                raise
    return matches


class Game:
    def __init__(self, side, owner, launcher_log=None, started=0):
        path = RUNTIME / f'MBAACC_{side}/MBAA.exe'
        def current():
            candidates = processes(path, owner, started)
            if launcher_log is not None:
                text = launcher_log.read_text(encoding='utf-8', errors='replace')
                match = re.search(r'LaunchSuspended OK \(PID=(\d+)\)', text)
                if not match:
                    return None
                candidates = [p for p in candidates if p.pid == int(match[1])]
            if len(candidates)>1:
                raise RuntimeError(f'対象が複数: {path}: {[p.pid for p in candidates]}')
            return candidates[0] if candidates else None
        self.process = wait(current, '今回のlauncherのゲーム起動')
        self.created = self.process.create_time()
        # 終了まで同一Windows process objectのhandleを保持する。PID再利用から独立する。
        self.handle = K.OpenProcess(0x101011, False, self.process.pid)
        if not self.handle:
            raise C.WinError(C.get_last_error())
        try:
            self.image_base = module_base(self.process.pid)
        except Exception:
            self.close_handle()
            raise
        def window():
            log=path.parent/'cccaster_st/cccaster_hook_log.txt'
            if not log.exists():
                return None
            text=log.read_text(encoding='utf-8',errors='replace')
            matches=re.findall(r'SetWindowLongPtr SUCCEEDED for HWND ([0-9a-fA-F]+)',text)
            if not matches:
                return None
            hwnd=int(matches[-1],16)
            pid=W.DWORD()
            U.GetWindowThreadProcessId(hwnd,C.byref(pid))
            return [hwnd] if pid.value==self.process.pid and U.IsWindowVisible(hwnd) else None
        try:
            self.hwnd = wait(window, 'ゲーム窓')[0]
        except Exception:
            self.close_handle()
            raise

    def read(self, address, size=4):
        address = self.image_base + preferred(address) - 0x400000
        data = C.create_string_buffer(size)
        if not K.ReadProcessMemory(self.handle, address, data, size, None):
            raise C.WinError(C.get_last_error())
        return int.from_bytes(data.raw, 'little')

    def post(self, message, w=0, l=0):
        if not U.PostMessageW(self.hwnd, message, w, l):
            raise C.WinError(C.get_last_error())

    def key(self, key):
        self.post(0x100, key)
        time.sleep(.08)
        # Escによる終了直後はキーアップの配送失敗を許容する。
        U.PostMessageW(self.hwnd, 0x101, key, 0xc0000000)

    def battle(self):
        return self.read(0x54eee8) == 1 and self.read(0x55d20b, 1) == 0

    def blocked(self):
        rows = []
        # 直接WM_CLOSEは入力待機中のPeekMessage経路も通り得る。
        for name, messages in (
            ('escape', [(0x100, 27, 0), (0x100, 27, 1 << 30), (0x102, 27, 0), (0x101, 27, 0xc0000000)]),
            ('sys_escape', [(0x104, 27, 0), (0x105, 27, 0xc0000000)]),
            ('caption_close', [(0xa1, 20, 0), (0xa2, 20, 0), (0xa3, 20, 0)]),
            ('system_close', [(0x112, 0xf063, 0)]),
            ('queued_close', [(0x10, 0, 0)] * 8)):
            first = self.read(0x55d1d4)
            for message in messages:
                self.post(*message)
            wait(lambda: self.read(0x55d1d4) >= first + 20, name + '後の進行', 3)
            assert self.battle(), name + 'で戦闘を離れた'
            rows.append(dict(operation=name, advanced=self.read(0x55d1d4)-first))
        # OSのキー状態を更新して、WndProc以外のGetAsyncKeyState監視も検査する。
        U.SetForegroundWindow(self.hwnd)
        wait(lambda: U.GetForegroundWindow() == self.hwnd, '前面化', 3)
        first = self.read(0x55d1d4)
        U.keybd_event(27, 1, 0, 0)
        try:
            time.sleep(.25)
        finally:
            U.keybd_event(27, 1, 2, 0)
        wait(lambda: self.read(0x55d1d4) >= first + 20, 'OS Esc後の進行', 3)
        assert self.battle()
        rows.append(dict(operation='async_escape', advanced=self.read(0x55d1d4)-first))
        return rows

    def wait_exit(self, seconds=10):
        state = K.WaitForSingleObject(self.handle, int(seconds*1000))
        if state == 258:
            raise RuntimeError(f'ゲーム終了待機時間切れ PID={self.process.pid}')
        if state != 0:
            raise C.WinError(C.get_last_error())
        code = W.DWORD()
        if not K.GetExitCodeProcess(self.handle, C.byref(code)):
            raise C.WinError(C.get_last_error())
        return code.value

    def terminate(self):
        if K.WaitForSingleObject(self.handle, 0) == 0:
            return
        if not K.TerminateProcess(self.handle, 1):
            error = C.get_last_error()
            # 自然終了と競合した場合だけ、同じhandleのsignalを確認して許容。
            if K.WaitForSingleObject(self.handle, 0) != 0:
                raise C.WinError(error)
        self.wait_exit()

    def close_handle(self):
        if self.handle:
            K.CloseHandle(self.handle)
            self.handle = None


def main():
    import vgamepad as vg
    from vgamepad.win import vigem_client as vc
    out = ROOT / 'test/logs' / time.strftime('exit_guard_%Y%m%d_%H%M%S')
    out.mkdir()
    print('Logs:', out, flush=True)
    sides = [RUNTIME / f'MBAACC_{n}' for n in (1, 2, 3)]
    before = protected_files(sides)
    (out/'protected_before.json').write_text(json.dumps(before,indent=2),encoding='utf-8')
    result = dict(checks={}, errors=[], binaries={}, processes=[], cleanup=[])
    backups, pads, streams, launchers, games = {}, [], [], [], []
    env = clean_environment()
    env['CCCASTER_STARTUP_TRACE'] = '1'
    service = Service()
    threading.Thread(target=service.serve_forever, daemon=True).start()
    env['CCCASTER_NTFY_SERVER'] = f'http://127.0.0.1:{service.server_port}'

    def launch(side, args, label):
        caster = sides[side-1] / 'cccaster_st'
        log = caster / 'cccaster_hook_log.txt'
        if log.exists():
            log.replace(out / (label + '_previous.log'))
        stream = (out / (label + '_launcher.log')).open('w', encoding='utf-8')
        streams.append(stream)
        proc = subprocess.Popen([str(caster/'CCCaster_Steam.exe'), *args], cwd=caster, env=env,
                                stdout=stream, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
        proc.exit_guard_started = time.time()-1
        proc.exit_guard_log = out / (label + '_launcher.log')
        launchers.append(proc)
        return proc

    def attach(side, proc):
        game = Game(side, proc, proc.exit_guard_log, proc.exit_guard_started)
        games.append(game)
        result['processes'].append(dict(side=side, launcher=proc.pid, game=game.process.pid, created=game.created, hwnd=hex(game.hwnd)))
        wait(lambda: game.read(0x54eee8) == 20, 'キャラ選択')
        log = sides[side-1] / 'cccaster_st/cccaster_hook_log.txt'
        wait(lambda: '[Startup] event=chara_present' in log.read_text(encoding='utf-8', errors='replace'), 'キャラ選択の初回描画')
        first = game.read(0x55d1d4)
        wait(lambda: game.read(0x55d1d4) >= first + 20, '通常速度でのキャラ選択進行', 5)
        return game

    def preserve_logs(label, side=1):
        shutil.copy2(sides[side-1]/'cccaster_st/cccaster_hook_log.txt', out/(label+'_game.log'))

    def press_a():
        for pad in pads:
            pad.press_button(vg.DS4_BUTTONS.DS4_BUTTON_SQUARE)
            pad.update()
        time.sleep(.1)
        for pad in pads:
            pad.reset()
            pad.update()
        time.sleep(.15)

    try:
        for side in sides:
            assert not processes(side/'MBAA.exe'), '対象ゲームが起動中'
            for name in ('CCCaster_Steam.exe', 'CCCaster_Steam_GUI.exe', 'libcccaster_steam_hook.dll'):
                path = side/'cccaster_st'/name
                result['binaries'][str(path)] = digest(path)
                assert digest(path) == digest(ROOT/'build/bin'/name)
        # 通常終了時にゲーム自身が保存する窓設定も含めて戻す。
        backups.update({Path(p):Path(p).read_bytes() for p in before if Path(p).suffix.lower()=='.ini'})
        guids = []
        for _ in range(2):
            product = random.SystemRandom().randrange(0x8000, 0xffff)
            class Pad(vg.VDS4Gamepad):
                def target_alloc(self):
                    target = vc.vigem_target_ds4_alloc()
                    vc.vigem_target_set_vid(target, 0x054c)
                    vc.vigem_target_set_pid(target, product)
                    return target
            pads.append(Pad())
            guids.append(wait(lambda: device_guid((product << 16) | 0x054c), '仮想パッド'))
        caster = sides[0]/'cccaster_st'
        paths = [caster/'cccaster_steam.ini', *[caster/f'Wireless Controller__{guid}.ini' for guid in guids]]
        for path in paths:
            if path not in backups:
                backups[path] = path.read_bytes() if path.exists() else None
        # 後処理の予期しない失敗からも復旧できるよう、変更前のバイト列を保存する。
        (out/'config_backups.json').write_text(json.dumps({str(p):b.hex() if b is not None else None
                                                        for p,b in backups.items()},indent=2),encoding='utf-8')
        paths[0].write_text('[Settings]\n'+''.join(f'P{n}Device=Wireless Controller\nP{n}DeviceGuid={guid}\n' for n,guid in enumerate(guids,1)), encoding='utf-8')
        for path in paths[1:]:
            path.write_text('[Mapping]\nUp=H0_8\nDown=H0_2\nLeft=H0_4\nRight=H0_6\nA=B0\nB=B1\nC=B2\nD=B3\nE=B4\nStart=B7\nFN1=B8\nFN2=B9\n', encoding='utf-8')
        for mode, battle, operation in [('training',False,'escape'), ('offline',False,'close'),
                                        ('offline',False,'escape'), ('training',True,'escape'),
                                        ('training',True,'close'), ('offline',True,'guard')]:
            label = f'{mode}_{"battle" if battle else "selection"}_{operation}'
            print(label, flush=True)
            proc = launch(1, ['--'+mode], label)
            game = attach(1, proc)
            if battle:
                end = time.monotonic()+40
                while not game.battle():
                    if time.monotonic() > end:
                        raise RuntimeError('戦闘に未到達')
                    press_a()
            if operation == 'guard':
                result['checks'][label] = game.blocked()
                # 試験終了だけは明示的なプロセス終了。抑止判定とは区別する。
                game.terminate()
            elif operation == 'escape':
                game.key(27)
            else:
                game.post(0x112, 0xf060)
            try:
                exit_code=game.wait_exit(5)
            except Exception:
                result['operation_failure']=dict(label=label,pid=game.process.pid,hwnd=hex(game.hwnd),
                    mode=game.read(0x54eee8),frame=game.read(0x55d1d4))
                preserve_logs(label)
                raise
            result['processes'][-1]['exit_code']=exit_code
            result['processes'][-1]['harness_terminated']=operation == 'guard'
            proc.wait(timeout=10)
            preserve_logs(label)
            assert exit_code in (0, 1), f'{label}: 異常終了 code=0x{exit_code:08X}'
            if operation != 'guard':
                launcher_log = (out/(label+'_launcher.log')).read_text(encoding='utf-8', errors='replace')
                reason = 2 if operation == 'escape' else 1
                assert re.search(rf'\[SESSION_RESULT\] code=user_exit stage=running exit={exit_code} reason={reason}\b', launcher_log), f'{label}: ユーザー終了理由が不一致'
            result['checks'].setdefault(label, True)
        # ネット対戦のキャラ選択ではEscを相手へ通知して両ゲームを終了する。
        port = free_match_port()
        host = launch(1, ['--headless','--host','--port',str(port)], 'net_selection_host')
        def code():
            text = (out/'net_selection_host_launcher.log').read_text(encoding='utf-8', errors='replace')
            match = re.search(r'\[HEADLESS HOST\] Hash: ([A-Za-z0-9]+)', text)
            return match[1] if match else None
        connection = wait(code, '接続コード')
        peer = launch(2, ['--headless','--hash',connection,'--port',str(port+1)], 'net_selection_peer')
        host_game, peer_game = attach(1, host), attach(2, peer)
        host_game.key(27)
        host_exit = host_game.wait_exit(5)
        peer_exit = peer_game.wait_exit(5)
        host.wait(timeout=10)
        peer.wait(timeout=10)
        preserve_logs('net_selection_host',1)
        preserve_logs('net_selection_peer',2)
        assert host_exit in (0, 1) and peer_exit in (0, 1), f'ネットキャラ選択の異常終了 host=0x{host_exit:08X} peer=0x{peer_exit:08X}'
        host_log = (out/'net_selection_host_launcher.log').read_text(encoding='utf-8', errors='replace')
        assert re.search(rf'\[SESSION_RESULT\] code=user_exit stage=running exit={host_exit} reason=2\b', host_log)
        peer_log = (out/'net_selection_peer_launcher.log').read_text(encoding='utf-8', errors='replace')
        assert '[ PEER CLOSED ] reason=2' in peer_log
        result['checks']['net_selection_escape_and_notice'] = True
        # 通常の1000確定F・遅延・損失・観戦検証の中で終了抑止も確認する。
        stream = (out/'p2p.log').open('w', encoding='utf-8')
        streams.append(stream)
        network_started=time.time()
        proc = subprocess.Popen([sys.executable,'-X','utf8',str(ROOT/'src/src/harness/run_p2p_smoke.py'),
                                 '--real-game','--standby-spectator'], stdout=stream, stderr=subprocess.STDOUT,
                                env=clean_environment(), creationflags=subprocess.CREATE_NO_WINDOW)
        launchers.append(proc)
        network=[]
        for side in (1,2):
            game=Game(side,proc,started=network_started)
            games.append(game)
            network.append(game)
            result['processes'].append(dict(side=side, harness=proc.pid, game=game.process.pid, created=game.created, hwnd=hex(game.hwnd)))
        for side, game in enumerate(network,1):
            wait(game.battle, 'ネット対戦開始')
            result['checks'][f'net_battle_guard_{side}'] = game.blocked()
        assert proc.wait(timeout=90) == 0, 'P2P回帰失敗'
        p2p_text = (out/'p2p.log').read_text(encoding='utf-8', errors='replace')
        folders = re.findall(r'I:[^\r\n]*p2p_real_\d+_\d+_\d+', p2p_text)
        assert folders, 'P2P結果パスなし'
        folder = Path(folders[-1].strip())
        p2p_result = json.loads((folder/'result.json').read_text(encoding='utf-8'))
        assert p2p_result['passed'] and p2p_result['protected_unchanged']
        result['p2p_result'] = str(folder/'result.json')
    except Exception:
        result['errors'].append(traceback.format_exc())
    finally:
        # 1段の失敗でも設定復元・結果保存を必ず試す。終了するのは保持した今回のprocessだけ。
        def cleanup(label, action):
            try:
                action()
                result['cleanup'].append(dict(step=label, success=True))
            except Exception:
                error=traceback.format_exc()
                result['cleanup'].append(dict(step=label, success=False, error=error))
                result['errors'].append(label+': '+error)
        for game in games:
            cleanup(f'game_exit_{game.process.pid}', game.terminate)
            cleanup(f'game_handle_{game.process.pid}', game.close_handle)
        for proc in reversed(launchers):
            def stop_launcher(proc=proc):
                if proc.poll() is None:
                    proc.terminate()
                    proc.wait(timeout=10)
            cleanup(f'launcher_{proc.pid}', stop_launcher)
        for index,stream in enumerate(streams):
            cleanup(f'log_{index}', stream.close)
        for path, saved in backups.items():
            def restore(path=path,saved=saved):
                if saved is None:
                    path.unlink(missing_ok=True)
                else:
                    path.write_bytes(saved)
            cleanup('restore_'+str(path),restore)
        for index,pad in enumerate(pads):
            def reset(pad=pad):
                pad.reset()
                pad.update()
            cleanup(f'pad_{index}',reset)
        cleanup('service_shutdown',service.shutdown)
        cleanup('service_close',service.server_close)
        result['protected_unchanged']=False
        try:
            result['protected_unchanged'] = before == protected_files(sides)
        except Exception:
            result['errors'].append('protected_files: '+traceback.format_exc())
        result['passed'] = not result['errors'] and result['protected_unchanged']
        (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(result,ensure_ascii=False,indent=2),flush=True)
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
