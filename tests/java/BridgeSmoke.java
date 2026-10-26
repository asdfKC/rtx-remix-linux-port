package com.nvidia.rtxremix;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

public class BridgeSmoke {
    public static void main(String[] args) throws Exception {
        System.load(args[0]);
        String line = RemixBridge.processCommandLine();
        System.out.println("processCommandLine: " + line);
        CommandLineRedaction.Result r = CommandLineRedaction.redact(line, List.of("--accessToken"));
        System.out.println("blanked values: " + r.blankedValues());
        System.out.println("overwrite rc: " + RemixBridge.overwriteProcessCommandLine(r.commandLine()));
        String proc = new String(Files.readAllBytes(Path.of("/proc/self/cmdline"))).replace('\0', '|');
        System.out.println("/proc/self/cmdline: " + proc);
        System.out.println("initialize rc: " + RemixBridge.initialize(args[1]));
        if (args.length > 2) {
            System.out.println("startup rc: " + RemixBridge.startup(0, false, true, false, false, false));
            System.out.println("present rc: " + RemixBridge.present(0, 1, 0, 0, 0));
            System.out.println("shutdown rc: " + RemixBridge.shutdown());
        }
    }
}
