# Usage: syncheck.py <build_dir> <substring filter> [jobs]
# Runs -fsyntax-only for matching entries of compile_commands.json, PCH removed, and prints a summary.
import json, shlex, subprocess, sys, os, re
from concurrent.futures import ThreadPoolExecutor
bd, flt = sys.argv[1], sys.argv[2]
jobs = int(sys.argv[3]) if len(sys.argv) > 3 else 4
cc = json.load(open(os.path.join(bd, 'compile_commands.json')))
ents = [e for e in cc if flt in e['file'] and e['file'].endswith('.cpp')]
def run(e):
    a = shlex.split(e['command'])
    out = []
    skip = False
    for i, x in enumerate(a):
        if skip: skip = False; continue
        if x in ('-o', '-MF', '-MQ'): skip = True; continue
        if x in ('-include',) and i + 1 < len(a) and a[i + 1].endswith('.pch.h'): skip = True; continue
        if x in ('-fpch-preprocess', '-MD', '-c', '-Winvalid-pch'): continue
        out.append(x)
    # Include the real PCH header so its declarations are visible.
    pch = None
    if 'src/dxvk/' in e['file']: pch = '../dxvk-remix/src/dxvk/pch/dxvk.pch.h'
    if 'src/d3d9/' in e['file']: pch = '../dxvk-remix/src/d3d9/pch/d3d9.pch.h'
    if pch:
        k = next(i for i, x in enumerate(out) if x.endswith('remix_linux_compat.h'))
        out[k + 1:k + 1] = ['-include', pch]
    out += ['-fsyntax-only', '-fdiagnostics-color=never', '-fmax-errors=8', '-w']
    r = subprocess.run(out, cwd=e['directory'], capture_output=True, text=True)
    return e['file'], r.returncode, r.stderr
res = list(ThreadPoolExecutor(jobs).map(run, ents))
bad = [r for r in res if r[1] != 0]
print(f'{len(ents)} files, {len(bad)} failing')
for f, rc, err in bad:
    errs = [l for l in err.split('\n') if ': error:' in l or 'fatal error' in l]
    print('==', os.path.relpath(f, bd))
    for l in list(dict.fromkeys(errs))[:int(os.environ.get('N', '3'))]:
        print('   ', re.sub(r'^.*?dxvk-remix/', '', l)[:220])
