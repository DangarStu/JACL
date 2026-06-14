/* android_jni.c --- JNI shim between the Kotlin app and the embedded RemGlk
 * terp. The Android equivalent of the SwiftUI side's socketpair setup: it
 * mirrors what GlkBridge.swift does in Swift, but here in C because Android's
 * Kotlin can't create a raw socketpair fd as cleanly.
 *
 * nativeStart() creates a socketpair, spawns a thread running jacl_bridge_run()
 * (the same POSIX glue the iPad uses) on the terp end, and returns the app end
 * fd to Kotlin, which wraps it in a ParcelFileDescriptor and exchanges RemGlk
 * JSON over it -- read updates, write events -- exactly as the Swift reader does.
 */

#include <jni.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <android/log.h>

#include "jacl_bridge.h"

#define LOG_TAG "JACL"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

/* Args handed to the terp thread. The thread runs jacl_bridge_run(), which on a
 * normal quit never returns (glk_exit -> pthread_exit), so this small struct
 * (and the strdup'd path) leak one allocation per launched game -- negligible,
 * and the alternative (freeing after a call that never returns) is impossible. */
struct terp_args {
    char *path;
    int   terp_fd;
};

static void *terp_thread(void *raw)
{
    struct terp_args *a = (struct terp_args *) raw;
    /* jacl_bridge_run copies the path (jacl_ios_set_gamepath) and dup2's
     * terp_fd onto stdin/stdout, then runs the game to completion. */
    jacl_bridge_run(a->path, a->terp_fd);
    return NULL;   /* unreached on a normal quit */
}

/* int nativeStart(String gamePath) -> app-side socket fd, or -1 on failure.
 * Kotlin adopts the returned fd via ParcelFileDescriptor.adoptFd(). */
JNIEXPORT jint JNICALL
Java_au_com_dangarmarine_jacl_GlkBridge_nativeStart(JNIEnv *env, jobject thiz, jstring gamePath)
{
    const char *path = (*env)->GetStringUTFChars(env, gamePath, NULL);
    if (path == NULL) return -1;

    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        LOGE("socketpair failed");
        (*env)->ReleaseStringUTFChars(env, gamePath, path);
        return -1;
    }

    struct terp_args *a = malloc(sizeof(*a));
    a->path = strdup(path);
    a->terp_fd = fds[1];
    (*env)->ReleaseStringUTFChars(env, gamePath, path);

    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&tid, &attr, terp_thread, a) != 0) {
        LOGE("pthread_create failed");
        pthread_attr_destroy(&attr);
        close(fds[0]);
        close(fds[1]);
        free(a->path);
        free(a);
        return -1;
    }
    pthread_attr_destroy(&attr);

    return fds[0];   /* app end; Kotlin reads/writes JSON here */
}

/* byte[] nativeImage(int num) -> the blorb image bytes (PNG/JPEG), or null. */
JNIEXPORT jbyteArray JNICALL
Java_au_com_dangarmarine_jacl_GlkBridge_nativeImage(JNIEnv *env, jobject thiz, jint num)
{
    unsigned int len = 0;
    const void *data = jacl_bridge_image((unsigned int) num, &len);
    if (data == NULL || len == 0) return NULL;

    jbyteArray arr = (*env)->NewByteArray(env, (jsize) len);
    if (arr == NULL) return NULL;
    (*env)->SetByteArrayRegion(env, arr, 0, (jsize) len, (const jbyte *) data);
    return arr;
}

/* byte[] nativeSound(int num) -> the blorb sound bytes (Ogg/AIFF/MOD), or null. */
JNIEXPORT jbyteArray JNICALL
Java_au_com_dangarmarine_jacl_GlkBridge_nativeSound(JNIEnv *env, jobject thiz, jint num)
{
    unsigned int len = 0;
    const void *data = jacl_bridge_sound((unsigned int) num, &len);
    if (data == NULL || len == 0) return NULL;

    jbyteArray arr = (*env)->NewByteArray(env, (jsize) len);
    if (arr == NULL) return NULL;
    (*env)->SetByteArrayRegion(env, arr, 0, (jsize) len, (const jbyte *) data);
    return arr;
}

/* String nativeVersion() -> "J_VERSION.J_RELEASE.J_BUILD". */
JNIEXPORT jstring JNICALL
Java_au_com_dangarmarine_jacl_GlkBridge_nativeVersion(JNIEnv *env, jobject thiz)
{
    return (*env)->NewStringUTF(env, jacl_interpreter_version());
}

/* void nativeSetAutosaveSuppressed(boolean) -> suppress/allow the autosave that
 * fires when the game's socket closes. Kotlin sets it true before a Restart
 * closes the socket so the discarded game isn't autosaved over. */
JNIEXPORT void JNICALL
Java_au_com_dangarmarine_jacl_GlkBridge_nativeSetAutosaveSuppressed(JNIEnv *env, jobject thiz, jboolean suppressed)
{
    jacl_autosave_set_suppressed(suppressed ? 1 : 0);
}
