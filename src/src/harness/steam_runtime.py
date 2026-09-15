"""実機プローブ用。Steamの実配置と検証済みデータ対応表を読み取る。"""
import ctypes as C
from ctypes import wintypes as W
from pathlib import Path
from functools import lru_cache
import re

ROOT = Path(__file__).resolve().parents[3]


def module_base(pid):
    class Module(C.Structure):
        _fields_ = [('size', W.DWORD), ('id', W.DWORD), ('pid', W.DWORD),
                    ('global_refs', W.DWORD), ('process_refs', W.DWORD),
                    ('base', C.c_void_p), ('bytes', W.DWORD), ('module', W.HMODULE),
                    ('name', W.WCHAR * 256), ('path', W.WCHAR * 260)]
    k = C.WinDLL('kernel32', use_last_error=True)
    k.CreateToolhelp32Snapshot.argtypes = [W.DWORD, W.DWORD]
    k.CreateToolhelp32Snapshot.restype = W.HANDLE
    k.Module32FirstW.argtypes = [W.HANDLE, C.POINTER(Module)]
    k.Module32NextW.argtypes = [W.HANDLE, C.POINTER(Module)]
    k.CloseHandle.argtypes = [W.HANDLE]
    snapshot = k.CreateToolhelp32Snapshot(0x18, pid)
    if snapshot == C.c_void_p(-1).value:
        raise C.WinError(C.get_last_error())
    try:
        entry = Module(); entry.size = C.sizeof(entry)
        valid = k.Module32FirstW(snapshot, C.byref(entry))
        while valid:
            if entry.name.lower() == 'mbaa.exe':
                if entry.bytes != 0xf3e000:
                    raise RuntimeError('Steam image size mismatch')
                return entry.base
            valid = k.Module32NextW(snapshot, C.byref(entry))
    finally:
        k.CloseHandle(snapshot)
    raise RuntimeError('Steam module not found')


@lru_cache(maxsize=None)
def preferred(legacy):
    # BGM: 521730(thread join), 5216A0(playing), 521771(marker)。
    special = {0x76e844: 0x7d2740, 0x76e838: 0x7d2744,
               0x76e004: 0x7d47a4, 0x472c6d: 0x4cb533}
    if legacy in special:
        return special[legacy]
    base = ROOT / 'src/src/core_dll'
    text = (base / 'mbaa_mem/SteamAddressMap.hpp').read_text(encoding='utf-8')
    for source, target in re.findall(r'case (0x[0-9a-f]+): return (0x[0-9a-f]+);', text, re.I):
        if int(source, 16) == legacy:
            return int(target, 16)
    for name in ('SteamSnapshotLayout.hpp', 'SteamRoundCallFragments.hpp',
                 'SteamDisplayFragments.hpp', 'SteamMiscFragments.hpp'):
        text = (base / 'rollback' / name).read_text(encoding='utf-8')
        for source, target, size in re.findall(
                r'\{\s*\d+,\s*(0x[0-9a-f]+),\s*(0x[0-9a-f]+),\s*(\d+|0x[0-9a-f]+),', text, re.I):
            source, target, size = int(source,16), int(target,16), int(size,0)
            if source <= legacy < source + size:
                return 0x400000 + target + legacy - source
    raise ValueError(f'未解析の旧アドレス: {legacy:08x}')
