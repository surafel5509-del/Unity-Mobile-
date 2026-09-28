package dev.prismengine.runtime;

import android.app.Activity;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.content.Context;
import android.widget.FrameLayout;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/** Prism engine sample host: GLES 3 scene with native C++ mixed 2D/3D simulation.
 *  No account, no INTERNET permission, no network calls. */
public final class PrismActivity extends Activity {
    private PrismView glView;
    private HudView hud;
    private long previousFrameNs;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,
                WindowManager.LayoutParams.FLAG_FULLSCREEN);
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        String source = "print(\"PRISM sample booted\");";
        try (InputStream in = getAssets().open("sample.prism")) {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[4096]; int n;
            while ((n = in.read(buf)) != -1) out.write(buf, 0, n);
            source = out.toString("UTF-8");
        } catch (IOException e) { android.util.Log.e("Prism", "sample script missing", e); }
        PrismBridge.nativeCreate(source);
        FrameLayout frame = new FrameLayout(this);
        glView = new PrismView(this);
        hud = new HudView(this);
        frame.addView(glView);
        frame.addView(hud);
        setContentView(frame);
    }

    @Override protected void onPause() { super.onPause(); if (glView != null) glView.onPause(); }
    @Override protected void onResume() { super.onResume(); if (glView != null) glView.onResume(); }
    @Override protected void onDestroy() {
        if (glView != null) {
            glView.queueEvent(PrismBridge::nativeDestroy);
        }
        super.onDestroy();
    }

    private final class PrismView extends GLSurfaceView implements GLSurfaceView.Renderer {
        PrismView(Context context) {
            super(context);
            setEGLContextClientVersion(3);
            setEGLConfigChooser(8, 8, 8, 8, 24, 8);
            setRenderer(this);
            setRenderMode(GLSurfaceView.RENDERMODE_CONTINUOUSLY);
        }
        @Override public void onSurfaceCreated(GL10 unused, EGLConfig config) {
            previousFrameNs = SystemClock.elapsedRealtimeNanos();
            PrismBridge.nativeSurfaceCreated();
        }
        @Override public void onSurfaceChanged(GL10 unused, int w, int h) { PrismBridge.nativeResize(w, h); }
        @Override public void onDrawFrame(GL10 unused) {
            long now = SystemClock.elapsedRealtimeNanos();
            float dt = Math.min((now - previousFrameNs) / 1_000_000_000.0f, 0.05f);
            previousFrameNs = now;
            PrismBridge.nativeFrame(dt);
            hud.postInvalidateOnAnimation();
        }
        @Override public boolean onTouchEvent(MotionEvent e) {
            final int action = e.getActionMasked();
            final int index = e.getActionIndex();
            if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
                final float x = e.getX(index) / getWidth(), y = e.getY(index) / getHeight();
                queueEvent(() -> PrismBridge.nativeTouch(0, x, y));
            } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) {
                final float x = e.getX(index) / getWidth(), y = e.getY(index) / getHeight();
                queueEvent(() -> PrismBridge.nativeTouch(2, x, y));
            } else if (action == MotionEvent.ACTION_MOVE) {
                final float x = e.getX(0) / getWidth(), y = e.getY(0) / getHeight();
                queueEvent(() -> PrismBridge.nativeTouch(1, x, y));
            }
            return true;
        }
    }

    /** Canvas HUD for readable text; graphics and physics remain native. */
    private static final class HudView extends View {
        private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        HudView(Context ctx) { super(ctx); setWillNotDraw(false); }
        @Override protected void onDraw(Canvas c) {
            super.onDraw(c);
            float d = getResources().getDisplayMetrics().density;
            p.setTypeface(Typeface.create("sans-serif-medium", Typeface.BOLD));
            p.setTextSize(20 * d);
            p.setColor(Color.rgb(255, 201, 60));
            c.drawText("◈  PRISM ENGINE", 20*d, 32*d, p);
            p.setTypeface(Typeface.MONOSPACE); p.setTextSize(12*d);
            p.setColor(Color.rgb(0, 217, 255));
            c.drawText("2D + 3D  •  Offline APK", 20*d, 50*d, p);
            p.setColor(Color.WHITE);
            c.drawText("◀  MOVE", 26*d, getHeight()-24*d, p);
            c.drawText("JUMP  ▶", getWidth()-110*d, getHeight()-24*d, p);
            p.setColor(Color.rgb(255, 61, 154)); p.setTextSize(10*d);
            c.drawText(PrismBridge.nativeStats(), getWidth()-260*d, 24*d, p);
        }
        @Override public boolean onTouchEvent(MotionEvent e) { return false; }
    }
}
