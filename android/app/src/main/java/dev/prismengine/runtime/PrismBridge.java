package dev.prismengine.runtime;

/** JNI bridge. One C++ runtime, one offline Android APK. */
public final class PrismBridge {
    static { System.loadLibrary("prism_runtime"); }
    private PrismBridge() {}

    public static native void nativeCreate(String script);
    public static native void nativeSurfaceCreated();
    public static native void nativeResize(int width, int height);
    public static native void nativeFrame(float deltaSeconds);
    public static native void nativeTouch(int action, float x, float y);
    public static native String nativeStats();
    public static native void nativeDestroy();
}
