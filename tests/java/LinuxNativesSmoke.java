package com.nvidia.rtxremix;

public class LinuxNativesSmoke {
    public static void main(String[] args) throws Exception {
        System.load(args[0]);
        System.out.println("setOutputSize before initialize: " + RemixBridge.linuxSetOutputSize(800, 600));
        System.out.println("initialize rc: " + RemixBridge.initialize(args[1]));
        System.out.println("setOutputSize(800,600): " + RemixBridge.linuxSetOutputSize(800, 600));
        System.out.println("setOutputSize(0,0): " + RemixBridge.linuxSetOutputSize(0, 0));
        int fd = RemixBridge.posixDupFd(1);
        System.out.println("dup(1) -> " + fd + ", dup(-5) -> " + RemixBridge.posixDupFd(-5));
        RemixBridge.posixCloseFd(fd);
        System.out.println("startup rc: " + RemixBridge.startup(0, false, true, false, false, false));
        System.out.println("shutdown rc: " + RemixBridge.shutdown());
    }
}
