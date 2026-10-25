"""Applies the native Linux changes to a remix_c.h (public 0.6.x or the mod's 0.16.x copy)."""
import sys
p = sys.argv[1]
s = open(p).read()
if 'REMIX_NATIVE_POSIX' in s:
    sys.exit(0)

old = '#include <stdint.h>\n'
assert old in s
s = s.replace(old, old + '''
// Non-Windows builds (native Linux): no Windows headers, no Win32 library loader,
// default-visibility exports and the platform calling convention.
#if !defined(_WIN32)
  #define REMIX_NATIVE_POSIX 1
  #ifndef REMIX_WINAPI_NO_INCLUDE
    #define REMIX_WINAPI_NO_INCLUDE
  #endif
  #ifndef REMIX_WINAPI_NO_LIBRARY_LOADER
    #define REMIX_WINAPI_NO_LIBRARY_LOADER
  #endif
  #ifndef REMIXAPI_CALL
    #define REMIXAPI_CALL
  #endif
  #ifndef REMIXAPI
    #define REMIXAPI __attribute__((visibility("default")))
  #endif
#endif
''', 1)

old = '#if _WIN64 != 1\n'
assert old in s
s = s.replace(old, '#if (defined(_WIN32) && _WIN64 != 1) || (!defined(_WIN32) && !defined(__LP64__))\n', 1)

old = '  typedef long long(__stdcall* remixapi_loader_PROC)();\n'
assert old in s
s = s.replace(old, '  typedef long long(REMIXAPI_CALL* remixapi_loader_PROC)();\n', 1)
open(p, 'w').write(s)
