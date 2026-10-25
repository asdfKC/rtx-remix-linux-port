#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <remix/remix_c.h>
int main(int argc, char** argv) {
  void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!lib) { fprintf(stderr, "%s\n", dlerror()); return 1; }
  PFN_remixapi_InitializeLibrary init = (PFN_remixapi_InitializeLibrary) dlsym(lib, "remixapi_InitializeLibrary");
  remixapi_InitializeLibraryInfo info; memset(&info, 0, sizeof(info));
  info.sType = REMIXAPI_STRUCT_TYPE_INITIALIZE_LIBRARY_INFO;
  info.version = REMIXAPI_VERSION_MAKE(REMIXAPI_VERSION_MAJOR, REMIXAPI_VERSION_MINOR, REMIXAPI_VERSION_PATCH);
  remixapi_Interface iface; memset(&iface, 0, sizeof(iface));
  if (init(&info, &iface) != REMIXAPI_ERROR_CODE_SUCCESS) return 1;
  remixapi_StartupInfo s; memset(&s, 0, sizeof(s));
  s.sType = REMIXAPI_STRUCT_TYPE_STARTUP_INFO;
  s.hwnd = (remixapi_HWND) 1;
  remixapi_ErrorCode rc = iface.Startup(&s);
  printf("Startup -> 0x%x\n", (unsigned) rc);
  return 0;
}
