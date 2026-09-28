# 3. Android APK export — step by step

## Provision once (online), then develop in airplane mode

1. Install JDK 17, Gradle **8.9**, Android SDK platform/build-tools **35**, NDK **27.2.12479018**, CMake **3.22.1**, and ADB. Store all artifacts locally. In CI these are provisioned on GitHub's connected runner; they are **not** vendored in this repo. A full offline installer is future work.
2. With network temporarily available, run `cd android && gradle :app:assembleDebug` once to populate the Gradle cache. For first-time builds this downloads the Android Gradle Plugin and transitive dependencies.
3. Disconnect Wi-Fi, enable airplane mode. Run `tools/build-apk.sh debug`. The script checks SDK/NDK/CMake and runs Gradle `--offline --no-daemon :app:assembleDebug`.
4. Output: `android/app/build/outputs/apk/debug/app-debug.apk`, signed with the local **debug key** (Android Gradle Plugin default). This is installable but not a release certificate.
5. Connect Android API 26+ device over USB, enable developer options + USB debugging. Run `adb devices` then `tools/build-apk.sh debug --install` or `adb install -r android/app/build/outputs/apk/debug/app-debug.apk`.
6. Release: create a signing keystore locally (never commit it), export `PRISM_KEYSTORE`, `PRISM_KEYSTORE_PASSWORD`, `PRISM_KEY_ALIAS`, `PRISM_KEY_PASSWORD`, run `tools/build-apk.sh release`. Output `app-release.apk`; keep the keystore in a secure offline location. No AAB or Play Store publishing is produced.
7. Validate: `unzip -l app-debug.apk | grep libprism_runtime`, `adb shell pm list packages | grep prism`, `adb logcat -s PRISM:I`, tap movement/jump and observe FPS HUD.

**The current exporter builds the bundled demo only.** The C# project manager intentionally rejects custom-project builds rather than silently shipping the wrong game. Integrate scene/script/asset import into Gradle generation before advertising arbitrary project export.

## Offline APK checklist

- [ ] SDK/NDK/CMake/Gradle/AGP dependencies cached locally before disconnect.
- [ ] `./build_tests.sh` passes on the build machine.
- [ ] Airplane mode on laptop and target device, no Wi-Fi connection.
- [ ] `tools/build-apk.sh debug` succeeds with `--offline` (no network).
- [ ] `app-debug.apk` exists and is nonempty; `unzip -l` lists `.so` and `sample.prism`.
- [ ] `aapt dump badging` confirms package and API; `aapt dump permissions` shows **no INTERNET permission**.
- [ ] APK installs via ADB; opens in landscape; both 2D player and 3D cube render; touch moves and jumps.
- [ ] Force stop / resume: no crash; rotate: locked landscape. Test API 26 and API 35.
- [ ] Test both arm64-v8a and armeabi-v7a physical devices/emulators.
- [ ] Save & unlock templates are **not production-ready**; do not claim encryption or fraud prevention.
- [ ] Release signature verified with `apksigner verify --verbose` before distribution.

## GitHub workflow

`.github/workflows/android-apk.yml` runs C++20 host tests then builds the Android debug APK and uploads a `prism-engine-debug-apk` artifact. CI requires internet during initial SDK/Gradle provisioning. The resulting game APK itself requires no internet.
