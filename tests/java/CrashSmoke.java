package com.nvidia.rtxremix;

public class CrashSmoke {
    public static void main(String[] args) {
        System.load(args[0]);
        System.out.println("initialize rc: " + RemixBridge.initialize(args[1]));
        // A Java NPE must still be handled by the JVM with the bridge's handler installed.
        try {
            Object o = null;
            o.hashCode();
        } catch (NullPointerException e) {
            System.out.println("JVM implicit null check still works");
        }
        System.out.println("faulting inside the bridge now");
        RemixBridge.d3d9Release(16);
    }
}
