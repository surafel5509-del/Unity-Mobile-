package dev.prismengine.runtime

/**
 * JNI bridge. One C++ runtime, one offline Android APK.
 *
 * The package + class name are load-bearing: the native library exports
 * `Java_dev_prismengine_runtime_PrismBridge_*` symbols that the JVM resolves
 * against exactly this class. Keep the name and package stable, and keep the
 * methods `@JvmStatic` so the second JNI argument stays `jclass`.
 */
object PrismBridge {
    init {
        System.loadLibrary("prism_runtime")
    }

    @JvmStatic external fun nativeCreate(script: String)
    @JvmStatic external fun nativeSurfaceCreated()
    @JvmStatic external fun nativeResize(width: Int, height: Int)
    @JvmStatic external fun nativeFrame(deltaSeconds: Float)
    @JvmStatic external fun nativeTouch(action: Int, x: Float, y: Float)
    @JvmStatic external fun nativeStats(): String
    @JvmStatic external fun nativeDestroy()
}
