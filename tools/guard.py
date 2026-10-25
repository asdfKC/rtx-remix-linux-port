"""Wrap C++ constructs in a preprocessor guard.

guard_func(src, signature, macro, stub=None, note=...)
  Finds the definition that starts with `signature` (text up to the opening brace),
  wraps it in `#ifndef macro` and, when `stub` is given, adds an `#else` with a replacement definition.
guard_text(src, text, macro, note=...)
  Wraps an exact block of text.
"""

def _line_start(s, i):
    return s.rfind('\n', 0, i) + 1

def _match_brace(s, j):
    depth = 0
    k = j
    while k < len(s):
        c = s[k]
        if c == '/' and s.startswith('//', k):
            k = s.index('\n', k)
            continue
        if c == '/' and s.startswith('/*', k):
            k = s.index('*/', k) + 2
            continue
        if c in '"\'':
            q = c
            k += 1
            while s[k] != q:
                if s[k] == '\\':
                    k += 1
                k += 1
        elif c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                return k
        k += 1
    raise ValueError('unbalanced braces')

def guard_func(s, signature, macro, stub=None, note='USD is compiled out of the native Linux build'):
    assert s.count(signature) == 1, (signature, s.count(signature))
    i = s.index(signature)
    start = _line_start(s, i)
    j = s.index('{', i)
    k = _match_brace(s, j)
    end = s.index('\n', k) + 1
    body = s[start:end]
    out = f'// NV-DXVK start: {note}\n#ifndef {macro}\n// NV-DXVK end\n' + body
    if stub is not None:
        out += '// NV-DXVK start: ' + note + '\n#else\n' + stub.rstrip('\n') + '\n#endif\n// NV-DXVK end\n'
    else:
        out += f'// NV-DXVK start: {note}\n#endif\n// NV-DXVK end\n'
    return s[:start] + out + s[end:]

def guard_text(s, text, macro, note='USD is compiled out of the native Linux build', stub=None):
    assert s.count(text) == 1, (text[:80], s.count(text))
    out = f'// NV-DXVK start: {note}\n#ifndef {macro}\n// NV-DXVK end\n' + text
    if stub is not None:
        out += '// NV-DXVK start: ' + note + '\n#else\n' + stub.rstrip('\n') + '\n#endif\n// NV-DXVK end\n'
    else:
        out += f'// NV-DXVK start: {note}\n#endif\n// NV-DXVK end\n'
    return s.replace(text, out, 1)
