/* Headless smoke test: InitializeLibrary, Startup, a few Presents, Shutdown.
 * Needs a Vulkan device with ray tracing (an NVIDIA RTX GPU, or Mesa lavapipe for a software run).
 * It renders an empty scene and checks that the headless path neither crashes nor stalls.
 *
 * Build: cc -I<dxvk-remix>/public/include tests/startup_remix.c -ldl -o startup_remix
 * Run:   ./startup_remix <path to libremix.so> [frames]
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <remix/remix_c.h>

typedef void (*PFN_remixlinux_SetWindowSize)(uint32_t, uint32_t);

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <libremix.so> [frames]\n", argv[0]);
    return 2;
  }
  const int frames = argc > 2 ? atoi(argv[2]) : 3;
  void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!lib) {
    fprintf(stderr, "%s\n", dlerror());
    return 1;
  }
  PFN_remixapi_InitializeLibrary init = (PFN_remixapi_InitializeLibrary) dlsym(lib, "remixapi_InitializeLibrary");
  PFN_remixlinux_SetWindowSize setSize = (PFN_remixlinux_SetWindowSize) dlsym(lib, "remixlinux_SetWindowSize");
  if (!init || !setSize) {
    fprintf(stderr, "missing export\n");
    return 1;
  }

  remixapi_InitializeLibraryInfo info;
  memset(&info, 0, sizeof(info));
  info.sType = REMIXAPI_STRUCT_TYPE_INITIALIZE_LIBRARY_INFO;
  info.version = REMIXAPI_VERSION_MAKE(REMIXAPI_VERSION_MAJOR, REMIXAPI_VERSION_MINOR, REMIXAPI_VERSION_PATCH);
  remixapi_Interface remix;
  memset(&remix, 0, sizeof(remix));
  if (init(&info, &remix) != REMIXAPI_ERROR_CODE_SUCCESS) {
    fprintf(stderr, "InitializeLibrary failed\n");
    return 1;
  }

  setSize(640, 360);

  remixapi_StartupInfo s;
  memset(&s, 0, sizeof(s));
  s.sType = REMIXAPI_STRUCT_TYPE_STARTUP_INFO;
  remixapi_ErrorCode rc = remix.Startup(&s);
  printf("Startup -> 0x%x\n", (unsigned) rc);
  if (rc != REMIXAPI_ERROR_CODE_SUCCESS) {
    return 1;
  }

  for (int i = 0; i < frames; ++i) {
    remixapi_PresentInfo p;
    memset(&p, 0, sizeof(p));
    p.sType = REMIXAPI_STRUCT_TYPE_PRESENT_INFO;
    p.skipVulkanSwapchain = 1;
    rc = remix.Present(&p);
    printf("Present %d -> 0x%x\n", i, (unsigned) rc);
    if (rc != REMIXAPI_ERROR_CODE_SUCCESS) {
      return 1;
    }
  }

  rc = remix.Shutdown();
  printf("Shutdown -> 0x%x\n", (unsigned) rc);
  return rc == REMIXAPI_ERROR_CODE_SUCCESS ? 0 : 1;
}
