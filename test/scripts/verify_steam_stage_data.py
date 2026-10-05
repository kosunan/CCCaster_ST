"""起動済みのSteam検証3コピーからステージ表を読み取る。書込み・プロセス制御なし。"""
import sys, ctypes as C, json, struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'src/src/harness'))
from steam_runtime import module_base
from bench_legacy_real import processes,open_process,path_of,close,rpm
results=[]
for pid,_,name in processes():
    if name.lower()!='mbaa.exe': continue
    handle=open_process(0x1010,False,pid)
    if not handle: continue
    try:
        path=path_of(handle)
        if path not in [ROOT/f'test/runtime/MBAACC_{i}/MBAA.exe' for i in (1,2,3)]: continue
        base=module_base(pid)
        def read(va,size):
            buf=C.create_string_buffer(size); count=C.c_size_t()
            if not rpm(handle,base+va-0x400000,buf,size,C.byref(count)) or count.value!=size:
                raise RuntimeError(f'Read failed: {pid} {va:x}')
            return buf.raw
        available=[i for i,v in enumerate(struct.unpack('<100I',read(0x7b6230,400))) if v]
        result=dict(pid=pid,path=str(path),base=hex(base),available=available,
                    training=list(struct.unpack('<3i',read(0x5b4e00,12))),
                    versus=list(struct.unpack('<19i',read(0x5b4e30,76))),
                    ryougi=read(0x7cfc78,16).hex())
        result['passed']=(len(available)==56 and {0,55,57,58}.issubset(available)
                          and result['training']==[-1]*3 and result['versus']==[-1]*19
                          and result['ryougi']=='00000000aacc1e4062676d6d65353500')
        results.append(result)
    finally: close(handle)
output=Path(sys.argv[1])
output.write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
print([(r['pid'],len(r['available']),r['passed']) for r in results])
raise SystemExit(0 if len(results)==3 and all(r['passed'] for r in results) else 1)
