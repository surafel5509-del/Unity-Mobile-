#!/usr/bin/env bash
# One-click offline APK export — after the SDK and Gradle are installed and cached.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export ANDROID_HOME="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}}"
export ANDROID_SDK_ROOT="$ANDROID_HOME"
[[ -d "$ANDROID_HOME/platforms/android-35" ]] || { echo "Missing offline SDK platform android-35 in $ANDROID_HOME" >&2; exit 2; }
[[ -d "$ANDROID_HOME/ndk/27.2.12479018" ]] || { echo "Missing offline NDK 27.2.12479018" >&2; exit 2; }
[[ -d "$ANDROID_HOME/cmake/3.22.1" ]] || { echo "Missing offline CMake 3.22.1" >&2; exit 2; }
command -v gradle >/dev/null || { echo "Install Gradle 8.9 first (must be available offline)." >&2; exit 2; }
variant="${1:-debug}"
case "$variant" in debug|release) ;; *) echo "Usage: $0 [debug|release] [--install]" >&2; exit 2;; esac
if [[ "$variant" == release && -z "${PRISM_KEYSTORE:-}" ]]; then
  echo "Release signing requires PRISM_KEYSTORE, PRISM_KEYSTORE_PASSWORD, PRISM_KEY_ALIAS, PRISM_KEY_PASSWORD" >&2
  exit 2
fi
cd "$ROOT/android"
# --offline guarantees zero network access. No AAB, no other platform output.
gradle --offline --no-daemon ":app:assemble${variant^}"
apk="$ROOT/android/app/build/outputs/apk/$variant/app-$variant.apk"
[[ -s "$apk" ]] || { echo "APK not generated: $apk" >&2; exit 1; }
echo "PRISM APK: $apk"
if [[ "${2:-}" == --install ]]; then
  command -v adb >/dev/null || { echo "ADB not found" >&2; exit 2; }
  adb install -r "$apk"
fi
