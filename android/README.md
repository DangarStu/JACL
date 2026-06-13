# JACL for Android

A native Android tablet app that plays JACL games, running the **same C
interpreter** as the iPad app: the JACL core (`../src`), RemGlk (`../ios/remglk`)
and the embed glue (`../ios/ios_startup.c`, `../ios/jacl_bridge.c`) are
cross-compiled to `libjacl.so` via the NDK/CMake, with a thin JNI shim
(`app/src/main/cpp/android_jni.c`) and a Jetpack Compose UI.

## Requirements

- Android Studio (or the command-line SDK) with:
  - **NDK 28.2.13676358** and **CMake 3.22.1** (declared in `app/build.gradle.kts`)
  - a platform for `compileSdk 34`
- A JDK 17+ (Android Studio's bundled JBR works).
- `minSdk 29` (RemGlk's `timespec_get` for timers needs API 29).

`local.properties` must point at your SDK (`sdk.dir=...`); it's gitignored.

## Build & run (debug)

```sh
./gradlew :app:assembleDebug                 # build app-debug.apk
./gradlew :app:installDebug                  # install on a connected device/emulator
```

The first build downloads the Android Gradle Plugin, Kotlin and Compose, then
runs the native CMake build (`arm64-v8a` + `x86_64`).

## Release signing

Signing credentials are read from `keystore.properties` (gitignored), so no
keystore or passwords are in the repo.

1. Create a release keystore once (keep it safe — it identifies you as the
   publisher and can't be regenerated):

   ```sh
   keytool -genkeypair -v -keystore jacl-release.jks -alias jacl \
           -keyalg RSA -keysize 2048 -validity 10000
   ```

2. Copy the template and fill in the real values:

   ```sh
   cp keystore.properties.example keystore.properties
   # edit keystore.properties (storeFile is relative to android/)
   ```

3. Build the signed artifacts:

   ```sh
   ./gradlew :app:assembleRelease    # signed APK  (app/build/outputs/apk/release/)
   ./gradlew :app:bundleRelease      # signed AAB  (app/build/outputs/bundle/release/) for Google Play
   ```

Without `keystore.properties`, the release build still configures but is left
unsigned (sign later with `apksigner` if needed).
