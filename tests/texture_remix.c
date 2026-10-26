/* Texture and config-layer smoke test (Phase C step 3).
 * Memory textures in the formats and purposes the mod uses, batched UpdateTextureFromMemory, a DDS file texture,
 * a dome light, DestroyTexture while a material still uses the texture, error paths, and config layers checked
 * through GetConfigVariableResolved. Needs a Vulkan device with ray tracing (an NVIDIA RTX GPU, or Mesa lavapipe).
 *
 * Build: cc -I<dxvk-remix>/public/include tests/texture_remix.c -ldl -lm -o texture_remix
 * Run:   ./texture_remix <path to libremix.so> [scratch dir, default .]
 * Exit code 0 means every check passed.
 */
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <remix/remix_c.h>

typedef void (*PFN_remixlinux_SetWindowSize)(uint32_t, uint32_t);
typedef void (*PFN_remixlinux_GetApiResourceStats)(uint64_t*, uint64_t*, uint64_t*, uint64_t*);

static remixapi_Interface remix;
static PFN_remixlinux_GetApiResourceStats getStats;
static int failures = 0;

#define CHECK(what, expr, expected)                                                       \
  do {                                                                                    \
    remixapi_ErrorCode rc_ = (expr);                                                      \
    printf("%-52s -> 0x%x%s\n", what, (unsigned) rc_, rc_ == (expected) ? "" : "  FAIL"); \
    if (rc_ != (expected)) {                                                              \
      failures++;                                                                         \
    }                                                                                     \
  } while (0)

#define EXPECT(what, cond)                                    \
  do {                                                        \
    int ok_ = (cond);                                         \
    printf("%-52s    %s\n", what, ok_ ? "ok" : "FAIL");       \
    if (!ok_) {                                               \
      failures++;                                             \
    }                                                         \
  } while (0)

typedef struct { uint64_t textures, resolved, uploads, layers; } Stats;

static Stats stats(const char* label) {
  Stats s;
  getStats(&s.textures, &s.resolved, &s.uploads, &s.layers);
  printf("%-52s    textures=%llu resolved=%llu uploads=%llu layers=%llu\n", label, (unsigned long long) s.textures,
         (unsigned long long) s.resolved, (unsigned long long) s.uploads, (unsigned long long) s.layers);
  return s;
}

static void setupCamera(void) {
  remixapi_CameraInfo cam;
  memset(&cam, 0, sizeof(cam));
  cam.sType = REMIXAPI_STRUCT_TYPE_CAMERA_INFO;
  cam.type = REMIXAPI_CAMERA_TYPE_WORLD;
  for (int i = 0; i < 4; ++i) {
    cam.view[i][i] = 1.0f;
  }
  const float f = 1.0f, aspect = 16.0f / 9.0f, n = 0.1f, fa = 1000.0f;
  cam.projection[0][0] = f / aspect;
  cam.projection[1][1] = f;
  cam.projection[2][2] = fa / (fa - n);
  cam.projection[2][3] = 1.0f;
  cam.projection[3][2] = -n * fa / (fa - n);
  remix.SetupCamera(&cam);
}

static void frames(int count) {
  for (int i = 0; i < count; ++i) {
    setupCamera();
    remixapi_SubmitInfo submit;
    memset(&submit, 0, sizeof(submit));
    submit.sType = REMIXAPI_STRUCT_TYPE_SUBMIT_INFO;
    remixapi_PresentInfo present;
    memset(&present, 0, sizeof(present));
    present.sType = REMIXAPI_STRUCT_TYPE_PRESENT_INFO;
    present.skipVulkanSwapchain = 1;
    if (remix.Submit(&submit) != REMIXAPI_ERROR_CODE_SUCCESS || remix.Present(&present) != REMIXAPI_ERROR_CODE_SUCCESS) {
      printf("frame failed  FAIL\n");
      failures++;
      return;
    }
  }
}

static remixapi_TextureHandle createTexture(const char* label, uint64_t hash, uint8_t* pixels, uint32_t w, uint32_t h,
                                            remixapi_TextureFormat format, remixapi_TexturePurpose purpose, size_t bytes) {
  remixapi_TextureFromMemoryInfo info;
  memset(&info, 0, sizeof(info));
  info.sType = REMIXAPI_STRUCT_TYPE_TEXTURE_FROM_MEMORY_INFO;
  info.hash = hash;
  info.pixelData = pixels;
  info.width = w;
  info.height = h;
  info.format = format;
  info.purpose = purpose;
  remixapi_TextureHandle handle = NULL;
  CHECK(label, remix.CreateTextureFromMemory(&info, &handle), REMIXAPI_ERROR_CODE_SUCCESS);
  memset(pixels, 0xAB, bytes); /* The runtime must have copied the pixels during the call. */
  return handle;
}

static remixapi_ErrorCode createBad(uint32_t w, uint32_t h, int format, int purpose, int nullPixels) {
  static uint8_t pixels[16 * 16 * 16];
  remixapi_TextureFromMemoryInfo info;
  memset(&info, 0, sizeof(info));
  info.sType = REMIXAPI_STRUCT_TYPE_TEXTURE_FROM_MEMORY_INFO;
  info.pixelData = nullPixels ? NULL : pixels;
  info.width = w;
  info.height = h;
  info.format = (remixapi_TextureFormat) format;
  info.purpose = (remixapi_TexturePurpose) purpose;
  remixapi_TextureHandle handle = NULL;
  return remix.CreateTextureFromMemory(&info, &handle);
}

static int writeDds(const char* path, uint32_t w, uint32_t h) {
  /* Uncompressed 32-bit BGRA DDS, no mips. */
  uint32_t header[32];
  memset(header, 0, sizeof(header));
  header[0] = 0x20534444;            /* "DDS " */
  header[1] = 124;                   /* dwSize */
  header[2] = 0x1 | 0x2 | 0x4 | 0x8 | 0x1000; /* CAPS | HEIGHT | WIDTH | PITCH | PIXELFORMAT */
  header[3] = h;
  header[4] = w;
  header[5] = w * 4;                 /* pitch */
  header[19] = 32;                   /* ddspf.dwSize */
  header[20] = 0x40 | 0x1;           /* DDPF_RGB | DDPF_ALPHAPIXELS */
  header[22] = 32;                   /* RGB bit count */
  header[23] = 0x00FF0000;           /* R mask */
  header[24] = 0x0000FF00;           /* G mask */
  header[25] = 0x000000FF;           /* B mask */
  header[26] = 0xFF000000;           /* A mask */
  header[27] = 0x1000;               /* DDSCAPS_TEXTURE */
  FILE* f = fopen(path, "wb");
  if (!f) {
    return 0;
  }
  fwrite(header, 1, sizeof(header), f);
  for (uint32_t i = 0; i < w * h; ++i) {
    const uint8_t texel[4] = { (uint8_t) (i * 7), (uint8_t) (i * 3), (uint8_t) i, 255 };
    fwrite(texel, 1, 4, f);
  }
  fclose(f);
  return 1;
}

static int readConfig(const char* key, char* out, uint32_t outSize) {
  remixapi_ConfigVariableResolvedInfo info;
  memset(&info, 0, sizeof(info));
  info.sType = REMIXAPI_STRUCT_TYPE_CONFIG_VARIABLE_RESOLVED_INFO;
  info.key = key;
  info.value = out;
  info.valueSize = outSize;
  return remix.GetConfigVariableResolved(&info) == REMIXAPI_ERROR_CODE_SUCCESS;
}

static float readConfigFloat(const char* key) {
  char buf[64] = { 0 };
  return readConfig(key, buf, sizeof(buf)) ? strtof(buf, NULL) : NAN;
}

static int readConfigBool(const char* key) {
  char buf[64] = { 0 };
  if (!readConfig(key, buf, sizeof(buf))) {
    return -1;
  }
  return strcmp(buf, "True") == 0 || strcmp(buf, "true") == 0 || strcmp(buf, "1") == 0;
}

static const char* kFloatKey = "rtx.secondarySpecularFireflyFilteringThreshold"; /* default 1000 */
static const char* kBoolKey = "rtx.enablePortalFadeInEffect";                    /* default False */

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <libremix.so> [scratch dir]\n", argv[0]);
    return 2;
  }
  const char* scratch = argc > 2 ? argv[2] : ".";
  void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!lib) {
    fprintf(stderr, "%s\n", dlerror());
    return 1;
  }
  PFN_remixapi_InitializeLibrary init = (PFN_remixapi_InitializeLibrary) dlsym(lib, "remixapi_InitializeLibrary");
  PFN_remixlinux_SetWindowSize setSize = (PFN_remixlinux_SetWindowSize) dlsym(lib, "remixlinux_SetWindowSize");
  getStats = (PFN_remixlinux_GetApiResourceStats) dlsym(lib, "remixlinux_GetApiResourceStats");
  if (!init || !setSize || !getStats) {
    fprintf(stderr, "missing export\n");
    return 1;
  }

  remixapi_InitializeLibraryInfo initInfo;
  memset(&initInfo, 0, sizeof(initInfo));
  initInfo.sType = REMIXAPI_STRUCT_TYPE_INITIALIZE_LIBRARY_INFO;
  initInfo.version = REMIXAPI_VERSION_MAKE(REMIXAPI_VERSION_MAJOR, REMIXAPI_VERSION_MINOR, REMIXAPI_VERSION_PATCH);
  CHECK("InitializeLibrary", init(&initInfo, &remix), REMIXAPI_ERROR_CODE_SUCCESS);

  /* Before Startup, config layers report that no device is registered. */
  remixapi_ConfigLayerHandle early = NULL;
  CHECK("AcquireConfigLayer before Startup", remix.AcquireConfigLayer("x.conf", 5000.0f, 0.5f, &early),
        REMIXAPI_ERROR_CODE_REMIX_DEVICE_WAS_NOT_REGISTERED);

  setSize(640, 360);
  remixapi_StartupInfo startup;
  memset(&startup, 0, sizeof(startup));
  startup.sType = REMIXAPI_STRUCT_TYPE_STARTUP_INFO;
  CHECK("Startup", remix.Startup(&startup), REMIXAPI_ERROR_CODE_SUCCESS);
  if (failures) {
    return 1;
  }

  /* ---- memory textures ---- */
  enum { W = 64, H = 64, SKY_W = 128, SKY_H = 64, NP_W = 30, NP_H = 17 };
  static uint8_t albedo[W * H * 4], normal[W * H * 4], height[W * H], roughness[W * H];
  static uint16_t sky[SKY_W * SKY_H * 4];
  static uint8_t bgra[NP_W * NP_H * 4];
  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      const int i = y * W + x;
      const int checker = ((x / 8) + (y / 8)) & 1;
      albedo[i * 4 + 0] = checker ? 220 : 40;
      albedo[i * 4 + 1] = 120;
      albedo[i * 4 + 2] = checker ? 40 : 220;
      albedo[i * 4 + 3] = 255;
      normal[i * 4 + 0] = (uint8_t) (128 + 60 * sinf(x * 0.3f));
      normal[i * 4 + 1] = (uint8_t) (128 + 60 * cosf(y * 0.3f));
      normal[i * 4 + 2] = 230;
      normal[i * 4 + 3] = 255;
      height[i] = (uint8_t) ((x * y) & 0xFF);
      roughness[i] = (uint8_t) (x * 4);
    }
  }
  for (int i = 0; i < SKY_W * SKY_H * 4; ++i) {
    sky[i] = 0x3C00; /* 1.0 in half float */
  }
  for (int i = 0; i < NP_W * NP_H * 4; ++i) {
    bgra[i] = (uint8_t) i;
  }

  remixapi_TextureHandle tAlbedo = createTexture("CreateTextureFromMemory RGBA8 COLOR 64x64", 0x1001, albedo, W, H,
      REMIXAPI_TEXTURE_FORMAT_RGBA8_UNORM, REMIXAPI_TEXTURE_PURPOSE_COLOR, sizeof(albedo));
  remixapi_TextureHandle tNormal = createTexture("CreateTextureFromMemory RGBA8 NORMAL_DX", 0x1002, normal, W, H,
      REMIXAPI_TEXTURE_FORMAT_RGBA8_UNORM, REMIXAPI_TEXTURE_PURPOSE_NORMAL_DX, sizeof(normal));
  remixapi_TextureHandle tHeight = createTexture("CreateTextureFromMemory R8 HEIGHTMAP", 0x1003, height, W, H,
      REMIXAPI_TEXTURE_FORMAT_R8_UNORM, REMIXAPI_TEXTURE_PURPOSE_HEIGHTMAP, sizeof(height));
  remixapi_TextureHandle tRough = createTexture("CreateTextureFromMemory R8 DATA", 0x1004, roughness, W, H,
      REMIXAPI_TEXTURE_FORMAT_R8_UNORM, REMIXAPI_TEXTURE_PURPOSE_DATA, sizeof(roughness));
  remixapi_TextureHandle tSky = createTexture("CreateTextureFromMemory RGBA16F 128x64 (sky)", 0x1005, (uint8_t*) sky,
      SKY_W, SKY_H, REMIXAPI_TEXTURE_FORMAT_RGBA16_SFLOAT, REMIXAPI_TEXTURE_PURPOSE_COLOR, sizeof(sky));
  remixapi_TextureHandle tBgra = createTexture("CreateTextureFromMemory BGRA8_SRGB 30x17", 0x1006, bgra, NP_W, NP_H,
      REMIXAPI_TEXTURE_FORMAT_BGRA8_SRGB, REMIXAPI_TEXTURE_PURPOSE_COLOR, sizeof(bgra));
  EXPECT("texture handles are distinct and non-null",
         tAlbedo && tNormal && tHeight && tRough && tSky && tBgra && tAlbedo != tNormal && tSky != tBgra);

  CHECK("CreateTextureFromMemory (null info)", remix.CreateTextureFromMemory(NULL, &tAlbedo), REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS);
  CHECK("CreateTextureFromMemory (width 0)", createBad(0, 16, REMIXAPI_TEXTURE_FORMAT_RGBA8_UNORM, 0, 0), REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS);
  CHECK("CreateTextureFromMemory (null pixels)", createBad(16, 16, REMIXAPI_TEXTURE_FORMAT_RGBA8_UNORM, 0, 1), REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS);
  CHECK("CreateTextureFromMemory (unknown format)", createBad(16, 16, 99, 0, 0), REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS);
  CHECK("CreateTextureFromMemory (unknown purpose)", createBad(16, 16, REMIXAPI_TEXTURE_FORMAT_RGBA8_UNORM, 99, 0), REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS);
  CHECK("CreateTextureFromMemory (NORMAL_DX from R8)", createBad(16, 16, REMIXAPI_TEXTURE_FORMAT_R8_UNORM, REMIXAPI_TEXTURE_PURPOSE_NORMAL_DX, 0), REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS);

  Stats s = stats("stats after creating 6 textures");
  EXPECT("6 textures on the CS side, 6 uploads", s.textures == 6 && s.resolved == 6 && s.uploads == 6);

  /* ---- a material and instance that sample them, and a dome light ---- */
  remixapi_MaterialInfoOpaqueEXT opaque;
  memset(&opaque, 0, sizeof(opaque));
  opaque.sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_EXT;
  opaque.roughnessTexture = tRough;
  opaque.heightTexture = tHeight;
  opaque.albedoConstant.x = opaque.albedoConstant.y = opaque.albedoConstant.z = 1.0f;
  opaque.opacityConstant = 1.0f;
  opaque.roughnessConstant = 0.5f;
  opaque.displaceIn = 0.05f;
  remixapi_MaterialInfo mat;
  memset(&mat, 0, sizeof(mat));
  mat.sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO;
  mat.pNext = &opaque;
  mat.hash = 0x2001;
  mat.albedoTexture = tAlbedo;
  mat.normalTexture = tNormal;
  remixapi_MaterialHandle material = NULL;
  CHECK("CreateMaterial (albedo, normal, height, roughness)", remix.CreateMaterial(&mat, &material), REMIXAPI_ERROR_CODE_SUCCESS);

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
  meshInfo.hash = 0x2002;
  meshInfo.surfaces_values = &surf;
  meshInfo.surfaces_count = 1;
  remixapi_MeshHandle mesh = NULL;
  CHECK("CreateMesh", remix.CreateMesh(&meshInfo, &mesh), REMIXAPI_ERROR_CODE_SUCCESS);

  remixapi_InstanceInfo inst;
  memset(&inst, 0, sizeof(inst));
  inst.sType = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO;
  inst.mesh = mesh;
  inst.transform.matrix[0][0] = inst.transform.matrix[1][1] = inst.transform.matrix[2][2] = 1.0f;
  inst.transform.matrix[2][3] = 3.0f;
  inst.doubleSided = 1;
  remixapi_SceneInstanceHandle instance = NULL;
  CHECK("SceneInstanceAdd", remix.SceneInstanceAdd(&inst, &instance), REMIXAPI_ERROR_CODE_SUCCESS);

  remixapi_LightInfoDomeEXT dome;
  memset(&dome, 0, sizeof(dome));
  dome.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DOME_EXT;
  dome.transform.matrix[0][0] = dome.transform.matrix[1][1] = dome.transform.matrix[2][2] = 1.0f;
  dome.colorTexture = tSky;
  dome.opacity = 1.0f;
  remixapi_LightInfo light;
  memset(&light, 0, sizeof(light));
  light.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO;
  light.pNext = &dome;
  light.hash = 0x3001;
  light.radiance.x = light.radiance.y = light.radiance.z = 1.0f;
  remixapi_LightHandle domeLight = NULL;
  CHECK("CreateLight (dome, sky texture)", remix.CreateLight(&light, &domeLight), REMIXAPI_ERROR_CODE_SUCCESS);

  frames(3);
  printf("%-52s    done\n", "3 frames with textured instance and dome light");

  /* ---- UpdateTextureFromMemory ---- */
  static uint8_t patchA[8 * 8 * 4];
  static uint8_t patchB[20 * 4 * 4]; /* 4x4 texels with a 20-texel row pitch */
  static uint16_t skyPatch[SKY_W * SKY_H * 4];
  memset(patchA, 0xFF, sizeof(patchA));
  memset(patchB, 0x80, sizeof(patchB));
  for (int i = 0; i < SKY_W * SKY_H * 4; ++i) {
    skyPatch[i] = 0x4000; /* 2.0 */
  }
  remixapi_TexturePatch albedoPatches[2];
  memset(albedoPatches, 0, sizeof(albedoPatches));
  albedoPatches[0] = (remixapi_TexturePatch) { 0, 0, 8, 8, patchA, 8 * 4, REMIXAPI_TEXTURE_FORMAT_RGBA8_UNORM };
  albedoPatches[1] = (remixapi_TexturePatch) { 60, 60, 4, 4, patchB, 20 * 4, REMIXAPI_TEXTURE_FORMAT_RGBA8_UNORM };
  remixapi_TexturePatch skyPatches[1] = { { 0, 0, SKY_W, SKY_H, (const uint8_t*) skyPatch, SKY_W * 8, REMIXAPI_TEXTURE_FORMAT_RGBA16_SFLOAT } };
  remixapi_TextureBatchEntry entries[2] = { { tAlbedo, 2, albedoPatches }, { tSky, 1, skyPatches } };
  remixapi_TexturePatchInfo patchInfo;
  memset(&patchInfo, 0, sizeof(patchInfo));
  patchInfo.sType = REMIXAPI_STRUCT_TYPE_TEXTURE_PATCH_INFO;
  patchInfo.entryCount = 2;
  patchInfo.entries = entries;
  CHECK("UpdateTextureFromMemory (2 entries, 3 patches)", remix.UpdateTextureFromMemory(&patchInfo), REMIXAPI_ERROR_CODE_SUCCESS);
  memset(patchA, 0, sizeof(patchA)); /* Copied during the call. */
  frames(2);
  s = stats("stats after the batch update");
  EXPECT("batch update made 2 uploads (one per entry)", s.uploads == 8);

  /* Normal-map patch goes through the same octahedral conversion. */
  static uint8_t normalPatch[16 * 16 * 4];
  memset(normalPatch, 128, sizeof(normalPatch));
  remixapi_TexturePatch np = { 16, 16, 16, 16, normalPatch, 0, REMIXAPI_TEXTURE_FORMAT_RGBA8_UNORM };
  remixapi_TextureBatchEntry ne = { tNormal, 1, &np };
  patchInfo.entryCount = 1;
  patchInfo.entries = &ne;
  CHECK("UpdateTextureFromMemory (normal map, rowPitch 0)", remix.UpdateTextureFromMemory(&patchInfo), REMIXAPI_ERROR_CODE_SUCCESS);

  /* A bad entry rejects the whole batch. */
  remixapi_TexturePatch outOfBounds = { 60, 60, 8, 8, patchA, 0, REMIXAPI_TEXTURE_FORMAT_RGBA8_UNORM };
  remixapi_TexturePatch wrongFormat = { 0, 0, 8, 8, patchA, 0, REMIXAPI_TEXTURE_FORMAT_BGRA8_UNORM };
  remixapi_TexturePatch shortPitch = { 0, 0, 8, 8, patchA, 16, REMIXAPI_TEXTURE_FORMAT_RGBA8_UNORM };
  remixapi_TextureBatchEntry badEntries[2] = { { tSky, 1, skyPatches }, { tAlbedo, 1, &outOfBounds } };
  patchInfo.entryCount = 2;
  patchInfo.entries = badEntries;
  CHECK("UpdateTextureFromMemory (2nd entry out of bounds)", remix.UpdateTextureFromMemory(&patchInfo), REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS);
  badEntries[1].patches = &wrongFormat;
  CHECK("UpdateTextureFromMemory (format mismatch)", remix.UpdateTextureFromMemory(&patchInfo), REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS);
  badEntries[1].patches = &shortPitch;
  CHECK("UpdateTextureFromMemory (rowPitch too small)", remix.UpdateTextureFromMemory(&patchInfo), REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS);
  badEntries[1].handle = (remixapi_TextureHandle) (uintptr_t) 0xDEAD;
  badEntries[1].patches = albedoPatches;
  CHECK("UpdateTextureFromMemory (unknown handle)", remix.UpdateTextureFromMemory(&patchInfo), REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS);
  patchInfo.entryCount = 0;
  CHECK("UpdateTextureFromMemory (no entries)", remix.UpdateTextureFromMemory(&patchInfo), REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS);
  s = stats("stats after rejected batches");
  EXPECT("rejected batches made no uploads (only the normal patch)", s.uploads == 9);

  /* ---- file texture ---- */
  char ddsPath[1024];
  snprintf(ddsPath, sizeof(ddsPath), "%s/remix_test_texture.dds", scratch);
  EXPECT("wrote test DDS", writeDds(ddsPath, 32, 32));
  wchar_t wpath[1024];
  mbstowcs(wpath, ddsPath, 1024);
  remixapi_TextureHandle tFile = NULL;
  CHECK("CreateTextureFromFile (32x32 BGRA DDS)", remix.CreateTextureFromFile(wpath, &tFile), REMIXAPI_ERROR_CODE_SUCCESS);
  remixapi_TextureHandle tMissing = NULL;
  CHECK("CreateTextureFromFile (missing file)", remix.CreateTextureFromFile(L"/nonexistent/missing.dds", &tMissing), REMIXAPI_ERROR_CODE_GENERAL_FAILURE);
  CHECK("CreateTextureFromFile (null path)", remix.CreateTextureFromFile(NULL, &tMissing), REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS);
  patchInfo.entryCount = 1;
  remixapi_TextureBatchEntry fileEntry = { tFile, 1, albedoPatches };
  patchInfo.entries = &fileEntry;
  CHECK("UpdateTextureFromMemory (file texture)", remix.UpdateTextureFromMemory(&patchInfo), REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS);
  s = stats("stats after file texture");
  EXPECT("file texture resolved (7 textures, 7 resolved)", s.textures == 7 && s.resolved == 7);

  /* ---- DestroyTexture while the material still uses it ---- */
  CHECK("DestroyTexture (albedo, still used by material)", remix.DestroyTexture(tAlbedo), REMIXAPI_ERROR_CODE_SUCCESS);
  frames(2);
  printf("%-52s    done\n", "2 frames after destroying a used texture");
  CHECK("DestroyTexture (again)", remix.DestroyTexture(tAlbedo), REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS);
  CHECK("DestroyTexture (null)", remix.DestroyTexture(NULL), REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS);
  patchInfo.entries = entries; /* entries[0] is the destroyed albedo */
  CHECK("UpdateTextureFromMemory (destroyed handle)", remix.UpdateTextureFromMemory(&patchInfo), REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS);
  CHECK("DestroyTexture (file texture)", remix.DestroyTexture(tFile), REMIXAPI_ERROR_CODE_SUCCESS);
  s = stats("stats after destroying 2 textures");
  EXPECT("5 textures left", s.textures == 5);

  /* ---- config layers ---- */
  char confPath[1024];
  snprintf(confPath, sizeof(confPath), "%s/remix_test_layer.conf", scratch);
  FILE* conf = fopen(confPath, "w");
  if (!conf) {
    printf("cannot write %s  FAIL\n", confPath);
    return 1;
  }
  fprintf(conf, "%s = 200\n%s = True\n", kFloatKey, kBoolKey);
  fclose(conf);

  const float f0 = readConfigFloat(kFloatKey);
  const int b0 = readConfigBool(kBoolKey);
  printf("%-52s    %s=%g %s=%d\n", "defaults", kFloatKey, f0, kBoolKey, b0);
  EXPECT("defaults read back (1000, False)", fabsf(f0 - 1000.0f) < 0.01f && b0 == 0);

  remixapi_ConfigLayerHandle layer = NULL, layer2 = NULL, missing = NULL;
  CHECK("AcquireConfigLayer", remix.AcquireConfigLayer(confPath, 5000.0f, 0.5f, &layer), REMIXAPI_ERROR_CODE_SUCCESS);
  CHECK("AcquireConfigLayer (same path and priority)", remix.AcquireConfigLayer(confPath, 5000.0f, 0.5f, &layer2), REMIXAPI_ERROR_CODE_SUCCESS);
  EXPECT("same handle for the cached layer", layer != NULL && layer == layer2);
  CHECK("AcquireConfigLayer (missing file)", remix.AcquireConfigLayer("/nonexistent/x.conf", 5000.0f, 0.5f, &missing), REMIXAPI_ERROR_CODE_GENERAL_FAILURE);
  CHECK("AcquireConfigLayer (NaN priority)", remix.AcquireConfigLayer(confPath, NAN, 0.5f, &missing), REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS);
  CHECK("SetConfigLayerBlending (unknown handle)", remix.SetConfigLayerBlending((remixapi_ConfigLayerHandle) (uintptr_t) 0xBEEF, 1.0f, 0.5f), REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS);

  frames(2);
  float f = readConfigFloat(kFloatKey);
  int b = readConfigBool(kBoolKey);
  printf("%-52s    float=%g bool=%d\n", "new layer (disabled, strength 0)", f, b);
  EXPECT("new layer has no effect", fabsf(f - 1000.0f) < 0.01f && b == 0);

  CHECK("SetConfigLayerBlending (strength 1)", remix.SetConfigLayerBlending(layer, 1.0f, 0.5f), REMIXAPI_ERROR_CODE_SUCCESS);
  frames(2);
  f = readConfigFloat(kFloatKey);
  b = readConfigBool(kBoolKey);
  printf("%-52s    float=%g bool=%d\n", "strength 1", f, b);
  EXPECT("strength 1 applies the layer (200, True)", fabsf(f - 200.0f) < 0.01f && b == 1);

  CHECK("SetConfigLayerBlending (strength 0.25)", remix.SetConfigLayerBlending(layer, 0.25f, 0.5f), REMIXAPI_ERROR_CODE_SUCCESS);
  frames(2);
  f = readConfigFloat(kFloatKey);
  b = readConfigBool(kBoolKey);
  printf("%-52s    float=%g bool=%d\n", "strength 0.25, threshold 0.5", f, b);
  EXPECT("float blends to 800, bool below threshold stays False", fabsf(f - 800.0f) < 0.5f && b == 0);

  /* Two requests in one frame: max strength and min threshold win. */
  CHECK("SetConfigLayerBlending (0.25, 0.9)", remix.SetConfigLayerBlending(layer, 0.25f, 0.9f), REMIXAPI_ERROR_CODE_SUCCESS);
  CHECK("SetConfigLayerBlending (0.5, 0.1) same frame", remix.SetConfigLayerBlending(layer, 0.5f, 0.1f), REMIXAPI_ERROR_CODE_SUCCESS);
  frames(2);
  f = readConfigFloat(kFloatKey);
  b = readConfigBool(kBoolKey);
  printf("%-52s    float=%g bool=%d\n", "two requests -> strength 0.5, threshold 0.1", f, b);
  EXPECT("float 600, bool True", fabsf(f - 600.0f) < 0.5f && b == 1);

  s = stats("stats with one layer acquired twice");
  EXPECT("one runtime layer", s.layers == 1);
  CHECK("ReleaseConfigLayer (1 of 2)", remix.ReleaseConfigLayer(layer), REMIXAPI_ERROR_CODE_SUCCESS);
  frames(1);
  EXPECT("layer still applied after first release", fabsf(readConfigFloat(kFloatKey) - 600.0f) < 0.5f);
  CHECK("ReleaseConfigLayer (2 of 2)", remix.ReleaseConfigLayer(layer), REMIXAPI_ERROR_CODE_SUCCESS);
  frames(2);
  f = readConfigFloat(kFloatKey);
  b = readConfigBool(kBoolKey);
  printf("%-52s    float=%g bool=%d\n", "after final release", f, b);
  EXPECT("defaults restored (1000, False)", fabsf(f - 1000.0f) < 0.01f && b == 0);
  CHECK("ReleaseConfigLayer (already released)", remix.ReleaseConfigLayer(layer), REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS);
  s = stats("stats after release");
  EXPECT("no runtime layers", s.layers == 0);

  /* A layer and textures left alive at Shutdown are released by Shutdown. */
  CHECK("AcquireConfigLayer (kept until Shutdown)", remix.AcquireConfigLayer(confPath, 7000.0f, 0.5f, &layer), REMIXAPI_ERROR_CODE_SUCCESS);
  frames(1);

  CHECK("Shutdown", remix.Shutdown(), REMIXAPI_ERROR_CODE_SUCCESS);
  remove(ddsPath);
  remove(confPath);
  printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
