#!/bin/bash
# Usage: find_bad_uconvert.sh <shader.slang> [extra slangc args]
S=/home/user/work/dxvk-remix
INC="-I$S/src/dxvk/shaders -I$S/submodules/rtxdi/rtxdi-sdk/include -I$S/submodules/rtxcr/shaders/include -I$S/submodules -I$S/submodules/rtxdi/rtxdi-sdk/include/volumetrics"
OUT=/tmp/claude-0/bad.spv
src=$1; shift
$S/external/slang/slangc -entry main -target spirv -zero-initialize -emit-spirv-directly $INC "$src" -D__SLANG__ "$@" -matrix-layout-column-major -Wno-30081 -fvk-use-scalar-layout -skip-spirv-validation -g2 -o $OUT 2>&1 | grep -v "warning\|^ \|^\^\|^$" | head -3
spirv-dis $OUT -o $OUT.asm
python3 - $OUT.asm <<'PY'
import re,sys
L=open(sys.argv[1]).read().split('\n')
types={}; idtype={}; strings={}; srcs={}
for l in L:
    m=re.match(r'\s*(%\S+) = OpType(\w+)',l)
    if m: types[m.group(1)]=m.group(2)
    m=re.match(r'\s*(%\S+) = Op\w+ (%\S+)',l)
    if m: idtype[m.group(1)]=m.group(2)
    m=re.match(r'\s*(%\S+) = OpString "(.*)"',l)
    if m: strings[m.group(1)]=m.group(2)
    m=re.match(r'\s*(%\S+) = OpExtInst %void %\S+ DebugSource (%\S+)',l)
    if m: srcs[m.group(1)]=m.group(2)
consts={}
for l in L:
    m=re.match(r'\s*(%\S+) = OpConstant %uint (\d+)',l)
    if m: consts[m.group(1)]=m.group(2)
seen=set()
for i,l in enumerate(L):
    m=re.match(r'\s*(%\S+) = Op([US]Convert|FConvert) (%\S+) (%\S+)',l)
    if not m: continue
    rt=types.get(m.group(3)); ot=types.get(idtype.get(m.group(4),''),'?')
    if (rt=='Vector')!=(ot=='Vector'):
        for j in range(i,max(0,i-400),-1):
            d=re.search(r'DebugLine (%\S+) (%\S+)',L[j])
            if d:
                f=strings.get(srcs.get(d.group(1),''),'?'); ln=consts.get(d.group(2),d.group(2))
                key=(f,ln)
                if key not in seen:
                    seen.add(key); print(f'{f}:{ln}')
                break
PY
