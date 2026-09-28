package dev.prismengine.runtime

import android.app.Activity
import android.content.Intent
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.os.Bundle
import android.view.Gravity
import android.view.View
import android.provider.OpenableColumns
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.TextView
import java.io.File

/**
 * Offline-first mobile editor shell for PRISM ENGINE. The panels and controls
 * are real Kotlin UI; the current release keeps edits in this Activity's
 * local session state. Native scene/terrain/mesh persistence and viewport
 * integration are intentionally not claimed yet.
 */
class PrismStudioActivity : Activity() {
    private val violet = Color.rgb(124, 58, 237)
    private val cyan = Color.rgb(0, 217, 255)
    private val gold = Color.rgb(255, 201, 60)
    private val magenta = Color.rgb(255, 61, 154)
    private val bgColor = Color.rgb(10, 10, 18)
    private val surface = Color.rgb(24, 24, 38)
    private val muted = Color.rgb(150, 150, 175)
    private val entities = mutableListOf("Ray", "Main Camera", "Sun Light", "Terrain")
    private val assets = mutableListOf("ray_character.prism", "spectrum_cube.mesh", "terrain.heightmap", "sky_gradient.mat")
    private lateinit var root: LinearLayout
    private lateinit var status: TextView
    private var activeTab = "Scene"
    private var selectedEntity = "Ray"

    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        @Suppress("DEPRECATION")
        window.decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_FULLSCREEN or
            View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
        root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(bgColor)
            setPadding(dp(12), dp(8), dp(12), dp(8))
        }
        root.addView(buildHeader())
        root.addView(buildTabs())
        status = label("Offline project · assets stored locally; scene edits are session-only", 11, muted)
        status.setPadding(dp(4), dp(7), dp(4), dp(4))
        root.addView(status)
        setContentView(root)
        showTab("Scene")
    }

    private fun buildHeader(): View = LinearLayout(this).apply {
        orientation = LinearLayout.HORIZONTAL
        gravity = Gravity.CENTER_VERTICAL
        val title = label("◈  PRISM STUDIO", 18, gold).apply { typeface = android.graphics.Typeface.DEFAULT_BOLD }
        addView(title, LinearLayout.LayoutParams(0, dp(44), 1f))
        addView(label("OFFLINE  •  APK ONLY", 10, cyan).apply { gravity = Gravity.CENTER_VERTICAL })
        addView(action("RUN", violet) { startActivity(Intent(this@PrismStudioActivity, PrismActivity::class.java)) })
    }

    private fun buildTabs(): View {
        val scroller = android.widget.HorizontalScrollView(this).apply { isHorizontalScrollBarEnabled = false }
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        listOf("Scene", "Assets", "Terrain", "Foliage", "Animation", "Modeling").forEach { tab ->
            row.addView(action(tab, if (tab == activeTab) violet else surface) { showTab(tab) })
        }
        scroller.addView(row)
        return scroller
    }

    private fun showTab(tab: String) {
        activeTab = tab
        root.removeViews(1, root.childCount - 1)
        root.addView(buildTabs(), 1)
        root.addView(status, 2)
        val content = when (tab) {
            "Assets" -> assetsPanel()
            "Terrain" -> terrainPanel()
            "Foliage" -> foliagePanel()
            "Animation" -> animationPanel()
            "Modeling" -> modelingPanel()
            else -> scenePanel()
        }
        root.addView(ScrollView(this).apply {
            isFillViewport = true
            addView(content)
        }, LinearLayout.LayoutParams(-1, 0, 1f))
    }

    private fun scenePanel(): View = column().apply {
        addView(section("SCENE VIEWPORT"))
        addView(EditorViewport(this@PrismStudioActivity), LinearLayout.LayoutParams(-1, dp(190)))
        addView(section("HIERARCHY  /  INSPECTOR"))
        addView(action("＋  Create Entity", violet) {
            val name = "Entity ${entities.size}"
            entities.add(name)
            selectedEntity = name
            showTab("Scene")
            message("Created $name in this editor session")
        })
        entities.forEach { entity ->
            addView(action(if (entity == selectedEntity) "◆  $entity" else "◇  $entity", if (entity == selectedEntity) surface else bgColor) {
                selectedEntity = entity
                showTab("Scene")
            })
        }
        addView(label("DETAILS · $selectedEntity", 14, cyan))
        addView(label("Transform", 12, gold))
        addView(vectorEditors("Position", listOf("0.0", "0.0", "0.0")))
        addView(vectorEditors("Rotation", listOf("0.0", "0.0", "0.0")))
        addView(vectorEditors("Scale", listOf("1.0", "1.0", "1.0")))
        addView(label("Components", 12, gold))
        listOf("Transform", "Mesh Renderer", "Collider").forEach { addView(action("▣  $it   ›", surface) { message("Inspector: $it selected") }) }
    }

    private fun assetsPanel(): View = column().apply {
        addView(section("CONTENT BROWSER"))
        addView(label("PROJECT  /  Assets", 13, cyan))
        addView(action("＋  Import from device…", violet) { chooseAsset() })
        addView(label("Offline asset shelf · ${assets.size} items", 11, muted))
        assets.forEachIndexed { i, file ->
            addView(action("${if (i % 2 == 0) "▧" else "◈"}   $file     ${if (file.endsWith("mesh")) "MESH" else if (file.endsWith("heightmap")) "TERRAIN" else "ASSET"}", surface) {
                message("Selected asset: $file")
            })
        }
        addView(EditorViewport(this@PrismStudioActivity), LinearLayout.LayoutParams(-1, dp(180)))
        addView(label("Import uses Android's local document picker; no upload or account.", 11, muted))
    }

    private fun terrainPanel(): View = column().apply {
        addView(section("LANDSCAPE · HEIGHTFIELD"))
        addView(EditorViewport(this@PrismStudioActivity), LinearLayout.LayoutParams(-1, dp(205)))
        addView(label("Brush", 13, cyan))
        listOf("Raise / Lower", "Smooth", "Flatten", "Erode").forEach { tool ->
            addView(action("◉  $tool", if (tool == "Raise / Lower") violet else surface) { message("Terrain tool selected: $tool · native Heightmap API is host-tested") })
        }
        addView(sliderRow("Brush radius", 1, 100, 24))
        addView(sliderRow("Strength", 1, 100, 50))
        addView(label("Sculpt controls are editor UI scaffolding; terrain changes are not yet persisted or rendered by the APK.", 11, muted))
    }

    private fun foliagePanel(): View = column().apply {
        addView(section("FOLIAGE PAINT"))
        addView(EditorViewport(this@PrismStudioActivity), LinearLayout.LayoutParams(-1, dp(205)))
        addView(label("Paintable types", 13, cyan))
        listOf("Pine tree", "Grass clump", "Rock", "Flower").forEach { type ->
            addView(action("▧  $type", surface) { message("Foliage type selected: $type") })
        }
        addView(sliderRow("Brush radius", 1, 100, 20))
        addView(sliderRow("Density", 0, 100, 65))
        addView(action("PAINT", violet) { message("Paint mode selected · FoliagePainter scatter is host-tested") })
        addView(action("ERASE", surface) { message("Erase mode selected") })
        addView(label("Foliage controls are UI scaffolding; placed instances are not yet serialized into the APK scene.", 11, muted))
    }

    private fun animationPanel(): View = column().apply {
        addView(section("ANIMATION EDITOR"))
        addView(EditorViewport(this@PrismStudioActivity), LinearLayout.LayoutParams(-1, dp(160)))
        addView(label("Skeleton · Ray_Rig", 13, cyan))
        listOf("Root", "Spine", "Arm_L", "Arm_R", "Leg_L", "Leg_R").forEach { bone ->
            addView(action("◇  $bone", surface) { message("Selected bone: $bone") })
        }
        addView(label("TIMELINE  ·  idle_loop", 12, gold))
        addView(sliderRow("Frame", 0, 60, 0))
        addView(action("▶  Play    ＋  Add Keyframe", violet) { message("Timeline preview · native animation clips and IK are host-tested") })
        addView(label("Timeline controls are a UI shell; skeletal playback is not yet wired to the renderer.", 11, muted))
    }

    private fun modelingPanel(): View = column().apply {
        addView(section("MODELING MODE"))
        addView(EditorViewport(this@PrismStudioActivity), LinearLayout.LayoutParams(-1, dp(205)))
        addView(label("Primitives", 13, cyan))
        listOf("Cube", "Sphere", "Cylinder", "Plane").forEach { primitive ->
            addView(action("＋  $primitive", surface) { message("Added $primitive to the local modeling selection") })
        }
        addView(label("Boolean operations", 13, gold))
        listOf("Union", "Subtract", "Intersect").forEach { operation ->
            addView(action("◇  $operation", violet) { message("$operation selected · mesh Boolean/CSG backend is not implemented") })
        }
        addView(label("Mesh Boolean/CSG operations are not yet implemented; these buttons document the intended mobile workflow.", 11, muted))
    }

    private fun vectorEditors(name: String, values: List<String>): View = LinearLayout(this).apply {
        orientation = LinearLayout.HORIZONTAL
        gravity = Gravity.CENTER_VERTICAL
        addView(label(name, 11, muted), LinearLayout.LayoutParams(dp(78), dp(40)))
        listOf("X", "Y", "Z").forEachIndexed { i, axis ->
            val edit = EditText(this@PrismStudioActivity).apply {
                setSingleLine(true)
                setText(values[i])
                textSize = 11f
                setTextColor(Color.WHITE)
                setHintTextColor(muted)
                setHint(axis)
                setPadding(dp(4), 0, dp(4), 0)
                setBackgroundColor(surface)
            }
            addView(edit, LinearLayout.LayoutParams(0, dp(40), 1f).apply { setMargins(dp(2), 0, dp(2), 0) })
        }
    }

    private fun sliderRow(title: String, min: Int, max: Int, initial: Int): View = LinearLayout(this).apply {
        orientation = LinearLayout.HORIZONTAL
        gravity = Gravity.CENTER_VERTICAL
        addView(label(title, 11, muted), LinearLayout.LayoutParams(dp(105), dp(42)))
        addView(SeekBar(this@PrismStudioActivity).apply {
            this.max = max - min
            progress = initial - min
            progressTintList = android.content.res.ColorStateList.valueOf(cyan)
            thumbTintList = android.content.res.ColorStateList.valueOf(violet)
        }, LinearLayout.LayoutParams(0, dp(42), 1f))
    }

    private fun column() = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        setPadding(0, dp(5), 0, dp(16))
    }

    private fun section(text: String) = label(text, 11, gold).apply {
        setPadding(dp(4), dp(12), dp(4), dp(8))
        typeface = android.graphics.Typeface.DEFAULT_BOLD
    }

    private fun label(text: String, sp: Int, color: Int) = TextView(this).apply {
        this.text = text
        textSize = sp.toFloat()
        setTextColor(color)
        setPadding(dp(4), dp(5), dp(4), dp(5))
    }

    private fun action(text: String, color: Int, click: () -> Unit) = Button(this).apply {
        this.text = text
        textSize = 11f
        isAllCaps = false
        setTextColor(Color.WHITE)
        setBackgroundTintList(android.content.res.ColorStateList.valueOf(color))
        setOnClickListener { click() }
    }

    private fun message(text: String) {
        status.text = "Offline project · $text"
    }

    private fun chooseAsset() {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "*/*"
        }
        @Suppress("DEPRECATION")
        startActivityForResult(intent, 104)
    }

    @Deprecated("Android document-picker callback")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != 104 || resultCode != RESULT_OK) return
        val uri = data?.data ?: return
        val displayName = contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
            ?.use { cursor -> if (cursor.moveToFirst()) cursor.getString(0) else null }
            ?: uri.lastPathSegment?.substringAfterLast(':')
            ?: "imported_asset"
        val safeName = displayName.replace(Regex("[^A-Za-z0-9._-]"), "_").take(120).ifBlank { "imported_asset" }
        val target = File(File(filesDir, "PrismProject/Assets").apply { mkdirs() }, safeName)
        try {
            contentResolver.openInputStream(uri)?.use { input ->
                target.outputStream().use { output -> input.copyTo(output) }
            } ?: throw IllegalStateException("Selected document could not be opened")
            val item = "$safeName  ·  ${target.length()} B"
            if (item !in assets) assets.add(item)
            showTab("Assets")
            message("Copied $safeName to private project storage · available offline")
        } catch (e: Exception) {
            message("Could not import $safeName: ${e.message ?: "read error"}")
        }
    }

    private fun dp(v: Int): Int = (v * resources.displayMetrics.density).toInt()

    /** Stylized, offline viewport placeholder: perspective grid + prism wireframe. */
    private inner class EditorViewport(ctx: PrismStudioActivity) : View(ctx) {
        private val p = Paint(Paint.ANTI_ALIAS_FLAG)
        override fun onDraw(canvas: Canvas) {
            super.onDraw(canvas)
            canvas.drawColor(Color.rgb(13, 14, 26))
            val w = width.toFloat(); val h = height.toFloat()
            val horizon = h * 0.46f
            p.color = Color.rgb(34, 35, 57); p.strokeWidth = dp(1).toFloat()
            canvas.drawLine(0f, horizon, w, horizon, p)
            for (i in -8..8) {
                val x = w * (i + 8) / 16f
                p.color = if (i == 0) violet else Color.rgb(38, 39, 61)
                canvas.drawLine(w * 0.5f, horizon, x, h, p)
            }
            for (i in 1..8) {
                val y = horizon + (h - horizon) * i * i / 64f
                p.color = Color.rgb(38, 39, 61)
                canvas.drawLine(0f, y, w, y, p)
            }
            val cx = w * 0.52f; val cy = h * 0.38f; val r = minOf(w, h) * 0.16f
            val path = Path().apply {
                moveTo(cx, cy - r); lineTo(cx + r, cy - r * 0.5f); lineTo(cx + r, cy + r * 0.65f)
                lineTo(cx, cy + r); lineTo(cx - r, cy + r * 0.5f); lineTo(cx - r, cy - r * 0.65f); close()
            }
            p.color = Color.argb(48, 124, 58, 237); p.style = Paint.Style.FILL
            canvas.drawPath(path, p)
            p.color = cyan; p.style = Paint.Style.STROKE; p.strokeWidth = dp(2).toFloat()
            canvas.drawPath(path, p)
            p.style = Paint.Style.FILL; p.textSize = dp(10).toFloat(); p.color = Color.WHITE
            canvas.drawText("PERSPECTIVE  ·  EDITOR PREVIEW", dp(8).toFloat(), dp(17).toFloat(), p)
        }
    }
}
