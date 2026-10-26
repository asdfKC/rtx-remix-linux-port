/* Retained scene smoke test (Phase C step 2): material, mesh, a retained instance across frames,
 * SceneInstanceUpdate, UpdateMaterial, SceneInstanceRemove, error paths, Shutdown.
 * Needs a Vulkan device with ray tracing (an NVIDIA RTX GPU, or Mesa lavapipe for a software run).
 *
 * Build: cc -I<dxvk-remix>/public/include tests/scene_remix.c -ldl -o scene_remix
 * Run:   ./scene_remix <path to libremix.so>
 * Exit code 0 means every check passed.
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <remix/remix_c.h>

typedef void (*PFN_remixlinux_SetWindowSize)(uint32_t, uint32_t);
typedef void (*PFN_remixlinux_GetRetainedSceneStats)(uint64_t*, uint64_t*);

static remixapi_Interface remix;
static PFN_remixlinux_GetRetainedSceneStats getStats;
static int failures = 0;

#define CHECK(what, expr, expected)                                                  \
  do {                                                                               \
    remixapi_ErrorCode rc_ = (expr);                                                 \
    printf("%-46s -> 0x%x%s\n", what, (unsigned) rc_, rc_ == (expected) ? "" : "  FAIL"); \
    if (rc_ != (expected)) {                                                         \
      failures++;                                                                    \
    }                                                                                \
  } while (0)

static void setupCamera(void) {
  remixapi_CameraInfo cam;
  memset(&cam, 0, sizeof(cam));
  cam.sType = REMIXAPI_STRUCT_TYPE_CAMERA_INFO;
  cam.type = REMIXAPI_CAMERA_TYPE_WORLD;
  for (int i = 0; i < 4; ++i) {
    cam.view[i][i] = 1.0f;
  }
  /* A simple perspective projection: 90 degree vertical FOV, 16:9, near 0.1, far 1000, row-vector convention. */
  const float f = 1.0f, aspect = 16.0f / 9.0f, n = 0.1f, fa = 1000.0f;
  cam.projection[0][0] = f / aspect;
  cam.projection[1][1] = f;
  cam.projection[2][2] = fa / (fa - n);
  cam.projection[2][3] = 1.0f;
  cam.projection[3][2] = -n * fa / (fa - n);
  remix.SetupCamera(&cam);
}

static remixapi_ErrorCode frame(void) {
  setupCamera();
  remixapi_SubmitInfo submit;
  memset(&submit, 0, sizeof(submit));
  submit.sType = REMIXAPI_STRUCT_TYPE_SUBMIT_INFO;
  remixapi_ErrorCode rc = remix.Submit(&submit);
  if (rc != REMIXAPI_ERROR_CODE_SUCCESS) {
    return rc;
  }
  remixapi_PresentInfo present;
  memset(&present, 0, sizeof(present));
  present.sType = REMIXAPI_STRUCT_TYPE_PRESENT_INFO;
  present.skipVulkanSwapchain = 1;
  return remix.Present(&present);
}

static void printStats(const char* label, uint64_t* liveOut, uint64_t* drawsOut) {
  uint64_t live = 0, draws = 0;
  getStats(&live, &draws);
  printf("%-46s    live=%llu replayedDraws=%llu\n", label, (unsigned long long) live, (unsigned long long) draws);
  if (liveOut) *liveOut = live;
  if (drawsOut) *drawsOut = draws;
}

static void fillInstance(remixapi_InstanceInfo* inst, remixapi_MeshHandle mesh, float z) {
  memset(inst, 0, sizeof(*inst));
  inst->sType = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO;
  inst->mesh = mesh;
  inst->transform.matrix[0][0] = 1.0f;
  inst->transform.matrix[1][1] = 1.0f;
  inst->transform.matrix[2][2] = 1.0f;
  inst->transform.matrix[2][3] = z;
  inst->doubleSided = 1;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <libremix.so>\n", argv[0]);
    return 2;
  }
  void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!lib) {
    fprintf(stderr, "%s\n", dlerror());
    return 1;
  }
  PFN_remixapi_InitializeLibrary init = (PFN_remixapi_InitializeLibrary) dlsym(lib, "remixapi_InitializeLibrary");
  PFN_remixlinux_SetWindowSize setSize = (PFN_remixlinux_SetWindowSize) dlsym(lib, "remixlinux_SetWindowSize");
  getStats = (PFN_remixlinux_GetRetainedSceneStats) dlsym(lib, "remixlinux_GetRetainedSceneStats");
  if (!init || !setSize || !getStats) {
    fprintf(stderr, "missing export\n");
    return 1;
  }

  remixapi_InitializeLibraryInfo info;
  memset(&info, 0, sizeof(info));
  info.sType = REMIXAPI_STRUCT_TYPE_INITIALIZE_LIBRARY_INFO;
  info.version = REMIXAPI_VERSION_MAKE(REMIXAPI_VERSION_MAJOR, REMIXAPI_VERSION_MINOR, REMIXAPI_VERSION_PATCH);
  CHECK("InitializeLibrary", init(&info, &remix), REMIXAPI_ERROR_CODE_SUCCESS);

  setSize(640, 360);
  remixapi_StartupInfo startup;
  memset(&startup, 0, sizeof(startup));
  startup.sType = REMIXAPI_STRUCT_TYPE_STARTUP_INFO;
  CHECK("Startup", remix.Startup(&startup), REMIXAPI_ERROR_CODE_SUCCESS);
  if (failures) {
    return 1;
  }

  /* Material: opaque, untextured. */
  remixapi_MaterialInfoOpaqueEXT opaque;
  memset(&opaque, 0, sizeof(opaque));
  opaque.sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_EXT;
  opaque.albedoConstant.x = 0.8f;
  opaque.albedoConstant.y = 0.2f;
  opaque.albedoConstant.z = 0.2f;
  opaque.opacityConstant = 1.0f;
  opaque.roughnessConstant = 0.5f;
  opaque.metallicConstant = 0.0f;
  remixapi_MaterialInfo mat;
  memset(&mat, 0, sizeof(mat));
  mat.sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO;
  mat.pNext = &opaque;
  mat.hash = 0x1234;
  remixapi_MaterialHandle material = NULL;
  CHECK("CreateMaterial", remix.CreateMaterial(&mat, &material), REMIXAPI_ERROR_CODE_SUCCESS);

  /* Mesh: one quad, two triangles. */
  remixapi_HardcodedVertex verts[4];
  memset(verts, 0, sizeof(verts));
  const float pos[4][3] = { { -1, -1, 0 }, { 1, -1, 0 }, { 1, 1, 0 }, { -1, 1, 0 } };
  for (int i = 0; i < 4; ++i) {
    memcpy(verts[i].position, pos[i], sizeof(pos[i]));
    verts[i].normal[2] = -1.0f;
    verts[i].texcoord[0] = pos[i][0] * 0.5f + 0.5f;
    verts[i].texcoord[1] = pos[i][1] * 0.5f + 0.5f;
    verts[i].color = 0xFFFFFFFFu;
  }
  const uint32_t indices[6] = { 0, 1, 2, 0, 2, 3 };
  remixapi_MeshInfoSurfaceTriangles surf;
  memset(&surf, 0, sizeof(surf));
  surf.vertices_values = verts;
  surf.vertices_count = 4;
  surf.indices_values = indices;
  surf.indices_count = 6;
  surf.material = material;
  remixapi_MeshInfo meshInfo;
  memset(&meshInfo, 0, sizeof(meshInfo));
  meshInfo.sType = REMIXAPI_STRUCT_TYPE_MESH_INFO;
  meshInfo.hash = 0x5678;
  meshInfo.surfaces_values = &surf;
  meshInfo.surfaces_count = 1;
  remixapi_MeshHandle mesh = NULL;
  CHECK("CreateMesh", remix.CreateMesh(&meshInfo, &mesh), REMIXAPI_ERROR_CODE_SUCCESS);

  /* Retained instance, rendered for several frames without further calls. */
  remixapi_InstanceInfo inst;
  fillInstance(&inst, mesh, 3.0f);
  remixapi_SceneInstanceHandle instance = NULL;
  CHECK("SceneInstanceAdd", remix.SceneInstanceAdd(&inst, &instance), REMIXAPI_ERROR_CODE_SUCCESS);
  memset(&inst, 0xCD, sizeof(inst)); /* The runtime must not read the info after the call. */

  for (int i = 0; i < 4; ++i) {
    char label[64];
    snprintf(label, sizeof(label), "frame %d (instance unchanged)", i);
    CHECK(label, frame(), REMIXAPI_ERROR_CODE_SUCCESS);
  }
  uint64_t live = 0, drawsAfterAdd = 0;
  printStats("stats after 4 frames", &live, &drawsAfterAdd);
  if (live != 1 || drawsAfterAdd < 1) {
    printf("FAIL: expected 1 live instance and at least 1 replayed draw\n");
    failures++;
  }

  /* Present without Submit must still render retained instances. */
  remixapi_PresentInfo present;
  memset(&present, 0, sizeof(present));
  present.sType = REMIXAPI_STRUCT_TYPE_PRESENT_INFO;
  setupCamera();
  CHECK("Present without Submit", remix.Present(&present), REMIXAPI_ERROR_CODE_SUCCESS);

  /* Update: move the instance, then change the material. */
  fillInstance(&inst, mesh, 5.0f);
  CHECK("SceneInstanceUpdate", remix.SceneInstanceUpdate(instance, &inst), REMIXAPI_ERROR_CODE_SUCCESS);
  opaque.albedoConstant.x = 0.2f;
  opaque.albedoConstant.z = 0.8f;
  CHECK("UpdateMaterial", remix.UpdateMaterial(material, &mat), REMIXAPI_ERROR_CODE_SUCCESS);
  for (int i = 0; i < 2; ++i) {
    CHECK("frame (after update)", frame(), REMIXAPI_ERROR_CODE_SUCCESS);
  }

  /* Remove, then the handle is invalid. */
  CHECK("SceneInstanceRemove", remix.SceneInstanceRemove(instance), REMIXAPI_ERROR_CODE_SUCCESS);
  for (int i = 0; i < 3; ++i) {
    CHECK("frame (after remove)", frame(), REMIXAPI_ERROR_CODE_SUCCESS);
  }
  uint64_t drawsAfterRemove = 0;
  printStats("stats after remove", &live, &drawsAfterRemove);
  if (live != 0) {
    printf("FAIL: expected 0 live instances after remove\n");
    failures++;
  }
  uint64_t drawsLater = 0;
  CHECK("frame (still removed)", frame(), REMIXAPI_ERROR_CODE_SUCCESS);
  CHECK("frame (still removed)", frame(), REMIXAPI_ERROR_CODE_SUCCESS);
  printStats("stats two frames later", NULL, &drawsLater);
  if (drawsLater != drawsAfterRemove) {
    printf("FAIL: a removed instance was still replayed\n");
    failures++;
  }

  /* Error paths. */
  fillInstance(&inst, mesh, 3.0f);
  CHECK("SceneInstanceUpdate (removed handle)", remix.SceneInstanceUpdate(instance, &inst), REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS);
  CHECK("SceneInstanceRemove (removed handle)", remix.SceneInstanceRemove(instance), REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS);
  CHECK("SceneInstanceAdd (null info)", remix.SceneInstanceAdd(NULL, &instance), REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS);
  inst.mesh = NULL;
  CHECK("SceneInstanceAdd (null mesh)", remix.SceneInstanceAdd(&inst, &instance), REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS);

  /* An instance left alive at Shutdown must not crash teardown. */
  fillInstance(&inst, mesh, 4.0f);
  CHECK("SceneInstanceAdd (kept until Shutdown)", remix.SceneInstanceAdd(&inst, &instance), REMIXAPI_ERROR_CODE_SUCCESS);
  CHECK("frame", frame(), REMIXAPI_ERROR_CODE_SUCCESS);

  CHECK("Shutdown", remix.Shutdown(), REMIXAPI_ERROR_CODE_SUCCESS);
  printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
