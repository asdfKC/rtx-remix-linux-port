/* Loads libremix.so, resolves remixapi_InitializeLibrary and initializes the C API.
 * remixapi_InitializeLibrary only fills the function table, so this runs without a GPU.
 * It does not call Startup, which creates the Vulkan device.
 *
 * Build: cc -I<dxvk-remix>/public/include tests/load_remix.c -ldl -o load_remix
 * Run:   ./load_remix <path to libremix.so>
 */
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#include <remix/remix_c.h>

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <libremix.so>\n", argv[0]);
    return 2;
  }
  void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!lib) {
    fprintf(stderr, "dlopen failed: %s\n", dlerror());
    return 1;
  }
  PFN_remixapi_InitializeLibrary init = (PFN_remixapi_InitializeLibrary) dlsym(lib, "remixapi_InitializeLibrary");
  if (!init) {
    fprintf(stderr, "remixapi_InitializeLibrary not exported: %s\n", dlerror());
    return 1;
  }
  remixapi_InitializeLibraryInfo info;
  memset(&info, 0, sizeof(info));
  info.sType = REMIXAPI_STRUCT_TYPE_INITIALIZE_LIBRARY_INFO;
  info.version = REMIXAPI_VERSION_MAKE(REMIXAPI_VERSION_MAJOR, REMIXAPI_VERSION_MINOR, REMIXAPI_VERSION_PATCH);

  remixapi_Interface iface;
  memset(&iface, 0, sizeof(iface));
  remixapi_ErrorCode rc = init(&info, &iface);
  printf("remixapi_InitializeLibrary(%d.%d.%d) -> %d\n",
         REMIXAPI_VERSION_MAJOR, REMIXAPI_VERSION_MINOR, REMIXAPI_VERSION_PATCH, (int) rc);
  if (rc != REMIXAPI_ERROR_CODE_SUCCESS) {
    return 1;
  }

  /* Count the filled and missing entries of the interface table. */
  void** fns = (void**) &iface;
  size_t n = sizeof(iface) / sizeof(void*);
  size_t missing = 0;
  for (size_t i = 0; i < n; ++i) {
    if (fns[i] == NULL) {
      ++missing;
    }
  }
  printf("interface entries: %zu, null: %zu\n", n, missing);
  printf("Startup=%p Present=%p CreateMesh=%p DrawInstance=%p\n",
         (void*) iface.Startup, (void*) iface.Present, (void*) iface.CreateMesh, (void*) iface.DrawInstance);
  return missing == 0 ? 0 : 3;
}
