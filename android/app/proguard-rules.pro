# These methods are resolved by the fixed Java_org_beam_android_Native_* symbols
# in src/android/jni.cpp. Their class, names and signatures must remain stable.
-keep class org.beam.android.Native {
    native <methods>;
}
