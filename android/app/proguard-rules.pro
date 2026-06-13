# ProGuard / R8 rules for the release build.
#
# Minification is currently disabled (isMinifyEnabled = false), so these rules
# are not applied today; the file exists so the release buildType's
# proguardFiles reference resolves, and as the place to add keep-rules if R8 is
# enabled later. The native interface is JNI-bound by exact name from
# android_jni.c, so if you enable R8 keep the bridge entry points:
#
# -keepclasseswithmembernames class au.com.dangarmarine.jacl.GlkBridge {
#     native <methods>;
# }
