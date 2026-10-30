// Minimal standalone repro for the deterministic crash dxvk-remix hits inside the driver's
// NVSDK_NGX_VULKAN_Init_Ext2 (see evidence/nrd-dlss/ngx-bug-report.md and
// evidence/quality-pass/dlss-investigation.md). Creates a bare Vulkan instance/device with
// exactly the extensions dxvk-remix enables, then calls into NGX via the same static loader
// (libnvsdk_ngx.a) dxvk-remix links, with a variant switch to narrow down what triggers it.
//
// Build: see tests/build_ngx_init_min.sh
// Usage: ngx_init_min <variant>
//   a  minimal FeatureCommonInfo, no PathListInfo
//   b  PathListInfo pointing at a directory containing the DLSS .so files
//   c  same as dxvk-remix's actual call (PathListInfo + app id + logging off)
//   d  same as (c) but with the DLSS .so files absent from the search path
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <vulkan/vulkan.h>
#include "nvsdk_ngx.h"
#include "nvsdk_ngx_vk.h"

static VkInstance g_instance;
static VkPhysicalDevice g_phys;
static VkDevice g_device;

static const char* kInstanceExts[] = {
  "VK_KHR_get_physical_device_properties2",
};
// Full device extension list from the actual crash run's remix-dxvk.log
// ("Enabled device extensions:" block), not just the 4 NGX requires.
static const char* kDeviceExts[] = {
  "VK_EXT_4444_formats",
  "VK_EXT_calibrated_timestamps",
  "VK_EXT_conservative_rasterization",
  "VK_EXT_custom_border_color",
  "VK_EXT_depth_clip_enable",
  "VK_EXT_extended_dynamic_state",
  "VK_EXT_memory_priority",
  "VK_EXT_opacity_micromap",
  "VK_EXT_robustness2",
  "VK_EXT_shader_atomic_float",
  "VK_EXT_shader_demote_to_helper_invocation",
  "VK_EXT_shader_viewport_index_layer",
  "VK_EXT_transform_feedback",
  "VK_EXT_vertex_attribute_divisor",
  "VK_KHR_acceleration_structure",
  "VK_KHR_buffer_device_address",
  "VK_KHR_create_renderpass2",
  "VK_KHR_deferred_host_operations",
  "VK_KHR_depth_stencil_resolve",
  "VK_KHR_draw_indirect_count",
  "VK_KHR_driver_properties",
  "VK_KHR_external_memory",
  "VK_KHR_external_memory_fd",
  "VK_KHR_external_semaphore",
  "VK_KHR_external_semaphore_fd",
  "VK_KHR_image_format_list",
  "VK_KHR_maintenance4",
  "VK_KHR_pipeline_library",
  "VK_KHR_push_descriptor",
  "VK_KHR_ray_query",
  "VK_KHR_ray_tracing_pipeline",
  "VK_KHR_sampler_mirror_clamp_to_edge",
  "VK_KHR_shader_float16_int8",
  "VK_KHR_shader_float_controls",
  "VK_KHR_synchronization2",
  "VK_NVX_binary_import",
  "VK_NVX_image_view_handle",
  "VK_NV_low_latency",
  "VK_NV_present_metering",
  "VK_NV_ray_tracing_invocation_reorder",
};

static void die(const char* what, VkResult r) {
  fprintf(stderr, "%s failed: VkResult %d\n", what, r);
  exit(1);
}

static void createInstanceAndDevice(void) {
  VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
  appInfo.pApplicationName = "ngx_init_min";
#ifdef VK_API_VERSION_1_4
  appInfo.apiVersion = VK_API_VERSION_1_4;
#else
  appInfo.apiVersion = VK_API_VERSION_1_3;
#endif

  VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
  ici.pApplicationInfo = &appInfo;
  ici.enabledExtensionCount = sizeof(kInstanceExts) / sizeof(kInstanceExts[0]);
  ici.ppEnabledExtensionNames = kInstanceExts;

  VkResult r = vkCreateInstance(&ici, NULL, &g_instance);
  if (r != VK_SUCCESS) die("vkCreateInstance", r);

  uint32_t count = 0;
  vkEnumeratePhysicalDevices(g_instance, &count, NULL);
  if (count == 0) { fprintf(stderr, "no Vulkan physical devices\n"); exit(1); }
  VkPhysicalDevice* devices = malloc(sizeof(VkPhysicalDevice) * count);
  vkEnumeratePhysicalDevices(g_instance, &count, devices);

  // Pick the first discrete NVIDIA GPU, else the first device.
  g_phys = devices[0];
  for (uint32_t i = 0; i < count; i++) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(devices[i], &props);
    if (props.vendorID == 0x10DE) { g_phys = devices[i]; break; }
  }
  free(devices);

  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(g_phys, &props);
  fprintf(stderr, "GPU: %s (vendor 0x%04x)\n", props.deviceName, props.vendorID);

  // Matches the real crash run's queue layout (remix-dxvk.log "Queue families:"):
  // Graphics 0, Transfer 1, Async Compute/Present 2 -- assumes a 3+ queue family GPU
  // (true of the RTX 5070); falls back to 1 queue family if that's not available.
  uint32_t qfCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(g_phys, &qfCount, NULL);
  float prio = 1.0f;
  VkDeviceQueueCreateInfo qcis[3];
  uint32_t qciCount = qfCount >= 3 ? 3 : 1;
  for (uint32_t i = 0; i < qciCount; i++) {
    qcis[i] = (VkDeviceQueueCreateInfo){ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    qcis[i].queueFamilyIndex = i;
    qcis[i].queueCount = 1;
    qcis[i].pQueuePriorities = &prio;
  }

  VkPhysicalDeviceBufferDeviceAddressFeatures bdaFeatures = {
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES
  };
  bdaFeatures.bufferDeviceAddress = VK_TRUE;

  VkPhysicalDeviceSynchronization2Features sync2Features = {
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES
  };
  sync2Features.pNext = &bdaFeatures;
  sync2Features.synchronization2 = VK_TRUE;

  VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures = {
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR
  };
  rayQueryFeatures.pNext = &sync2Features;
  rayQueryFeatures.rayQuery = VK_TRUE;

  VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeatures = {
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR
  };
  rtPipelineFeatures.pNext = &rayQueryFeatures;
  rtPipelineFeatures.rayTracingPipeline = VK_TRUE;

  VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures = {
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR
  };
  asFeatures.pNext = &rtPipelineFeatures;
  asFeatures.accelerationStructure = VK_TRUE;

  VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
  dci.pNext = &asFeatures;
  dci.queueCreateInfoCount = qciCount;
  dci.pQueueCreateInfos = qcis;
  dci.enabledExtensionCount = sizeof(kDeviceExts) / sizeof(kDeviceExts[0]);
  dci.ppEnabledExtensionNames = kDeviceExts;

  r = vkCreateDevice(g_phys, &dci, NULL, &g_device);
  if (r != VK_SUCCESS) die("vkCreateDevice", r);
}

static const char* resultToString(NVSDK_NGX_Result r) {
  switch (r) {
    case NVSDK_NGX_Result_Success: return "Success";
    case NVSDK_NGX_Result_Fail: return "Fail";
    case NVSDK_NGX_Result_FAIL_FeatureNotSupported: return "FAIL_FeatureNotSupported";
    case NVSDK_NGX_Result_FAIL_PlatformError: return "FAIL_PlatformError";
    case NVSDK_NGX_Result_FAIL_FeatureAlreadyExists: return "FAIL_FeatureAlreadyExists";
    case NVSDK_NGX_Result_FAIL_FeatureNotFound: return "FAIL_FeatureNotFound";
    case NVSDK_NGX_Result_FAIL_InvalidParameter: return "FAIL_InvalidParameter";
    case NVSDK_NGX_Result_FAIL_ScratchBufferTooSmall: return "FAIL_ScratchBufferTooSmall";
    case NVSDK_NGX_Result_FAIL_NotInitialized: return "FAIL_NotInitialized";
    case NVSDK_NGX_Result_FAIL_UnsupportedInputFormat: return "FAIL_UnsupportedInputFormat";
    case NVSDK_NGX_Result_FAIL_RWFlagMissing: return "FAIL_RWFlagMissing";
    case NVSDK_NGX_Result_FAIL_MissingInput: return "FAIL_MissingInput";
    case NVSDK_NGX_Result_FAIL_UnableToInitializeFeature: return "FAIL_UnableToInitializeFeature";
    case NVSDK_NGX_Result_FAIL_OutOfDate: return "FAIL_OutOfDate";
    case NVSDK_NGX_Result_FAIL_OutOfGPUMemory: return "FAIL_OutOfGPUMemory";
    case NVSDK_NGX_Result_FAIL_UnsupportedFormat: return "FAIL_UnsupportedFormat";
    case NVSDK_NGX_Result_FAIL_UnableToWriteToAppDataPath: return "FAIL_UnableToWriteToAppDataPath";
    case NVSDK_NGX_Result_FAIL_UnsupportedParameter: return "FAIL_UnsupportedParameter";
    case NVSDK_NGX_Result_FAIL_Denied: return "FAIL_Denied";
    case NVSDK_NGX_Result_FAIL_NotImplemented: return "FAIL_NotImplemented";
    default: return "?";
  }
}

int main(int argc, char** argv) {
  const char* variant = argc > 1 ? argv[1] : "a";
  fprintf(stderr, "variant: %s\n", variant);

  createInstanceAndDevice();

  wchar_t logPath[] = L".";
  unsigned long long appId = 102100511ULL; // RtxOptions::applicationId() default

  NVSDK_NGX_FeatureCommonInfo featureInfo;
  memset(&featureInfo, 0, sizeof(featureInfo));

  const wchar_t* searchPaths[1];
  wchar_t searchDir[4096];

  if (strcmp(variant, "b") == 0 || strcmp(variant, "c") == 0) {
    const char* dir = argc > 2 ? argv[2] : ".";
    mbstowcs(searchDir, dir, sizeof(searchDir) / sizeof(wchar_t));
    searchPaths[0] = searchDir;
    featureInfo.PathListInfo.Path = searchPaths;
    featureInfo.PathListInfo.Length = 1;
  }
  if (strcmp(variant, "c") == 0) {
    featureInfo.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_VERBOSE;
  }

  PFN_vkGetInstanceProcAddr gipa = vkGetInstanceProcAddr;
  PFN_vkGetDeviceProcAddr gdpa =
      (PFN_vkGetDeviceProcAddr)vkGetInstanceProcAddr(g_instance, "vkGetDeviceProcAddr");

  fprintf(stderr, "calling NVSDK_NGX_VULKAN_Init...\n");
  fflush(stderr);
  NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_Init(
      appId, logPath, g_instance, g_phys, g_device,
      gipa, gdpa, &featureInfo, NVSDK_NGX_Version_API);
  fprintf(stderr, "NVSDK_NGX_VULKAN_Init returned: %s (%d)\n", resultToString(r), (int)r);

  if (r == NVSDK_NGX_Result_Success) {
    NVSDK_NGX_VULKAN_Shutdown1(g_device);
  }

  vkDestroyDevice(g_device, NULL);
  vkDestroyInstance(g_instance, NULL);
  fprintf(stderr, "done, no crash\n");
  return 0;
}
