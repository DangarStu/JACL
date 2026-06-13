import java.io.FileInputStream
import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

// Release signing is configured from keystore.properties (gitignored), so no
// keystore or passwords live in the repo. If the file is absent (a fresh clone,
// CI, or anyone who only builds debug) the release build is simply left
// unsigned -- the project still configures and assembleDebug still works.
// See keystore.properties.example and README.md for the one-time setup.
val keystorePropertiesFile = rootProject.file("keystore.properties")
val keystoreProperties = Properties().apply {
    if (keystorePropertiesFile.exists()) FileInputStream(keystorePropertiesFile).use { load(it) }
}

android {
    namespace = "au.com.dangarmarine.jacl"
    compileSdk = 34
    ndkVersion = "28.2.13676358"

    defaultConfig {
        applicationId = "au.com.dangarmarine.jacl"
        minSdk = 29            // timespec_get (RemGlk timers) needs API 29+
        targetSdk = 34
        versionCode = 1
        versionName = "0.1"

        externalNativeBuild {
            cmake {
                // Match the minSdk so the native link finds API-29 libc symbols.
                arguments += "-DANDROID_PLATFORM=android-29"
            }
        }
        // arm64 for devices and Apple-Silicon emulators; x86_64 for Intel ones.
        ndk { abiFilters += listOf("arm64-v8a", "x86_64") }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    signingConfigs {
        if (keystorePropertiesFile.exists()) {
            create("release") {
                storeFile = rootProject.file(keystoreProperties.getProperty("storeFile"))
                storePassword = keystoreProperties.getProperty("storePassword")
                keyAlias = keystoreProperties.getProperty("keyAlias")
                keyPassword = keystoreProperties.getProperty("keyPassword")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            // Sign with the release key when configured; otherwise leave the
            // release artifact unsigned (you can sign it later with apksigner).
            if (keystorePropertiesFile.exists()) {
                signingConfig = signingConfigs.getByName("release")
            }
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
    buildFeatures { compose = true }
}

dependencies {
    val composeBom = platform("androidx.compose:compose-bom:2024.06.00")
    implementation(composeBom)
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.activity:activity-compose:1.9.0")
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-tooling-preview")
    implementation("androidx.compose.foundation:foundation")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.material:material-icons-extended")
    implementation("androidx.navigation:navigation-compose:2.7.7")
    debugImplementation("androidx.compose.ui:ui-tooling")
}
