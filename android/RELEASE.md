# Releasing JACL for Android

A checklist for publishing the app to Google Play (and/or distributing the APK
directly). The build/signing mechanics are in [README.md](README.md); this is
the "what do I actually need to do" list for shipping.

## One-time setup

1. **Google Play Developer account** — register at
   <https://play.google.com/console> (one-time US$25 fee).
2. **Create the app** in the Play Console (name: *JACL*, default language,
   app/not game — your call, though it's a games platform).
3. **Create the upload keystore** (once) and keep it + its passwords safe and
   backed up off-machine — it is your publisher identity and **cannot be
   regenerated**:
   ```sh
   cd android
   keytool -genkeypair -v -keystore jacl-release.jks -alias jacl \
           -keyalg RSA -keysize 2048 -validity 10000
   cp keystore.properties.example keystore.properties   # fill in the passwords
   ```
4. **Enrol in Play App Signing** (recommended): you upload an AAB signed with
   the *upload* key above; Google holds the real *app signing* key. If you ever
   lose the upload key, Google can reset it.

## Each release

1. **Bump the version** in `app/build.gradle.kts` — `versionCode` must strictly
   increase for every upload; set a human `versionName` (e.g. `1.0`).
2. **Build the signed bundle**:
   ```sh
   ./gradlew :app:bundleRelease      # app/build/outputs/bundle/release/app-release.aab
   ```
3. **Smoke-test the release build** on a device/emulator (it's a different,
   minified-config build than debug):
   ```sh
   ./gradlew :app:assembleRelease && adb install -r \
     app/build/outputs/apk/release/app-release.apk
   ```
4. **Upload the `.aab`** to a Play Console track — start with **Internal
   testing**, then promote to Closed → Open → Production.

## Store-listing requirements (first submission)

- App name, short description, full description.
- Graphics: app icon (have it), **feature graphic** (1024×500), and
  **screenshots** — include **7" and 10" tablet** screenshots (this is a tablet
  app); phone screenshots too.
- **Privacy policy URL** — <https://jacl.dangarmarine.com.au/privacy.html>
  (already linked in the app's Settings).
- **Data safety form** — declare **no data collected / no data shared**: the app
  makes no network connections, has no accounts, and stores games + saves only
  on-device.
- **Content rating questionnaire** — note that the bundled *Down Dragon*
  contains coarse language and adult themes; answer accordingly so the rating
  is correct.
- Target audience & ads — **no ads**; pick an appropriate age range.
- Category / contact email / website.

## Decisions worth making before launch

- **ABIs**: the build ships `arm64-v8a` (real devices) + `x86_64` (emulators).
  With an **AAB**, Play delivers per-device splits, so users only download their
  own ABI — keeping `x86_64` costs real users nothing. Add `armeabi-v7a` only if
  you want very old 32-bit devices (verify the native build links there first).
- **Bundled starters**: *The Down Dragon* and *The Unholy Grail* ship inside the
  app (`app/src/main/assets/`). Confirm you're happy distributing those, and
  consider whether the content rating should reflect Dragon's themes.
- **`minSdk 29`** (Android 10) covers the large majority of active tablets and
  is required by RemGlk's `timespec_get`. Lower would need a shim.

## Distributing without Google Play (sideload)

```sh
./gradlew :app:assembleRelease       # signed APK
```
Share `app-release.apk`; users enable "install unknown apps" for the source.
No store listing, rating, or review needed — but also no auto-updates.
