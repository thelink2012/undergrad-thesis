#include <jni.h>

extern "C" JNIEXPORT
jint JNICALL
Java_com_waldo_JNIExample_foo(
    JNIEnv* jni_env,
    jobject JNIExample_obj,
    jint bar)
{
  return bar * 2;
}
