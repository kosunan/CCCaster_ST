"""標準リプレイを通常の画面操作で選び、再生完了とファイル保全を検査する。

ready.jsonのPIDをComputer Useで確認し、一覧から決定する。ゲームメモリは読取りのみ。
GUIのworker起動時間はbench_startup.py --mode replayで別に測る。
"""
import argparse
import hashlib
import json
import os
import random
from pathlib import Path
import shutil
import subprocess
import sys
import time

from bench_legacy_real import processes, open_process, close, path_of, read32, wait, terminate
from bench_startup import protected_files, readiness
from steam_runtime import module_base, preferred

ROOT = Path(__file__).resolve().parents[3]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, default=ROOT/'test/runtime/MBAACC_1')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--timeout', type=int, default=180)
    parser.add_argument('--virtual-input', action='store_true', help='既存ViGEmの通常DirectInputで決定する')
    args = parser.parse_args()
    game_dir, out = args.game_dir.resolve(), args.out.resolve()
    if not game_dir.is_relative_to(ROOT/'test/runtime'):
        raise RuntimeError('test/runtime内の独立コピーを指定してください')
    game = game_dir/'MBAA.exe'
    caster = game_dir/'cccaster_st'
    for pid, _, name in processes():
        if args.virtual_input and name.lower() == 'mbaa.exe':
            raise RuntimeError('仮想入力の混入を避けるため他の実ゲームと同時実行できません')
        if name.lower() not in ('mbaa.exe', 'cccaster_steam.exe', 'cccaster_steam_gui.exe'):
            continue
        handle = open_process(0x1000, False, pid)
        if not handle:
            raise RuntimeError(f'既存プロセスを照合できません: {pid}')
        try:
            if path_of(handle).is_relative_to(game_dir):
                raise RuntimeError('対象コピーを先にdeployで停止してください')
        finally:
            close(handle)
    out.mkdir(parents=True, exist_ok=False)
    binaries = {n: sha(caster/n) for n in ('CCCaster_Steam.exe', 'CCCaster_Steam_GUI.exe', 'libcccaster_steam_hook.dll')}
    if any(value != sha(ROOT/'build/bin'/name) for name, value in binaries.items()):
        raise RuntimeError('build/binとのSHA-256不一致')
    before = protected_files([game_dir])
    (out/'protected_before.json').write_text(json.dumps(before, indent=2), encoding='utf-8')
    log = caster/'cccaster_hook_log.txt'
    if log.exists():
        shutil.move(log, out/'before.log')
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('CCCASTER_', 'CCBENCH_'))}
    env['CCCASTER_STARTUP_TRACE'] = '1'
    pad = None
    if args.virtual_input:
        sys.path.insert(0, str(ROOT/'build/virtual-pad-venv/Lib/site-packages'))
        import vgamepad as vg
        from vgamepad.win import vigem_client as vc
        # Steam Inputが既知DS4を別機器へ変換しないよう専用の試験PIDを使う。
        product = random.SystemRandom().randrange(0x8000, 0xFFFF)
        class TestPad(vg.VDS4Gamepad):
            def target_alloc(self):
                target = vc.vigem_target_ds4_alloc()
                vc.vigem_target_set_vid(target, 0x054C)
                vc.vigem_target_set_pid(target, product)
                return target
        pad = TestPad()
        env['CCCASTER_TEST_VIRTUAL_PRODUCT'] = f'{(product << 16) | 0x054C:08X}'
        time.sleep(1)
    proc = handle = None
    result = dict(passed=False, binaries=binaries, samples=[], active_samples=0)
    try:
        with (out/'launcher.log').open('wb') as stream:
            proc = subprocess.Popen([str(caster/'CCCaster_Steam.exe'), '--headless', '--replay'],
                cwd=caster, env=env, stdout=stream, stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW)
        deadline = time.monotonic() + args.timeout
        ready = playing = False
        active_worlds = set()
        while time.monotonic() < deadline:
            if proc.poll() is not None:
                raise RuntimeError('再生完了前にランチャーが終了しました')
            if not handle:
                for pid, parent, name in processes():
                    if parent != proc.pid or name.lower() != 'mbaa.exe':
                        continue
                    candidate = open_process(0x1010 | 0x100000 | 1, False, pid)
                    if not candidate:
                        continue
                    if path_of(candidate) == game:
                        try:
                            image_base = module_base(pid)
                        except OSError as exc:
                            close(candidate)
                            if exc.winerror in (24, 299):
                                continue  # suspended起動直後のモジュール列挙を再試行
                            raise
                        handle = candidate
                        result['game_pid'] = pid
                        break
                    close(candidate)
            if handle:
                if wait(handle, 0) == 0:
                    raise RuntimeError('再生完了前にゲームが終了しました')
                try:
                    mode, world, round_no = (read32(handle, image_base + preferred(address) - 0x400000) for address in (0x54EEE8, 0x55D1D4, 0x77BFA4))
                except OSError as exc:
                    # Steam起動直後は展開前のデータページをまだ読めない。
                    # 起動上限は維持し、ready後の読取り障害は隠さない。
                    if not ready and exc.winerror == 299:
                        time.sleep(.025)
                        continue
                    raise
                result['samples'].append(dict(time=time.monotonic(), mode=mode, world=world, round=round_no))
                if not ready and mode == 26 and log.exists():
                    ready = readiness(log.read_text(encoding='utf-8', errors='replace'), 'replay')['ready']
                    if ready:
                        (out/'ready.json').write_text(json.dumps(dict(game_pid=result['game_pid'])), encoding='utf-8')
                        print(f'READY pid={result["game_pid"]}: 一覧のリプレイを通常の決定入力で開始', flush=True)
                        if pad:
                            # 一覧の初期化・入力の解放ゲートを通常更新で完了してから押す。
                            time.sleep(.5)
                            pad.press_button(vg.DS4_BUTTONS.DS4_BUTTON_SQUARE)
                            pad.update()
                            time.sleep(.18)
                            pad.reset()
                            pad.update()
                if ready and mode == 1:
                    playing = True
                    active_worlds.add((round_no, world))
                result['active_samples'] = len(active_worlds)
                if playing and mode in (5, 26):
                    if len(active_worlds) < 120:
                        raise RuntimeError('再生中の通常進行標本が不足しています')
                    result['terminal_mode'] = mode
                    result['playback_complete'] = True
                    result['passed'] = True
                    break
            time.sleep(.025)
        else:
            raise TimeoutError('一覧・再生・終了の条件が揃いませんでした')
    except Exception as exc:
        result['error'] = str(exc)
    finally:
        if pad:
            pad.reset()
            pad.update()
        if proc and proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=5)
        if handle:
            if wait(handle, 0) != 0:
                terminate(handle, 0)
                if wait(handle, 3000) != 0:
                    result['passed'] = False
                    result['cleanup_error'] = 'テストゲームの終了を確認できません'
            close(handle)
        after = protected_files([game_dir])
        (out/'protected_after.json').write_text(json.dumps(after, indent=2), encoding='utf-8')
        result['protected_unchanged'] = before == after
        result['protected_count'] = len(before)
        result['passed'] = result['passed'] and result['protected_unchanged']
        if log.exists():
            shutil.copy2(log, out/'game.log')
        (out/'result.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
        print(json.dumps({k: v for k, v in result.items() if k != 'samples'}, ensure_ascii=False, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
