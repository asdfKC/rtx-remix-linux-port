/* FD interop smoke test: Startup(interopImage=1) -> dxvk_GetInteropInfo -> import semaphores and the
 * color image into OpenGL via EXT_memory_object_fd / EXT_semaphore_fd -> build a small lit scene with
 * the classic immediate-mode API (CreateMaterial/CreateMesh/CreateLight/SetupCamera/DrawInstance) ->
 * Present -> glWaitSemaphoreEXT -> read the imported texture back and dump it as a PPM.
 *
 * Needs an NVIDIA GPU with VK_KHR_external_memory_fd / VK_KHR_external_semaphore_fd (the Linux
 * runtime already requires these) and EGL_EXT_platform_device to pick that GPU headlessly, without
 * depending on the window system (Wayland/Hyprland in this case).
 *
 * Build: see build_interop_gl.sh next to this file.
 * Run:   RTX_ENABLE_ASYNC_COMPILATION=0 ./interop_gl <path to libremix.so> <out.ppm>
 *
 * RTX_ENABLE_ASYNC_COMPILATION=0 matters: this test's material/mesh/light/camera combination is
 * compiling its shader pipelines for the first time ever, and by default (rtx.shader.
 * enableAsyncCompilation=true) a frame whose pipeline isn't ready yet is silently skipped rather
 * than blocked on -- across this test's short, back-to-back Present() loop, that can mean every
 * single frame comes back black. Setting it to 0 makes the first frame that needs a new pipeline
 * block until it's ready instead. See PORTING_NOTES.md Phase D step 5 for the full story.
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>

#include <remix/remix_c.h>

typedef void (*PFN_remixlinux_SetWindowSize)(uint32_t, uint32_t);

/* ---- GL function pointers (EXT_memory_object / EXT_memory_object_fd / EXT_semaphore / EXT_semaphore_fd) ---- */
static void (*glCreateMemoryObjectsEXT_)(GLsizei, GLuint*);
static void (*glMemoryObjectParameterivEXT_)(GLuint, GLenum, const GLint*);
static void (*glImportMemoryFdEXT_)(GLuint, GLuint64, GLenum, GLint);
static void (*glTexStorageMem2DEXT_)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLuint, GLuint64);
static void (*glGenSemaphoresEXT_)(GLsizei, GLuint*);
static void (*glImportSemaphoreFdEXT_)(GLuint, GLenum, GLint);
static void (*glWaitSemaphoreEXT_)(GLuint, GLuint, const GLuint*, GLuint, const GLuint*, const GLenum*);
static void (*glSignalSemaphoreEXT_)(GLuint, GLuint, const GLuint*, GLuint, const GLuint*, const GLenum*);
static void (*glGetUnsignedBytevEXT_)(GLenum, GLubyte*);
static void (*glGetUnsignedBytei_vEXT_)(GLenum, GLuint, GLubyte*);

/* GL/gl.h on this system only declares legacy GL 1.x; FBOs are core since GL 3.0, so load them
 * as procs too rather than depending on a newer system GL header. */
static void (*glGenFramebuffers_)(GLsizei, GLuint*);
static void (*glBindFramebuffer_)(GLenum, GLuint);
static void (*glFramebufferTexture2D_)(GLenum, GLenum, GLenum, GLuint, GLint);
static GLenum (*glCheckFramebufferStatus_)(GLenum);

#define LOAD(name) do { name##_ = (void*)eglGetProcAddress(#name); if (!name##_) { fprintf(stderr, "missing GL proc: %s\n", #name); return 1; } } while (0)

static int loadGlExtProcs(void) {
  LOAD(glCreateMemoryObjectsEXT);
  LOAD(glMemoryObjectParameterivEXT);
  LOAD(glImportMemoryFdEXT);
  LOAD(glTexStorageMem2DEXT);
  LOAD(glGenSemaphoresEXT);
  LOAD(glImportSemaphoreFdEXT);
  LOAD(glWaitSemaphoreEXT);
  LOAD(glSignalSemaphoreEXT);
  LOAD(glGetUnsignedBytevEXT);
  LOAD(glGetUnsignedBytei_vEXT);
  LOAD(glGenFramebuffers);
  LOAD(glBindFramebuffer);
  LOAD(glFramebufferTexture2D);
  LOAD(glCheckFramebufferStatus);
  return 0;
}
#undef LOAD

/* ---- EGL: pick the NVIDIA device explicitly, independent of the Wayland/Hyprland compositor ---- */
static EGLDisplay createNvidiaEglDisplay(void) {
  PFNEGLQUERYDEVICESEXTPROC eglQueryDevicesEXT =
    (PFNEGLQUERYDEVICESEXTPROC) eglGetProcAddress("eglQueryDevicesEXT");
  PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT =
    (PFNEGLGETPLATFORMDISPLAYEXTPROC) eglGetProcAddress("eglGetPlatformDisplayEXT");
  PFNEGLQUERYDEVICESTRINGEXTPROC eglQueryDeviceStringEXT =
    (PFNEGLQUERYDEVICESTRINGEXTPROC) eglGetProcAddress("eglQueryDeviceStringEXT");
  if (!eglQueryDevicesEXT || !eglGetPlatformDisplayEXT) {
    fprintf(stderr, "EGL_EXT_device_base / EGL_EXT_platform_device not available\n");
    return EGL_NO_DISPLAY;
  }

  EGLDeviceEXT devices[16];
  EGLint numDevices = 0;
  if (!eglQueryDevicesEXT(16, devices, &numDevices) || numDevices == 0) {
    fprintf(stderr, "eglQueryDevicesEXT found no devices\n");
    return EGL_NO_DISPLAY;
  }

  for (EGLint i = 0; i < numDevices; ++i) {
    const char* vendor = eglQueryDeviceStringEXT ? eglQueryDeviceStringEXT(devices[i], EGL_EXTENSIONS) : NULL;
    /* NVIDIA's device extension string includes EGL_NV_device_cuda; that's a reliable discriminator
     * from the AMD iGPU on this box, which won't advertise it. */
    if (vendor && strstr(vendor, "EGL_NV_device_cuda")) {
      EGLDisplay dpy = eglGetPlatformDisplayEXT(EGL_PLATFORM_DEVICE_EXT, devices[i], NULL);
      if (dpy != EGL_NO_DISPLAY) {
        fprintf(stderr, "EGL device %d selected (EGL_NV_device_cuda present)\n", i);
        return dpy;
      }
    }
  }

  fprintf(stderr, "no EGL device advertised EGL_NV_device_cuda; falling back to device 0\n");
  return eglGetPlatformDisplayEXT(EGL_PLATFORM_DEVICE_EXT, devices[0], NULL);
}

static void writePpm(const char* path, int w, int h, const unsigned char* rgba) {
  FILE* f = fopen(path, "wb");
  if (!f) {
    fprintf(stderr, "failed to open %s for writing\n", path);
    return;
  }
  fprintf(f, "P6\n%d %d\n255\n", w, h);
  for (int y = h - 1; y >= 0; --y) { /* GL reads bottom-up; flip to a conventional top-down PPM */
    for (int x = 0; x < w; ++x) {
      const unsigned char* px = &rgba[(y * w + x) * 4];
      fputc(px[0], f);
      fputc(px[1], f);
      fputc(px[2], f);
    }
  }
  fclose(f);
}

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s <libremix.so> <out.ppm>\n", argv[0]);
    return 2;
  }
  const char* libPath = argv[1];
  const char* outPath = argv[2];
  const uint32_t W = 640, H = 360;

  /* ---- EGL context on the NVIDIA GPU, surfaceless ---- */
  EGLDisplay dpy = createNvidiaEglDisplay();
  if (dpy == EGL_NO_DISPLAY) {
    fprintf(stderr, "no EGL display\n");
    return 1;
  }
  EGLint major, minor;
  if (!eglInitialize(dpy, &major, &minor)) {
    fprintf(stderr, "eglInitialize failed: 0x%x\n", eglGetError());
    return 1;
  }
  fprintf(stderr, "EGL %d.%d, vendor: %s\n", major, minor, eglQueryString(dpy, EGL_VENDOR));

  if (!eglBindAPI(EGL_OPENGL_API)) {
    fprintf(stderr, "eglBindAPI(OPENGL) failed\n");
    return 1;
  }

  EGLint configAttribs[] = {
    EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
    EGL_NONE
  };
  EGLConfig config;
  EGLint numConfigs;
  if (!eglChooseConfig(dpy, configAttribs, &config, 1, &numConfigs) || numConfigs == 0) {
    fprintf(stderr, "eglChooseConfig failed\n");
    return 1;
  }

  EGLint pbufAttribs[] = { EGL_WIDTH, (EGLint) W, EGL_HEIGHT, (EGLint) H, EGL_NONE };
  EGLSurface surf = eglCreatePbufferSurface(dpy, config, pbufAttribs);
  if (surf == EGL_NO_SURFACE) {
    fprintf(stderr, "eglCreatePbufferSurface failed: 0x%x\n", eglGetError());
    return 1;
  }

  EGLint ctxAttribs[] = { EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 5, EGL_NONE };
  EGLContext ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, ctxAttribs);
  if (ctx == EGL_NO_CONTEXT) {
    fprintf(stderr, "eglCreateContext failed: 0x%x\n", eglGetError());
    return 1;
  }
  if (!eglMakeCurrent(dpy, surf, surf, ctx)) {
    fprintf(stderr, "eglMakeCurrent failed: 0x%x\n", eglGetError());
    return 1;
  }

  fprintf(stderr, "GL_VENDOR:   %s\n", (const char*) glGetString(GL_VENDOR));
  fprintf(stderr, "GL_RENDERER: %s\n", (const char*) glGetString(GL_RENDERER));
  fprintf(stderr, "GL_VERSION:  %s\n", (const char*) glGetString(GL_VERSION));

  if (strstr((const char*) glGetString(GL_VENDOR), "NVIDIA") == NULL) {
    fprintf(stderr, "FATAL: GL context did not land on NVIDIA (got the iGPU?) -- aborting\n");
    return 1;
  }

  /* This is a core-profile GL 4.5 context: glGetString(GL_EXTENSIONS) is deprecated/empty there.
   * Use the GL 3.0+ indexed query instead. */
  const char* (*glGetStringi_)(GLenum, GLuint) = (const char*(*)(GLenum, GLuint)) eglGetProcAddress("glGetStringi");
  if (!glGetStringi_) {
    fprintf(stderr, "FATAL: glGetStringi not available\n");
    return 1;
  }
  GLint numExts = 0;
  glGetIntegerv(GL_NUM_EXTENSIONS, &numExts);
  int hasMemObjFd = 0, hasSemaFd = 0;
  for (GLint i = 0; i < numExts; ++i) {
    const char* e = glGetStringi_(GL_EXTENSIONS, (GLuint) i);
    if (!e) continue;
    if (!strcmp(e, "GL_EXT_memory_object_fd")) hasMemObjFd = 1;
    if (!strcmp(e, "GL_EXT_semaphore_fd")) hasSemaFd = 1;
  }
  if (!hasMemObjFd || !hasSemaFd) {
    fprintf(stderr, "FATAL: GL_EXT_memory_object_fd(%d) / GL_EXT_semaphore_fd(%d) not advertised\n", hasMemObjFd, hasSemaFd);
    return 1;
  }
  if (loadGlExtProcs() != 0) {
    return 1;
  }

  /* ---- GL device UUID, to cross check against the Vulkan device Remix actually renders on ---- */
  PFNGLGETUNSIGNEDBYTEI_VEXTPROC glGetUnsignedBytei_vEXT = glGetUnsignedBytei_vEXT_;
  GLint numDeviceUuids = 0;
  glGetIntegerv(GL_NUM_DEVICE_UUIDS_EXT, &numDeviceUuids);
  unsigned char glDeviceUuid[16] = {0};
  if (numDeviceUuids > 0) {
    glGetUnsignedBytei_vEXT(GL_DEVICE_UUID_EXT, 0, glDeviceUuid);
  }
  fprintf(stderr, "GL device UUID: ");
  for (int i = 0; i < 16; ++i) fprintf(stderr, "%02x", glDeviceUuid[i]);
  fprintf(stderr, "\n");

  /* ---- Load the runtime ---- */
  void* lib = dlopen(libPath, RTLD_NOW | RTLD_LOCAL);
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

  remixapi_InitializeLibraryInfo ilInfo;
  memset(&ilInfo, 0, sizeof(ilInfo));
  ilInfo.sType = REMIXAPI_STRUCT_TYPE_INITIALIZE_LIBRARY_INFO;
  ilInfo.version = REMIXAPI_VERSION_MAKE(REMIXAPI_VERSION_MAJOR, REMIXAPI_VERSION_MINOR, REMIXAPI_VERSION_PATCH);
  remixapi_Interface remix;
  memset(&remix, 0, sizeof(remix));
  if (init(&ilInfo, &remix) != REMIXAPI_ERROR_CODE_SUCCESS) {
    fprintf(stderr, "InitializeLibrary failed\n");
    return 1;
  }

  setSize(W, H);

  remixapi_StartupInfo startupInfo;
  memset(&startupInfo, 0, sizeof(startupInfo));
  startupInfo.sType = REMIXAPI_STRUCT_TYPE_STARTUP_INFO;
  startupInfo.interopImage = 1;
  remixapi_ErrorCode rc = remix.Startup(&startupInfo);
  printf("Startup -> 0x%x\n", (unsigned) rc);
  if (rc != REMIXAPI_ERROR_CODE_SUCCESS) {
    return 1;
  }

  /* ---- Step 1: import the two semaphores right after Startup ---- */
  remixapi_dxvk_InteropInfo interop;
  memset(&interop, 0, sizeof(interop));
  rc = remix.dxvk_GetInteropInfo(&interop);
  printf("dxvk_GetInteropInfo (semaphores) -> 0x%x\n", (unsigned) rc);
  if (rc != REMIXAPI_ERROR_CODE_SUCCESS) {
    return 1;
  }
  if (interop.dxvkDoneSemaphore_Handle == 0 || interop.clientDoneSemaphore_Handle == 0) {
    fprintf(stderr, "FATAL: no semaphore fds returned\n");
    return 1;
  }

  GLuint glSemas[2];
  glGenSemaphoresEXT_(2, glSemas);
  glImportSemaphoreFdEXT_(glSemas[0], GL_HANDLE_TYPE_OPAQUE_FD_EXT, (GLint) interop.dxvkDoneSemaphore_Handle);
  glImportSemaphoreFdEXT_(glSemas[1], GL_HANDLE_TYPE_OPAQUE_FD_EXT, (GLint) interop.clientDoneSemaphore_Handle);
  GLuint glDxvkDoneSema = glSemas[0];
  GLuint glClientDoneSema = glSemas[1];

  /* ---- Build a small lit scene with the classic immediate-mode API ---- */
  remixapi_MaterialInfoOpaqueEXT matOpaque;
  memset(&matOpaque, 0, sizeof(matOpaque));
  matOpaque.sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_EXT;
  matOpaque.albedoConstant = (remixapi_Float3D) { 0.8f, 0.8f, 0.8f };
  matOpaque.opacityConstant = 1.0f;
  matOpaque.roughnessConstant = 0.7f;
  matOpaque.metallicConstant = 0.0f;
  matOpaque.alphaTestType = 7; /* D3DCMP_ALWAYS-ish passthrough; matched to other callers' convention */

  remixapi_MaterialInfo matInfo;
  memset(&matInfo, 0, sizeof(matInfo));
  matInfo.sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO;
  matInfo.pNext = &matOpaque;
  matInfo.hash = 0xC0FFEE01u;
  matInfo.filterMode = 1;
  matInfo.wrapModeU = 0;
  matInfo.wrapModeV = 0;
  /* Self-lit, so the quad is visible even if external-light sampling (NEE/ReSTIR) needs more
   * warmup or has gaps -- isolates "is the geometry/camera there at all" from "is lighting working". */
  matInfo.emissiveIntensity = 20.0f;
  matInfo.emissiveColorConstant = (remixapi_Float3D) { 1.0f, 1.0f, 1.0f };

  remixapi_MaterialHandle matHandle = NULL;
  rc = remix.CreateMaterial(&matInfo, &matHandle);
  printf("CreateMaterial -> 0x%x\n", (unsigned) rc);
  if (rc != REMIXAPI_ERROR_CODE_SUCCESS) {
    return 1;
  }

  /* A quad facing the camera (camera looks down +Z, quad normal is -Z) */
  remixapi_HardcodedVertex verts[4];
  memset(verts, 0, sizeof(verts));
  const float qs = 1.5f;
  const float positions[4][2] = { { -qs, -qs }, { qs, -qs }, { qs, qs }, { -qs, qs } };
  const float uvs[4][2] = { { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 } };
  for (int i = 0; i < 4; ++i) {
    verts[i].position[0] = positions[i][0];
    verts[i].position[1] = positions[i][1];
    verts[i].position[2] = 0.0f;
    verts[i].normal[0] = 0.0f;
    verts[i].normal[1] = 0.0f;
    verts[i].normal[2] = -1.0f;
    verts[i].texcoord[0] = uvs[i][0];
    verts[i].texcoord[1] = uvs[i][1];
    verts[i].color = 0xFFFFFFFFu;
  }
  uint32_t indices[6] = { 0, 1, 2, 0, 2, 3 };

  remixapi_MeshInfoSurfaceTriangles meshSurf;
  memset(&meshSurf, 0, sizeof(meshSurf));
  meshSurf.vertices_values = verts;
  meshSurf.vertices_count = 4;
  meshSurf.indices_values = indices;
  meshSurf.indices_count = 6;
  meshSurf.material = matHandle;

  remixapi_MeshInfo meshInfo;
  memset(&meshInfo, 0, sizeof(meshInfo));
  meshInfo.sType = REMIXAPI_STRUCT_TYPE_MESH_INFO;
  meshInfo.hash = 0xC0FFEE02u;
  meshInfo.surfaces_values = &meshSurf;
  meshInfo.surfaces_count = 1;

  remixapi_MeshHandle meshHandle = NULL;
  rc = remix.CreateMesh(&meshInfo, &meshHandle);
  printf("CreateMesh -> 0x%x\n", (unsigned) rc);
  if (rc != REMIXAPI_ERROR_CODE_SUCCESS) {
    return 1;
  }

  remixapi_LightInfoSphereEXT sphereLight;
  memset(&sphereLight, 0, sizeof(sphereLight));
  sphereLight.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT;
  sphereLight.position = (remixapi_Float3D) { 1.0f, 1.0f, -2.0f };
  sphereLight.radius = 0.2f;
  sphereLight.volumetricRadianceScale = 1.0f;

  remixapi_LightInfo lightInfo;
  memset(&lightInfo, 0, sizeof(lightInfo));
  lightInfo.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO;
  lightInfo.pNext = &sphereLight;
  lightInfo.hash = 0xC0FFEE03u;
  lightInfo.radiance = (remixapi_Float3D) { 400.0f, 400.0f, 400.0f };

  remixapi_LightHandle lightHandle = NULL;
  rc = remix.CreateLight(&lightInfo, &lightHandle);
  printf("CreateLight -> 0x%x\n", (unsigned) rc);
  if (rc != REMIXAPI_ERROR_CODE_SUCCESS) {
    return 1;
  }

  remixapi_CameraInfoParameterizedEXT camParams;
  memset(&camParams, 0, sizeof(camParams));
  camParams.sType = REMIXAPI_STRUCT_TYPE_CAMERA_INFO_PARAMETERIZED_EXT;
  camParams.position = (remixapi_Float3D) { 0.0f, 0.0f, -4.0f };
  camParams.forward = (remixapi_Float3D) { 0.0f, 0.0f, 1.0f };
  camParams.up = (remixapi_Float3D) { 0.0f, 1.0f, 0.0f };
  camParams.right = (remixapi_Float3D) { 1.0f, 0.0f, 0.0f };
  camParams.fovYInDegrees = 50.0f;
  camParams.aspect = (float) W / (float) H;
  camParams.nearPlane = 0.1f;
  camParams.farPlane = 100.0f;

  remixapi_CameraInfo camInfo;
  memset(&camInfo, 0, sizeof(camInfo));
  camInfo.sType = REMIXAPI_STRUCT_TYPE_CAMERA_INFO;
  camInfo.pNext = &camParams;
  camInfo.type = REMIXAPI_CAMERA_TYPE_WORLD;

  remixapi_InstanceInfo instInfo;
  memset(&instInfo, 0, sizeof(instInfo));
  instInfo.sType = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO;
  instInfo.mesh = meshHandle;
  instInfo.transform.matrix[0][0] = 1.0f;
  instInfo.transform.matrix[1][1] = 1.0f;
  instInfo.transform.matrix[2][2] = 1.0f;
  instInfo.doubleSided = 1; /* avoid any winding/handedness mismatch hiding the quad */

  /* ---- Render several frames: draw calls -> SetupCamera -> Present (which blits into the interop
   *       image and signals dxvkDoneSemaphore on the same submission). The immediate-mode draw
   *       calls are per-frame (unlike the retained SceneInstance* API), and the path tracer's
   *       temporal history/denoiser needs a handful of frames before the image is not black. ---- */
  const int kWarmupFrames = 16;
  remixapi_PresentInfo presentInfo;
  memset(&presentInfo, 0, sizeof(presentInfo));
  presentInfo.sType = REMIXAPI_STRUCT_TYPE_PRESENT_INFO;
  presentInfo.skipVulkanSwapchain = 1;
  presentInfo.interopFlipY = 0;

  for (int frame = 0; frame < kWarmupFrames; ++frame) {
    rc = remix.SetupCamera(&camInfo);
    if (rc != REMIXAPI_ERROR_CODE_SUCCESS) {
      fprintf(stderr, "SetupCamera[%d] -> 0x%x\n", frame, (unsigned) rc);
      return 1;
    }
    rc = remix.DrawInstance(&instInfo);
    if (rc != REMIXAPI_ERROR_CODE_SUCCESS) {
      fprintf(stderr, "DrawInstance[%d] -> 0x%x\n", frame, (unsigned) rc);
      return 1;
    }
    rc = remix.DrawLightInstance(lightHandle);
    if (rc != REMIXAPI_ERROR_CODE_SUCCESS) {
      fprintf(stderr, "DrawLightInstance[%d] -> 0x%x\n", frame, (unsigned) rc);
      return 1;
    }
    rc = remix.Present(&presentInfo);
    printf("Present[%d] -> 0x%x\n", frame, (unsigned) rc);
    if (rc != REMIXAPI_ERROR_CODE_SUCCESS) {
      return 1;
    }
  }

  /* ---- Step 2: get the color image now that a frame has been rendered ---- */
  memset(&interop, 0, sizeof(interop));
  rc = remix.dxvk_GetInteropInfo(&interop);
  printf("dxvk_GetInteropInfo (image) -> 0x%x, %dx%d, allocSize=%llu\n",
    (unsigned) rc, interop.width, interop.height, (unsigned long long) interop.allocationSize);
  if (rc != REMIXAPI_ERROR_CODE_SUCCESS || interop.imageMemory_Handle == 0) {
    fprintf(stderr, "FATAL: no interop image available after Present\n");
    return 1;
  }
  if (interop.interopImageFormat != REMIXAPI_TEXTURE_FORMAT_RGBA8_UNORM) {
    fprintf(stderr, "FATAL: unexpected interop image format %d\n", (int) interop.interopImageFormat);
    return 1;
  }

  GLuint memObj;
  glCreateMemoryObjectsEXT_(1, &memObj);
  GLint dedicated = GL_TRUE;
  glMemoryObjectParameterivEXT_(memObj, GL_DEDICATED_MEMORY_OBJECT_EXT, &dedicated);
  glImportMemoryFdEXT_(memObj, interop.allocationSize, GL_HANDLE_TYPE_OPAQUE_FD_EXT, (GLint) interop.imageMemory_Handle);

  GLuint tex;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_TILING_EXT, GL_OPTIMAL_TILING_EXT);
  glTexStorageMem2DEXT_(GL_TEXTURE_2D, 1, GL_RGBA8, interop.width, interop.height, memObj, 0);
  GLenum glErr = glGetError();
  printf("glTexStorageMem2DEXT -> GL error 0x%x\n", glErr);
  if (glErr != GL_NO_ERROR) {
    return 1;
  }

  /* ---- Wait for Remix's rendering, exactly as the header's documented flow describes ---- */
  GLenum srcLayout = GL_LAYOUT_COLOR_ATTACHMENT_EXT;
  glWaitSemaphoreEXT_(glDxvkDoneSema, 0, NULL, 1, &tex, &srcLayout);
  glErr = glGetError();
  printf("glWaitSemaphoreEXT -> GL error 0x%x\n", glErr);

  /* ---- Read it back ---- */
  GLuint fbo;
  glGenFramebuffers_(1, &fbo);
  glBindFramebuffer_(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D_(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
  GLenum fboStatus = glCheckFramebufferStatus_(GL_FRAMEBUFFER);
  printf("FBO status -> 0x%x\n", fboStatus);
  if (fboStatus != GL_FRAMEBUFFER_COMPLETE) {
    return 1;
  }

  unsigned char* pixels = (unsigned char*) malloc((size_t) interop.width * interop.height * 4);
  glReadPixels(0, 0, interop.width, interop.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  glErr = glGetError();
  printf("glReadPixels -> GL error 0x%x\n", glErr);

  /* ---- Signal client-done so Remix (if it checked) knows GL is finished with the image ---- */
  GLenum dstLayout = GL_LAYOUT_COLOR_ATTACHMENT_EXT;
  glSignalSemaphoreEXT_(glClientDoneSema, 0, NULL, 1, &tex, &dstLayout);
  glFinish();

  /* ---- Judge the result: not all-black, not a single flat color ---- */
  long sum = 0;
  unsigned char minV = 255, maxV = 0;
  for (size_t i = 0; i < (size_t) interop.width * interop.height * 4; i += 4) {
    for (int c = 0; c < 3; ++c) {
      unsigned char v = pixels[i + c];
      sum += v;
      if (v < minV) minV = v;
      if (v > maxV) maxV = v;
    }
  }
  double avg = (double) sum / ((double) interop.width * interop.height * 3);
  printf("pixel stats: avg=%.2f min=%u max=%u\n", avg, minV, maxV);

  writePpm(outPath, interop.width, interop.height, pixels);
  printf("wrote %s\n", outPath);

  int plausible = (maxV > minV) && (avg > 0.5);
  printf("RESULT: %s (avg=%.2f, range=%u-%u)\n", plausible ? "PLAUSIBLE_IMAGE" : "SUSPECT_BLANK", avg, minV, maxV);

  free(pixels);

  remix.Shutdown();
  return plausible ? 0 : 1;
}
