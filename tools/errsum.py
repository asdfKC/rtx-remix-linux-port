import re, sys, collections
log = open(sys.argv[1], errors='replace').read()
ansi = re.compile(r'\x1b\[[0-9;]*m')
log = ansi.sub('', log)
failed = re.findall(r'^FAILED: (.*)$', log, re.M)
print(f'{len(failed)} FAILED')
errs = collections.OrderedDict()
for m in re.finditer(r'^(\.\./dxvk-remix/[^:\n]+):(\d+):\d+: (fatal error|error): (.*)$', log, re.M):
    f, line, kind, msg = m.groups()
    errs.setdefault(f, []).append(f'{line}: {msg}')
lim = int(sys.argv[2]) if len(sys.argv) > 2 else 3
for f, l in errs.items():
    uniq = list(dict.fromkeys(l))
    print(f'== {f} ({len(uniq)})')
    for e in uniq[:lim]:
        print('   ', e[:200])
