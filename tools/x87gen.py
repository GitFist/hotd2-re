"""Symbolic x87 -> C++ float expressions for straight-line matrix code (24-bit precision control: every op rounds to
float, so a float expression tree with the same shape is bit-exact).
usage: python tools/x87gen.py <va> <n> <base regs comma> [--stop <va>] [--init a,b]  (--init: x87 stack at <va>, ST0 last)
Example: the inverse 0x004A8D20 (mx::Inverse): x87gen.py 0x004A8D81 450 edi --init det (determinant: 0x004A8B60 120 eax).
Verify a port function against the original with tools/emu.py (set the FPU control word to 0x007F first: Direct3D runs
the game at 24-bit precision).
Memory [base + off] -> p[off/4]; [esp + off] -> locals named by frame offset (esp tracked)."""
import os, re, subprocess, sys

va, n = sys.argv[1], sys.argv[2]
bases = sys.argv[3].split(',')
stop = int(sys.argv[sys.argv.index('--stop') + 1], 16) if '--stop' in sys.argv else None
out = subprocess.run([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'disasm.py'), va, n],
                     capture_output=True, text=True).stdout  # run from the checkout with game\
st = sys.argv[sys.argv.index('--init') + 1].split(',') if '--init' in sys.argv else []  # x87 stack, st[-1] = ST0
esp = 0          # esp relative to entry (after the call: [esp] = return address at entry)
locals_ = {}     # frame offset -> expression name
temps = []       # emitted statements
tcount = [0]


def name(e):
    """Materialize an expression as a float temp (keeps trees small)."""
    if re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*(\[\d+\])?', e) or re.fullmatch(r'-?[0-9.]+f', e):
        return e
    tcount[0] += 1
    t = 't%d' % tcount[0]
    temps.append('const float %s = %s;' % (t, e))
    return t


def mem(arg):
    m = re.match(r'dword ptr \[(\w+)(?: ([+-]) (0x[0-9a-f]+|\d+))?\]', arg)
    if not m:
        raise ValueError('mem ' + arg)
    reg, sign, off = m.group(1), m.group(2), m.group(3)
    o = int(off, 0) if off else 0
    if sign == '-':
        o = -o
    if reg in bases:
        return ('p', o // 4)
    if reg == 'esp':
        return ('l', esp + o)
    raise ValueError('reg ' + arg)


def load(arg):
    k, o = mem(arg)
    if k == 'p':
        return 'p[%d]' % o
    if o not in locals_:
        raise ValueError('uninit local %d' % o)
    return locals_[o]


def sti(arg):
    m = re.match(r'st\((\d)\)', arg)
    return int(m.group(1))


for line in out.splitlines():
    m = re.match(r'0x([0-9A-F]+): [0-9a-f]+\s+(\S+)\s*(.*)', line)
    if not m:
        continue
    addr, op, arg = int(m.group(1), 16), m.group(2), m.group(3).strip()
    if stop is not None and addr >= stop:
        break
    if op in ('ret', 'rep'):
        break
    if op == 'sub' and arg.startswith('esp,'):
        esp -= int(arg.split(',')[1], 0); continue
    if op == 'add' and arg.startswith('esp,'):
        esp += int(arg.split(',')[1], 0); continue
    if op == 'push':
        esp -= 4; continue
    if op == 'pop':
        esp += 4; continue
    if op in ('mov', 'test', 'jne', 'je', 'call', 'fcom', 'fnstsw', 'lea', 'rep', 'cmp', 'jmp'):
        continue
    if op == 'fld':
        if arg.startswith('st('):
            st.append(st[-1 - sti(arg)])
        else:
            st.append(load(arg))
    elif op in ('fmul', 'fadd', 'fsub', 'fdiv', 'fsubr', 'fdivr'):
        sym = {'fmul': '*', 'fadd': '+', 'fsub': '-', 'fdiv': '/', 'fsubr': '-', 'fdivr': '/'}[op]
        if arg.startswith('dword'):
            b = load(arg)
            a = st[-1]
            st[-1] = name('(%s %s %s)' % ((b, sym, a) if op.endswith('r') else (a, sym, b)))
        else:
            parts = [x.strip() for x in arg.split(',')]
            if len(parts) == 1:  # st0 = st0 op st(i)
                i = sti(parts[0])
                a, b = st[-1], st[-1 - i]
                st[-1] = name('(%s %s %s)' % ((b, sym, a) if op.endswith('r') else (a, sym, b)))
            else:
                raise ValueError(line)
    elif op in ('faddp', 'fmulp', 'fsubp', 'fsubrp', 'fdivp', 'fdivrp'):
        sym = {'faddp': '+', 'fmulp': '*', 'fsubp': '-', 'fsubrp': '-', 'fdivp': '/', 'fdivrp': '/'}[op]
        i = sti(arg) if arg else 1
        a = st[-1 - i]  # st(i)
        b = st[-1]      # st0
        # Intel: fsubp st(i), st0 -> st(i) = st(i) - st0 ; fsubrp -> st0 - st(i)
        if op in ('fsubrp', 'fdivrp'):
            r = '(%s %s %s)' % (b, sym, a)
        else:
            r = '(%s %s %s)' % (a, sym, b)
        st[-1 - i] = name(r)
        st.pop()
    elif op == 'fxch':
        i = sti(arg) if arg else 1
        st[-1], st[-1 - i] = st[-1 - i], st[-1]
    elif op == 'fchs':
        st[-1] = name('(-%s)' % st[-1])
    elif op in ('fstp', 'fst'):
        if arg.startswith('st('):
            i = sti(arg)
            st[-1 - i] = st[-1]
            if op == 'fstp':
                st.pop()
        else:
            k, o = mem(arg)
            v = name(st[-1])
            if k == 'p':
                temps.append('o[%d] = %s;' % (o, v))
            else:
                locals_[o] = v
            if op == 'fstp':
                st.pop()
    else:
        raise ValueError('unhandled ' + line)

print('\n'.join(temps))
print('// locals:', ', '.join('%d=%s' % (k, v) for k, v in sorted(locals_.items())))
print('// stack:', st)
