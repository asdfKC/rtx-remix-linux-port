#!/usr/bin/env bash
# Installs RTX Remix for Minecraft 1.21.1 on Linux into a Prism Launcher instance:
# Fabric Loader + Fabric API + the mod JAR, which bundles the native Linux runtime
# (libremix.so; the DLSS libraries only with --dlss) and the JNI bridge (libremixbridge.so).
#
# Usage: scripts/install.sh [options]
#   --work DIR            work directory of scripts/build-runtime.sh (default /mnt/big/rtx-remix-linux)
#   --prism-dir DIR       Prism Launcher data directory (default ~/.local/share/PrismLauncher)
#   --instance-id ID      instance folder name (default rtx-remix-linux)
#   --skip-tests          build without running the mod's unit tests
#   --mod-jar FILE        install this mod JAR instead of building one
#   --fabric-api-jar FILE install this Fabric API JAR instead of resolving it with Gradle
#   --min-driver N        DXVK_REMIX_NVIDIA_MIN_DRIVER for the instance (default 600, 0 = not set)
#   --reset-mod           recreate the mod checkout's linux-port branch from the pinned commit
#   --dlss                also bundle the DLSS libraries and enable NGX (REMIX_LINUX_ENABLE_NGX=1).
#                         Off by default: NGX init faults on Linux (PORTING_NOTES, Phase D step 7)
#   --diagnostics         also apply patches/rtx-remix-minecraft/optional/ and set REMIX_LINUX_DIAGNOSTICS=1
#                         (interop texture dumps into <game dir>/remix-diagnostics/, per-second log line)
#   --no-prime            leave the NVIDIA PRIME render-offload variables out of the launch script
#   --launcher FILE       where to write the launch script (default ~/.local/bin/remix-minecraft)
#
# The game environment is set by a generated launch script, not by Prism's instance settings: Prism's
# WrapperCommand did not reach the game's environment (checked in /proc/<pid>/environ on the RTX 5070).
# The script exports the variables and runs `prismlauncher -l <instance>`, so the game inherits them.
#
# Needs only official Arch packages, and never runs pacman or sudo itself. What it
# downloads, all from the repositories' own pins: the mod source from GitHub; Gradle 9.3.1
# (checked against the wrapper's pinned SHA-256); Minecraft, Fabric and the mod's Maven
# dependencies through Gradle/Loom; the runtime textures through the mod's Packman pin.
# Prism downloads Minecraft and Fabric Loader for the instance when it first launches it.
set -euo pipefail

PORT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK=/mnt/big/rtx-remix-linux
PRISM_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/PrismLauncher"
INSTANCE_ID=rtx-remix-linux
SKIP_TESTS=0
MOD_JAR=
FABRIC_API_JAR=
MIN_DRIVER=600
RESET_MOD=0
DLSS=0
DIAGNOSTICS=0
PRIME=1
LAUNCHER="$HOME/.local/bin/remix-minecraft"

while [ $# -gt 0 ]; do
  case "$1" in
    --work) WORK="$2"; shift 2 ;;
    --prism-dir) PRISM_DIR="$2"; shift 2 ;;
    --instance-id) INSTANCE_ID="$2"; shift 2 ;;
    --skip-tests) SKIP_TESTS=1; shift ;;
    --mod-jar) MOD_JAR="$(realpath "$2")"; shift 2 ;;
    --fabric-api-jar) FABRIC_API_JAR="$(realpath "$2")"; shift 2 ;;
    --min-driver) MIN_DRIVER="$2"; shift 2 ;;
    --reset-mod) RESET_MOD=1; shift ;;
    --dlss) DLSS=1; shift ;;
    --diagnostics) DIAGNOSTICS=1; shift ;;
    --no-prime) PRIME=0; shift ;;
    --launcher) LAUNCHER="$2"; shift 2 ;;
    -h|--help) sed -n '2,32p' "$0"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

# Pins (see PINS.md). The mod patches in patches/rtx-remix-minecraft/ apply to this commit.
MOD_REPO=https://github.com/NVIDIAGameWorks/rtx-remix-minecraft
MOD_COMMIT=f2c6fdaa763f45676f1576e63f8e054bf4ef16af
# sha256 of gradle/wrapper/gradle-wrapper.jar at MOD_COMMIT (Gradle 9.3.1 wrapper).
WRAPPER_JAR_SHA256=d3b261c2820e9e3d8d639ed084900f11f4a86050a8f83342ade7b6bc9b0d2bdd
MC_VERSION=1.21.1
FABRIC_LOADER_VERSION=0.16.5
FABRIC_API_VERSION=0.116.10+1.21.1
JAVA_HOME_21=/usr/lib/jvm/java-21-openjdk
INSTANCE_NAME="RTX Remix for Minecraft (Linux)"

log() { printf '\n==> %s\n' "$*"; }
die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

# --- 1. packages ------------------------------------------------------------
log "Checking packages (official Arch repositories only)"
declare -A WHY=(
  [prismlauncher]="Prism Launcher, runs the Minecraft instance (extra)"
  [jdk21-openjdk]="Java 21 JDK: runs Minecraft 1.21.1 and Gradle, and provides jni.h for the bridge (extra)"
  [git]="clones the mod source and applies the patches (core)"
  [cmake]="builds the JNI bridge libremixbridge.so (extra)"
  [base-devel]="compiler toolchain (gcc, make) for the bridge (core)"
  [vulkan-icd-loader]="libvulkan.so, which the runtime loads (extra)"
)
missing=()
for pkg in prismlauncher jdk21-openjdk git cmake base-devel vulkan-icd-loader; do
  pacman -Qq "$pkg" >/dev/null 2>&1 || missing+=("$pkg")
done
if [ ${#missing[@]} -gt 0 ]; then
  echo "Missing packages. Install them with:"
  echo "  sudo pacman -S --needed ${missing[*]}"
  for pkg in "${missing[@]}"; do echo "    $pkg: ${WHY[$pkg]}"; done
  exit 1
fi
[ -x "$JAVA_HOME_21/bin/java" ] || die "$JAVA_HOME_21/bin/java not found (jdk21-openjdk)"
export JAVA_HOME="$JAVA_HOME_21"

if pgrep -x prismlauncher >/dev/null 2>&1; then
  die "Prism Launcher is running. Close it first: it rewrites instance files while open."
fi

# --- 2. runtime -------------------------------------------------------------
RUNTIME="$WORK/build/src/d3d9/libremix.so"
if [ -z "$MOD_JAR" ]; then
  [ -f "$RUNTIME" ] || die "No runtime at $RUNTIME. Build it first: scripts/build-runtime.sh $WORK"
fi

# --- 3. mod source ----------------------------------------------------------
MOD_DIR="$WORK/rtx-remix-minecraft"
patches=("$PORT_DIR"/patches/rtx-remix-minecraft/*.patch)
[ "$DIAGNOSTICS" = 1 ] && patches+=("$PORT_DIR"/patches/rtx-remix-minecraft/optional/*.patch)
if [ -z "$MOD_JAR" ]; then
  if [ ! -d "$MOD_DIR/.git" ]; then
    log "Cloning the mod at $MOD_COMMIT"
    git clone --filter=blob:none --no-checkout "$MOD_REPO" "$MOD_DIR"
    RESET_MOD=1
  fi
  if [ "$RESET_MOD" = 1 ]; then
    log "Applying ${#patches[@]} Linux patches to a fresh linux-port branch"
    git -C "$MOD_DIR" checkout -q -f -B linux-port "$MOD_COMMIT"
    git -C "$MOD_DIR" -c user.name=build -c user.email=build@localhost am -q "${patches[@]}"
  else
    applied=$(git -C "$MOD_DIR" rev-list --count "$MOD_COMMIT..HEAD" 2>/dev/null || echo -1)
    [ "$applied" = "${#patches[@]}" ] || die "$MOD_DIR has $applied commits on top of the pin, expected ${#patches[@]} patches (the series changed, or --diagnostics differs from the last run). Rerun with --reset-mod (this discards local changes in that checkout)."
  fi

  actual=$(sha256sum "$MOD_DIR/gradle/wrapper/gradle-wrapper.jar" | cut -d' ' -f1)
  [ "$actual" = "$WRAPPER_JAR_SHA256" ] || die "gradle-wrapper.jar sha256 $actual does not match the pin $WRAPPER_JAR_SHA256"

  # --- 4. build -------------------------------------------------------------
  log "Building the mod JAR (first run downloads Gradle, Minecraft and Fabric; takes a while)"
  gradle_args=(--no-daemon)
  [ "$SKIP_TESTS" = 1 ] && gradle_args+=(-x test)
  stage_args=(-PlinuxRuntime="$RUNTIME")
  [ "$DLSS" = 1 ] && stage_args+=(-PlinuxDlss=true)
  (cd "$MOD_DIR" && ./gradlew "${gradle_args[@]}" stageLinuxRuntime "${stage_args[@]}")
  (cd "$MOD_DIR" && ./gradlew "${gradle_args[@]}" package stageFabricApi)
  MOD_JAR=$(ls -t "$MOD_DIR"/build/distributions/rtx-remix-renderer-*-linux64.jar 2>/dev/null | head -1)
  [ -n "$MOD_JAR" ] || die "No packaged JAR in $MOD_DIR/build/distributions"
  [ -n "$FABRIC_API_JAR" ] || FABRIC_API_JAR="$MOD_DIR/build/dev/fabric-api.jar"
fi
[ -f "$MOD_JAR" ] || die "Mod JAR not found: $MOD_JAR"
[ -n "$FABRIC_API_JAR" ] && [ -f "$FABRIC_API_JAR" ] || die "Fabric API JAR not found (pass --fabric-api-jar)"

# --- 5. Prism instance ------------------------------------------------------
INSTANCE="$PRISM_DIR/instances/$INSTANCE_ID"
CFG="$INSTANCE/instance.cfg"
log "Prism instance: $INSTANCE"
mkdir -p "$INSTANCE/.minecraft/mods"
if [ ! -f "$CFG" ]; then
  printf 'InstanceType=OneSix\nname=%s\nJavaVersion=21\n' "$INSTANCE_NAME" > "$CFG"
  cat > "$INSTANCE/mmc-pack.json" <<EOF
{
    "components": [
        { "cachedName": "Minecraft", "important": true, "uid": "net.minecraft", "version": "$MC_VERSION" },
        { "cachedName": "Fabric Loader", "uid": "net.fabricmc.fabric-loader", "version": "$FABRIC_LOADER_VERSION" }
    ],
    "formatVersion": 1
}
EOF
  echo "Created the instance."
else
  cp "$CFG" "$CFG.bak"
  echo "Reusing the instance (instance.cfg backed up to instance.cfg.bak)."
fi

# Sets KEY=VALUE in instance.cfg, replacing an existing KEY line.
set_cfg() {
  local key="$1" value="$2" tmp
  tmp=$(mktemp "$CFG.XXXXXX")
  grep -v "^$key=" "$CFG" > "$tmp" || true
  printf '%s=%s\n' "$key" "$value" >> "$tmp"
  mv "$tmp" "$CFG"
}

# JVM arguments: the mod's RECOMMENDED_JVM_ARGS from packaging/installer.ps1 (the
# repository's single source for them), without heap sizes, which Prism sets from
# its memory settings instead. -Xmx2G becomes MaxMemAlloc=2048, as on Windows.
# -Xss8m is this port's own addition: the JVM's default thread stack (~1MB on Linux) is too
# small for NVIDIA's NGX (DLSS) init call, which segfaults inside the driver's
# libnvidia-ngx.so with a stack under ~2MB (see evidence/look-and-run/job1-dlss-stack-fix.md).
JVM_ARGS="-XX:+UnlockExperimentalVMOptions -XX:+UseG1GC -XX:G1NewSizePercent=20 -XX:G1ReservePercent=20 -XX:MaxGCPauseMillis=50 -XX:G1HeapRegionSize=32M -XX:HeapDumpPath=MojangTricksIntelDriversForPerformance_javaw.exe_minecraft.exe.heapdump -Xss8m"
if [ -f "$MOD_DIR/packaging/installer.ps1" ]; then
  from_installer=$(sed -n "s/^\$script:RECOMMENDED_JVM_ARGS = '\(.*\)'.*/\1/p" "$MOD_DIR/packaging/installer.ps1")
  [ -n "$from_installer" ] && JVM_ARGS=$(printf '%s\n' $from_installer | grep -v -e '^-Xmx' -e '^-Xms' | paste -sd' ')
  JVM_ARGS="$JVM_ARGS -Xss8m"
fi
set_cfg OverrideJavaLocation true
set_cfg JavaPath "$JAVA_HOME_21/bin/java"
set_cfg OverrideJavaArgs true
set_cfg JvmArgs "$JVM_ARGS"
set_cfg OverrideMemory true
set_cfg MinMemAlloc 512
set_cfg MaxMemAlloc 2048
# Prism's "AutomaticJava" instance option, when true, silently ignores JavaPath/JvmArgs/
# memory settings above and re-detects its own Java + default flags at launch time -- the
# custom JvmArgs (including -Xss8m) were never actually passed to the game process until this
# was set. Must come after the set_cfg calls above since it lives in a different ini section.
set_cfg AutomaticJava false
# Earlier versions of this script set the environment through WrapperCommand, which did not reach the
# game. Clear it; the launch script below sets the environment instead.
set_cfg OverrideCommands false
set_cfg WrapperCommand ""

# --- 6. launch script -------------------------------------------------------
game_env=()
# The runtime's NVIDIA driver minimum is not Linux-aware yet (KNOWN_GAPS.md).
[ "$MIN_DRIVER" != 0 ] && game_env+=("DXVK_REMIX_NVIDIA_MIN_DRIVER=$MIN_DRIVER")
# Synchronous first-use shader compilation: with async compilation the runtime silently skips ray
# tracing while a pipeline compiles (PORTING_NOTES, Phase D step 5).
game_env+=(RTX_ENABLE_ASYNC_COMPILATION=0)
# PRIME render offload: puts Minecraft's GL context on the NVIDIA GPU on hybrid systems, where the
# interop image (Vulkan on NVIDIA) must be imported by a GL context on the same GPU.
[ "$PRIME" = 1 ] && game_env+=(__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia __VK_LAYER_NV_optimus=NVIDIA_only)
[ "$DLSS" = 1 ] && game_env+=(REMIX_LINUX_ENABLE_NGX=1)
[ "$DIAGNOSTICS" = 1 ] && game_env+=(REMIX_LINUX_DIAGNOSTICS=1)
mkdir -p "$(dirname "$LAUNCHER")"
{
  printf '#!/bin/sh
'
  printf '# Generated by rtx-remix-linux-port scripts/install.sh. Starts the Prism instance %s with the
' "$INSTANCE_ID"
  printf '# environment the Linux runtime needs. Prism passes its own environment on to the game, so the
'
  printf '# variables are exported here; Prism'"'"'s WrapperCommand did not pass them through.
'
  for kv in "${game_env[@]}"; do printf 'export %s
' "$kv"; done
  printf 'if pgrep -x prismlauncher >/dev/null 2>&1; then
'
  printf '  echo "Prism Launcher is already running: close it first, or the game starts without this environment." >&2
'
  printf '  exit 1
'
  printf 'fi
'
  printf 'exec prismlauncher -l %s "$@"
' "$INSTANCE_ID"
} > "$LAUNCHER"
chmod +x "$LAUNCHER"

# Replace earlier copies of the mod and of Fabric API in this instance only.
rm -f "$INSTANCE/.minecraft/mods"/rtx-remix-renderer-*.jar "$INSTANCE/.minecraft/mods"/fabric-api-*.jar
cp "$MOD_JAR" "$INSTANCE/.minecraft/mods/"
cp "$FABRIC_API_JAR" "$INSTANCE/.minecraft/mods/fabric-api-$FABRIC_API_VERSION.jar"

log "Installed"
echo "  instance: $INSTANCE_NAME ($INSTANCE)"
echo "  mods:     $(basename "$MOD_JAR"), fabric-api-$FABRIC_API_VERSION.jar"
echo
echo "  launcher: $LAUNCHER"
echo
echo "Sign in once in Prism Launcher with a Microsoft account that owns Minecraft: Java Edition, close"
echo "Prism, then start the game with $LAUNCHER (not from Prism's own window, which would"
echo "start it without the environment above). The first launch extracts the runtime into"
echo "  $INSTANCE/.minecraft/remix-sdk/"
echo "Logs: $INSTANCE/.minecraft/logs/latest.log and $INSTANCE/.minecraft/rtx-remix/logs/remix-dxvk.log"
echo "Crash reports from native code: $INSTANCE/.minecraft/crash/"
echo "Uninstall: delete $INSTANCE"
