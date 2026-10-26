package com.nvidia.rtxremix.minecraft.gl;
import com.nvidia.rtxremix.InteropInfo;
import com.nvidia.rtxremix.RemixBridge;
import com.nvidia.rtxremix.TextureFormat;
import org.lwjgl.opengl.StubGl;
import java.nio.file.*;
public class FdProtocolSmoke {
  static long openFds() throws Exception { try (var s = Files.list(Path.of("/proc/self/fd"))) { return s.count(); } }
  // Like dxvk_GetInteropInfo: fresh caller-owned descriptors on every call; no image before the first Present.
  static InteropInfo info(boolean presented, int w, int h) {
    InteropInfo i = new InteropInfo();
    i.dxvkDoneSemaphore_Handle = RemixBridge.posixDupFd(0);
    i.clientDoneSemaphore_Handle = RemixBridge.posixDupFd(0);
    if (presented) { i.imageMemory_Handle = RemixBridge.posixDupFd(0); i.allocationSize = (long) w * h * 4; i.width = w; i.height = h;
      i.interopImageFormat = TextureFormat.RGBA8_UNORM; }
    return i;
  }
  public static void main(String[] a) throws Exception {
    System.load(a[0]);
    StubGl.closer = RemixBridge::posixCloseFd;
    long base = openFds();
    GlInterop gl = new GlInterop(info(false, 0, 0));          // at init, before any frame (the in-game case)
    System.out.println("after init: imports=" + StubGl.imports + " fds held by 'GL' would be 2; process fds +" + (openFds() - base));
    boolean presented = false; int presents = 0;
    for (int frame = 0; frame < 50; frame++) {
      int w = frame < 25 ? 854 : 1280, h = frame < 25 ? 480 : 720;   // a resize half way
      gl.recreateImagesIf(info(presented, w, h)); gl.waitForVk(null);  // blitVkImageToGL
      gl.recreateImagesIf(info(presented, w, h)); gl.copyFromFramebuffer(0); gl.signalDone();  // blitGlToRemix
      gl.beforeRemixPresent(); presents++; presented = true; gl.afterRemixPresent();
    }
    System.out.println("after 50 frames: process fds +" + (openFds() - base) + ", imports=" + StubGl.imports
        + " (2 semaphores + 2 image imports), waits=" + StubGl.waits + " for " + presents + " presents");
  }
}
