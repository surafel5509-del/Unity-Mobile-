package dev.prismengine.runtime

import android.app.Activity
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Typeface
import android.content.Intent
import android.opengl.GLSurfaceView
import android.os.Bundle
import android.os.SystemClock
import android.util.Log
import android.view.MotionEvent
import android.view.View
import android.view.WindowManager
import android.widget.FrameLayout
import android.widget.Button
import android.view.Gravity
import java.io.File
import org.json.JSONObject
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10
import kotlin.math.min

/**
 * Prism engine sample host: a GLES 3 surface driven by the native C++ mixed
 * 2D/3D simulation, plus a lightweight Canvas HUD. No account, no INTERNET
 * permission, no network calls — everything runs offline from the APK.
 */
class PrismActivity : Activity() {

    private var glView: PrismView? = null
    private var hud: HudView? = null
    private var previousFrameNs = 0L

    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        window.setFlags(
            WindowManager.LayoutParams.FLAG_FULLSCREEN,
            WindowManager.LayoutParams.FLAG_FULLSCREEN,
        )
        @Suppress("DEPRECATION")
        window.decorView.systemUiVisibility =
            View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY

        var source = "print(\"PRISM sample booted\");"
        val projectRoot = intent.getStringExtra(PrismStudioActivity.EXTRA_PROJECT_ROOT)?.let(::File)
        var loadedProjectScript = false
        if (projectRoot != null) {
            try {
                val manifest = JSONObject(File(projectRoot, "project.prism.json").readText())
                val relativeScript = manifest.optString("script", "scripts/main.prism")
                val script = File(projectRoot, relativeScript)
                if (script.canonicalPath.startsWith(projectRoot.canonicalPath + File.separator) && script.isFile) {
                    source = script.readText()
                    loadedProjectScript = true
                }
            } catch (e: Exception) {
                Log.e("Prism", "project script unavailable; using bundled fallback", e)
            }
        }
        if (!loadedProjectScript) try {
            assets.open("sample.prism").use { input -> source = input.readBytes().toString(Charsets.UTF_8) }
        } catch (e: Exception) {
            Log.e("Prism", "sample script missing", e)
        }
        PrismBridge.nativeCreate(source)

        val frame = FrameLayout(this)
        val view = PrismView(this)
        val hudView = HudView(this)
        glView = view
        hud = hudView
        frame.addView(view)
        frame.addView(hudView)
        val studio = Button(this).apply {
            text = "STUDIO"
            textSize = 11f
            isAllCaps = false
            setTextColor(android.graphics.Color.WHITE)
            backgroundTintList = android.content.res.ColorStateList.valueOf(android.graphics.Color.rgb(124, 58, 237))
            setOnClickListener {
                startActivity(Intent(this@PrismActivity, PrismStudioActivity::class.java)
                    .putExtra(PrismStudioActivity.EXTRA_PROJECT_ROOT, intent.getStringExtra(PrismStudioActivity.EXTRA_PROJECT_ROOT)))
            }
        }
        frame.addView(studio, FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.WRAP_CONTENT,
            FrameLayout.LayoutParams.WRAP_CONTENT,
            Gravity.TOP or Gravity.END,
        ).apply {
            val d = resources.displayMetrics.density
            setMargins(0, (8 * d).toInt(), (12 * d).toInt(), 0)
        })
        setContentView(frame)
    }

    override fun onPause() {
        super.onPause()
        glView?.onPause()
    }

    override fun onResume() {
        super.onResume()
        glView?.onResume()
    }

    override fun onDestroy() {
        glView?.queueEvent { PrismBridge.nativeDestroy() }
        super.onDestroy()
    }

    private inner class PrismView(context: Context) :
        GLSurfaceView(context), GLSurfaceView.Renderer {

        init {
            setEGLContextClientVersion(3)
            setEGLConfigChooser(8, 8, 8, 8, 24, 8)
            setRenderer(this)
            renderMode = RENDERMODE_CONTINUOUSLY
        }

        override fun onSurfaceCreated(unused: GL10?, config: EGLConfig?) {
            previousFrameNs = SystemClock.elapsedRealtimeNanos()
            PrismBridge.nativeSurfaceCreated()
        }

        override fun onSurfaceChanged(unused: GL10?, w: Int, h: Int) {
            PrismBridge.nativeResize(w, h)
        }

        override fun onDrawFrame(unused: GL10?) {
            val now = SystemClock.elapsedRealtimeNanos()
            val dt = min((now - previousFrameNs) / 1_000_000_000.0f, 0.05f)
            previousFrameNs = now
            PrismBridge.nativeFrame(dt)
            hud?.postInvalidateOnAnimation()
        }

        override fun onTouchEvent(e: MotionEvent): Boolean {
            val index = e.actionIndex
            when (e.actionMasked) {
                MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                    val x = e.getX(index) / width
                    val y = e.getY(index) / height
                    queueEvent { PrismBridge.nativeTouch(0, x, y) }
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                    val x = e.getX(index) / width
                    val y = e.getY(index) / height
                    queueEvent { PrismBridge.nativeTouch(2, x, y) }
                }
                MotionEvent.ACTION_MOVE -> {
                    val x = e.getX(0) / width
                    val y = e.getY(0) / height
                    queueEvent { PrismBridge.nativeTouch(1, x, y) }
                }
            }
            return true
        }
    }

    /** Canvas HUD for readable text; graphics and physics remain native. */
    private class HudView(ctx: Context) : View(ctx) {
        private val paint = Paint(Paint.ANTI_ALIAS_FLAG)

        init {
            setWillNotDraw(false)
        }

        override fun onDraw(c: Canvas) {
            super.onDraw(c)
            val d = resources.displayMetrics.density
            paint.typeface = Typeface.create("sans-serif-medium", Typeface.BOLD)
            paint.textSize = 20 * d
            paint.color = Color.rgb(255, 201, 60)
            c.drawText("◈  PRISM ENGINE", 20 * d, 32 * d, paint)

            paint.typeface = Typeface.MONOSPACE
            paint.textSize = 12 * d
            paint.color = Color.rgb(0, 217, 255)
            c.drawText("2D + 3D  •  Offline APK", 20 * d, 50 * d, paint)

            paint.color = Color.WHITE
            c.drawText("◀  MOVE", 26 * d, height - 24 * d, paint)
            c.drawText("JUMP  ▶", width - 110 * d, height - 24 * d, paint)

            paint.color = Color.rgb(255, 61, 154)
            paint.textSize = 10 * d
            c.drawText(PrismBridge.nativeStats(), width - 260 * d, 24 * d, paint)
        }

        @Suppress("ClickableViewAccessibility")
        override fun onTouchEvent(event: MotionEvent): Boolean = false
    }
}
