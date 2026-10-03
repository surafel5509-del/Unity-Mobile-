package dev.prismengine.runtime

import android.app.Activity
import android.content.Intent
import android.graphics.Bitmap
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
    private val entities = mutableListOf<String>()
    private val assets = mutableListOf<String>()
    private lateinit var projectDir: File
    private var projectName = "New Prism Game"
    private lateinit var root: LinearLayout
    private lateinit var status: TextView
    private var activeTab = "Scene"
    private var selectedEntity = "Ray"
    private var touchDeadZone = 12

    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        @Suppress("DEPRECATION")
        window.decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_FULLSCREEN or
            View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
        projectDir = intent.getStringExtra(EXTRA_PROJECT_ROOT)?.let(::File)
            ?: File(filesDir, "PrismProjects/QuickStart").apply { mkdirs() }
        projectDir.mkdirs()
        projectName = try {
            val info = org.json.JSONObject(File(projectDir, "project.prism.json").readText())
            info.optString("name", info.optString("Name", projectDir.name))
        } catch (_: Exception) { projectDir.name }
        loadProjectData()
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
        addView(label("${projectName.uppercase()}  ·  OFFLINE", 10, cyan).apply { gravity = Gravity.CENTER_VERTICAL })
        addView(action("HUB", surface) { finish() })
        addView(action("RUN", violet) {
            startActivity(Intent(this@PrismStudioActivity, PrismActivity::class.java)
                .putExtra(EXTRA_PROJECT_ROOT, projectDir.absolutePath))
        })
    }

    private fun buildTabs(): View {
        val scroller = android.widget.HorizontalScrollView(this).apply { isHorizontalScrollBarEnabled = false }
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        listOf("Scene", "UI", "Texture", "Controls", "Script", "Model", "Assets", "Terrain", "Foliage", "Animation", "Modeling").forEach { tab ->
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
            "UI" -> uiBuilderPanel()
            "Texture" -> textureEditorPanel()
            "Controls" -> controlsEditorPanel()
            "Script" -> scriptEditorPanel()
            "Model" -> modelEditorPanel()
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
            appendSceneEntity(name)
            selectedEntity = name
            showTab("Scene")
            message("Created $name and saved the scene locally")
        })
        entities.forEach { entity ->
            addView(action(if (entity == selectedEntity) "◆  $entity" else "◇  $entity", if (entity == selectedEntity) surface else bgColor) {
                selectedEntity = entity
                showTab("Scene")
            })
        }
        addView(label("DETAILS · $selectedEntity", 14, cyan))
        addView(label("Transform", 12, gold))
        addView(vectorEditors("Position", entityVector("position", listOf("0", "0", "0")), "position"))
        addView(vectorEditors("Rotation", entityVector("rotation", listOf("0", "0", "0")), "rotation"))
        addView(vectorEditors("Scale", entityVector("scale", listOf("1", "1", "1")), "scale"))
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

    private fun uiBuilderPanel(): View = column().apply {
        addView(section("VISUAL UI BUILDER  ·  1920 × 1080 SAFE AREA"))
        val nodes = loadUiNodes()
        addView(UIBuilderCanvas(this@PrismStudioActivity, nodes), LinearLayout.LayoutParams(-1, dp(230)))
        addView(label("Component palette · tap to add to the saved layout", 12, cyan))
        listOf("Panel", "Label", "Button", "Image", "Progress Bar", "Touch Stick").forEach { type ->
            addView(action("＋  $type", surface) {
                val index = nodes.size
                nodes.add(org.json.JSONObject()
                    .put("type", type).put("name", "${type.replace(" ", "")}_$index")
                    .put("x", 0.12 + (index % 4) * 0.12).put("y", 0.22 + (index % 3) * 0.16)
                    .put("width", 0.24).put("height", 0.10).put("text", if (type == "Button") "Button" else type))
                saveUiNodes(nodes)
                showTab("UI")
                message("Added $type to ui/main.ui.json")
            })
        }
        addView(action("SAVE LAYOUT", violet) { saveUiNodes(nodes); message("UI layout saved to ui/main.ui.json") })
        addView(label("Layouts persist as editable JSON in the selected project. Drag/reflow handles are the next UI-tool integration step.", 11, muted))
    }

    private fun textureEditorPanel(): View = column().apply {
        addView(section("2D TEXTURE PAINTER  ·  LOCAL PNG SOURCE"))
        val imageFile = File(projectDir, "assets/textures/painted_texture.png")
        val bitmap = if (imageFile.isFile) android.graphics.BitmapFactory.decodeFile(imageFile.absolutePath)
            ?: Bitmap.createBitmap(128, 128, Bitmap.Config.ARGB_8888)
            else Bitmap.createBitmap(128, 128, Bitmap.Config.ARGB_8888).apply { eraseColor(Color.rgb(30, 30, 48)) }
        val canvasView = TextureCanvas(this@PrismStudioActivity, bitmap)
        addView(canvasView, LinearLayout.LayoutParams(-1, dp(250)))
        addView(label("Tap and paint · color", 12, cyan))
        val palette = LinearLayout(this@PrismStudioActivity).apply { orientation = LinearLayout.HORIZONTAL }
        listOf(violet, cyan, gold, magenta, Color.WHITE, Color.BLACK).forEachIndexed { i, color ->
            palette.addView(action(listOf("Violet", "Cyan", "Gold", "Magenta", "White", "Ink")[i], color) {
                canvasView.paintColor = color
                message("Texture brush color selected")
            }, LinearLayout.LayoutParams(0, dp(44), 1f))
        }
        addView(palette)
        addView(sliderRow("Brush size", 1, 24, 6) { canvasView.brushRadius = it; canvasView.invalidate() })
        addView(action("SAVE PNG TO PROJECT", violet) {
            imageFile.parentFile?.mkdirs()
            imageFile.outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
            refreshAssets()
            message("Saved ${imageFile.relativeTo(projectDir).invariantSeparatorsPath}")
        })
        addView(label("The PNG is a real RGBA texture source on disk. GPU preview/material assignment is not yet wired to the renderer.", 11, muted))
    }

    private fun controlsEditorPanel(): View = column().apply {
        addView(section("PRO TOUCH-CONTROL EDITOR  ·  LANDSCAPE"))
        val controls = loadControls()
        addView(ControlPreview(this@PrismStudioActivity, controls), LinearLayout.LayoutParams(-1, dp(230)))
        listOf("Virtual Stick", "Action Button", "Jump", "Pause", "Interact").forEach { type ->
            addView(action("＋  Add $type", surface) {
                val idx = controls.length()
                controls.put(org.json.JSONObject().put("type", type).put("name", "${type.replace(" ", "")}_$idx")
                    .put("x", if (type == "Virtual Stick") 0.16 else 0.84)
                    .put("y", 0.82).put("radius", if (type == "Virtual Stick") 0.13 else 0.07)
                    .put("action", type.lowercase().replace(" ", "_")))
                saveControls(controls)
                showTab("Controls")
                message("$type saved in controls/touch.json")
            })
        }
        addView(sliderRow("Touch dead zone", 0, 60, loadDeadZone()) {
            touchDeadZone = it
            saveControls(controls)
        })
        addView(action("SAVE CONTROL PROFILE", violet) { saveControls(controls); message("Control profile saved to controls/touch.json") })
        addView(label("Control positions/actions persist locally; native input-profile loading and in-game overlay binding are future wiring.", 11, muted))
    }

    private fun scriptEditorPanel(): View = column().apply {
        addView(section("PRISMSCRIPT  ·  OFFLINE SOURCE EDITOR"))
        val sourceFile = projectScriptFile()
        val code = EditText(this@PrismStudioActivity).apply {
            setText(sourceFile.readText())
            setTextColor(Color.rgb(220, 225, 245))
            setHintTextColor(muted)
            textSize = 12f
            typeface = android.graphics.Typeface.MONOSPACE
            gravity = Gravity.TOP or Gravity.START
            inputType = android.text.InputType.TYPE_CLASS_TEXT or android.text.InputType.TYPE_TEXT_FLAG_MULTI_LINE or android.text.InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
            minLines = 14
            setPadding(dp(10), dp(10), dp(10), dp(10))
            setBackgroundColor(Color.rgb(16, 17, 29))
        }
        addView(code, LinearLayout.LayoutParams(-1, dp(330)))
        addView(action("SAVE SCRIPT", surface) {
            sourceFile.parentFile?.mkdirs(); sourceFile.writeText(code.text.toString()); message("Saved ${sourceFile.relativeTo(projectDir).invariantSeparatorsPath}")
        })
        addView(action("VALIDATE WITH NATIVE PRISMSCRIPT PARSER", violet) {
            sourceFile.parentFile?.mkdirs(); sourceFile.writeText(code.text.toString())
            val result = PrismBridge.nativeValidateScript(code.text.toString())
            message(result)
        })
        addView(label("Validation invokes the native lexer/parser without executing project code. Runtime gameplay bindings remain under development.", 11, muted))
    }

    private fun modelEditorPanel(): View = column().apply {
        addView(section("LOW-POLY MODEL SOURCE TOOL"))
        addView(EditorViewport(this@PrismStudioActivity), LinearLayout.LayoutParams(-1, dp(190)))
        addView(label("Create editable Wavefront OBJ source meshes", 12, cyan))
        listOf("Cube", "Pyramid", "Plane", "Prism Pickup").forEach { kind ->
            addView(action("＋  Create $kind", surface) {
                val file = writePrimitiveObj(kind)
                refreshAssets()
                message("Created source mesh ${file.relativeTo(projectDir).invariantSeparatorsPath}")
            })
        }
        addView(label("Meshes are real editable OBJ source files. Subdivision, rigging, UV unwrapping and mesh Boolean/CSG are not implemented yet.", 11, muted))
    }

    private fun loadProjectData() {
        val sceneFile = File(projectDir, "scenes/main.scene.json")
        entities.clear()
        try {
            val rows = org.json.JSONObject(sceneFile.readText()).optJSONArray("entities")
            if (rows != null) for (i in 0 until rows.length()) entities.add(rows.optJSONObject(i)?.optString("name") ?: "Entity $i")
        } catch (_: Exception) { }
        if (entities.isNotEmpty() && selectedEntity !in entities) selectedEntity = entities.first()
        val script = projectScriptFile()
        if (!script.exists()) { script.parentFile?.mkdirs(); script.writeText("// $projectName\nfunc start() { print(\"$projectName ready\"); }\nstart();\n") }
        File(projectDir, "assets").mkdirs()
        File(projectDir, "ui").mkdirs()
        File(projectDir, "controls").mkdirs()
        refreshAssets()
    }

    private fun entityVector(key: String, fallback: List<String>): List<String> {
        return try {
            val rows = org.json.JSONObject(File(projectDir, "scenes/main.scene.json").readText()).optJSONArray("entities") ?: return fallback
            val entity = (0 until rows.length()).mapNotNull { rows.optJSONObject(it) }.firstOrNull { it.optString("name") == selectedEntity } ?: return fallback
            val values = entity.optJSONArray(key) ?: return fallback
            (0..2).map { i -> if (i < values.length()) values.optDouble(i, 0.0).toString() else fallback[i] }
        } catch (_: Exception) { fallback }
    }

    private fun saveTransform(key: String, values: List<Double>) {
        try {
            val file = File(projectDir, "scenes/main.scene.json")
            val scene = org.json.JSONObject(file.readText())
            val rows = scene.optJSONArray("entities") ?: return
            val entity = (0 until rows.length()).mapNotNull { rows.optJSONObject(it) }.firstOrNull { it.optString("name") == selectedEntity } ?: return
            val vector = entity.optJSONArray(key) ?: org.json.JSONArray()
            for (i in values.indices) vector.put(i, values[i])
            if (key == "rotation" && vector.length() < 4) vector.put(3, 1.0)
            entity.put(key, vector)
            file.writeText(scene.toString(2) + "\n")
            status.text = "Saved $key for $selectedEntity · offline project"
        } catch (_: Exception) { }
    }

    private fun refreshAssets() {
        assets.clear()
        val dir = File(projectDir, "assets")
        if (dir.isDirectory) dir.walkTopDown().filter { it.isFile }.forEach {
            assets.add(it.relativeTo(projectDir).invariantSeparatorsPath)
        }
    }

    private fun projectScriptFile(): File {
        val manifest = try { org.json.JSONObject(File(projectDir, "project.prism.json").readText()) } catch (_: Exception) { org.json.JSONObject() }
        val relative = manifest.optString("script", "scripts/main.prism")
        return File(projectDir, relative)
    }

    private fun appendSceneEntity(name: String) {
        val file = File(projectDir, "scenes/main.scene.json")
        val scene = try { org.json.JSONObject(file.readText()) } catch (_: Exception) { org.json.JSONObject().put("version", 1).put("name", "Main") }
        val rows = scene.optJSONArray("entities") ?: org.json.JSONArray()
        rows.put(org.json.JSONObject().put("id", "entity-${System.currentTimeMillis()}").put("name", name)
            .put("position", org.json.JSONArray(listOf(0, 0, 0))).put("rotation", org.json.JSONArray(listOf(0, 0, 0, 1)))
            .put("scale", org.json.JSONArray(listOf(1, 1, 1))).put("components", org.json.JSONObject().put("Transform", org.json.JSONObject().put("enabled", true))))
        scene.put("entities", rows)
        file.parentFile?.mkdirs(); file.writeText(scene.toString(2) + "\n")
    }

    private fun loadUiNodes(): MutableList<org.json.JSONObject> {
        val file = File(projectDir, "ui/main.ui.json")
        return try {
            val rows = org.json.JSONObject(file.readText()).optJSONArray("nodes") ?: org.json.JSONArray()
            MutableList(rows.length()) { rows.getJSONObject(it) }
        } catch (_: Exception) { mutableListOf() }
    }

    private fun saveUiNodes(nodes: List<org.json.JSONObject>) {
        val rows = org.json.JSONArray()
        nodes.forEach { rows.put(it) }
        val file = File(projectDir, "ui/main.ui.json"); file.parentFile?.mkdirs()
        file.writeText(org.json.JSONObject().put("version", 1).put("canvas", org.json.JSONObject().put("width", 1920).put("height", 1080).put("safeArea", true)).put("nodes", rows).toString(2) + "\n")
    }

    private fun loadControlDocument(): org.json.JSONObject {
        return try { org.json.JSONObject(File(projectDir, "controls/touch.json").readText()) }
        catch (_: Exception) { org.json.JSONObject() }
    }

    private fun loadControls(): org.json.JSONArray {
        val doc = loadControlDocument()
        touchDeadZone = doc.optInt("deadZone", 12)
        return doc.optJSONArray("controls") ?: org.json.JSONArray()
    }

    private fun loadDeadZone(): Int = loadControlDocument().optInt("deadZone", 12)

    private fun saveControls(controls: org.json.JSONArray) {
        val file = File(projectDir, "controls/touch.json"); file.parentFile?.mkdirs()
        file.writeText(org.json.JSONObject().put("version", 1).put("layout", "landscape")
            .put("safeArea", true).put("deadZone", touchDeadZone).put("controls", controls).toString(2) + "\n")
    }

    private fun writePrimitiveObj(kind: String): File {
        val dir = File(projectDir, "assets/models").apply { mkdirs() }
        val safe = kind.lowercase().replace(Regex("[^a-z0-9]+"), "_").trim('_')
        val file = File(dir, "${safe}_${System.currentTimeMillis()}.obj")
        val source = when (kind) {
            "Plane" -> "# PRISM source mesh · $kind\no Plane\nv -1 0 -1\nv 1 0 -1\nv 1 0 1\nv -1 0 1\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nf 1/1 2/2 3/3 4/4\n"
            "Pyramid", "Prism Pickup" -> "# PRISM source mesh · $kind\no $safe\nv -0.7 0 -0.7\nv 0.7 0 -0.7\nv 0.7 0 0.7\nv -0.7 0 0.7\nv 0 1.3 0\nf 1 2 5\nf 2 3 5\nf 3 4 5\nf 4 1 5\nf 4 3 2 1\n"
            else -> "# PRISM source mesh · $kind\no Cube\nv -0.5 -0.5 -0.5\nv 0.5 -0.5 -0.5\nv 0.5 0.5 -0.5\nv -0.5 0.5 -0.5\nv -0.5 -0.5 0.5\nv 0.5 -0.5 0.5\nv 0.5 0.5 0.5\nv -0.5 0.5 0.5\nf 1 2 3 4\nf 5 8 7 6\nf 1 5 6 2\nf 2 6 7 3\nf 3 7 8 4\nf 5 1 4 8\n"
        }
        file.writeText(source)
        return file
    }

    private inner class UIBuilderCanvas(ctx: PrismStudioActivity, private val nodes: List<org.json.JSONObject>) : View(ctx) {
        private val p = Paint(Paint.ANTI_ALIAS_FLAG)
        override fun onDraw(c: Canvas) {
            c.drawColor(Color.rgb(18, 19, 32))
            p.color = Color.rgb(52, 54, 78); p.style = Paint.Style.STROKE; p.strokeWidth = dp(1).toFloat()
            for (i in 1..7) c.drawLine(width * i / 8f, 0f, width * i / 8f, height.toFloat(), p)
            for (i in 1..4) c.drawLine(0f, height * i / 5f, width.toFloat(), height * i / 5f, p)
            p.style = Paint.Style.FILL
            nodes.forEachIndexed { i, n ->
                val x = (n.optDouble("x", .1) * width).toFloat(); val y = (n.optDouble("y", .2) * height).toFloat()
                val w = (n.optDouble("width", .22) * width).toFloat(); val h = (n.optDouble("height", .1) * height).toFloat()
                p.color = if (i % 2 == 0) Color.argb(180, 124, 58, 237) else Color.argb(190, 0, 115, 145)
                c.drawRoundRect(x, y, x + w, y + h, dp(5).toFloat(), dp(5).toFloat(), p)
                p.color = Color.WHITE; p.textSize = dp(9).toFloat(); c.drawText(n.optString("text", n.optString("name", "UI")), x + dp(4), y + h * .65f, p)
            }
            p.color = gold; p.textSize = dp(10).toFloat(); c.drawText("LIVE CANVAS PREVIEW  ·  ${nodes.size} COMPONENTS", dp(8).toFloat(), dp(16).toFloat(), p)
        }
    }

    private inner class TextureCanvas(ctx: PrismStudioActivity, private val bitmap: Bitmap) : View(ctx) {
        var paintColor = violet
        var brushRadius = 3
        private val brush = Paint(Paint.ANTI_ALIAS_FLAG)
        override fun onDraw(c: Canvas) {
            c.drawColor(Color.rgb(29, 29, 42))
            val side = minOf(width, height).toFloat()
            val x = (width - side) * .5f; val y = (height - side) * .5f
            c.drawBitmap(bitmap, null, android.graphics.RectF(x, y, x + side, y + side), brush)
            brush.color = Color.argb(60, 255, 255, 255); brush.style = Paint.Style.STROKE; brush.strokeWidth = 1f
            for (i in 1..16) { c.drawLine(x + side * i / 16, y, x + side * i / 16, y + side, brush); c.drawLine(x, y + side * i / 16, x + side, y + side * i / 16, brush) }
            brush.style = Paint.Style.FILL
        }
        @Deprecated("Touch painting canvas")
        override fun onTouchEvent(event: android.view.MotionEvent): Boolean {
            if (event.action != android.view.MotionEvent.ACTION_DOWN && event.action != android.view.MotionEvent.ACTION_MOVE) return true
            val side = minOf(width, height).toFloat(); val ox = (width - side) * .5f; val oy = (height - side) * .5f
            val px = ((event.x - ox) / side * bitmap.width).toInt().coerceIn(0, bitmap.width - 1)
            val py = ((event.y - oy) / side * bitmap.height).toInt().coerceIn(0, bitmap.height - 1)
            val radius = brushRadius
            for (yy in (py - radius).coerceAtLeast(0)..(py + radius).coerceAtMost(bitmap.height - 1))
                for (xx in (px - radius).coerceAtLeast(0)..(px + radius).coerceAtMost(bitmap.width - 1)) bitmap.setPixel(xx, yy, paintColor)
            invalidate(); return true
        }
    }

    private inner class ControlPreview(ctx: PrismStudioActivity, private val controls: org.json.JSONArray) : View(ctx) {
        private val p = Paint(Paint.ANTI_ALIAS_FLAG)
        override fun onDraw(c: Canvas) {
            c.drawColor(Color.rgb(15, 17, 28))
            p.color = Color.rgb(54, 56, 76); p.style = Paint.Style.STROKE
            c.drawRect(1f, 1f, width - 1f, height - 1f, p)
            for (i in 0 until controls.length()) {
                val item = controls.optJSONObject(i) ?: continue
                val x = (item.optDouble("x", .8) * width).toFloat(); val y = (item.optDouble("y", .8) * height).toFloat()
                val r = (item.optDouble("radius", .07) * minOf(width, height)).toFloat()
                p.color = Color.argb(75, 0, 217, 255); p.style = Paint.Style.FILL; c.drawCircle(x, y, r, p)
                p.color = cyan; p.style = Paint.Style.STROKE; c.drawCircle(x, y, r, p)
                p.style = Paint.Style.FILL; p.textSize = dp(9).toFloat(); c.drawText(item.optString("name", "Control"), x - r, y + dp(4), p)
            }
            p.color = gold; p.textSize = dp(10).toFloat(); c.drawText("TOUCH SAFE-AREA PREVIEW", dp(8).toFloat(), dp(16).toFloat(), p)
        }
    }

    private fun vectorEditors(name: String, values: List<String>, key: String): View = LinearLayout(this).apply {
        orientation = LinearLayout.HORIZONTAL
        gravity = Gravity.CENTER_VERTICAL
        val edits = mutableListOf<EditText>()
        addView(label(name, 11, muted), LinearLayout.LayoutParams(dp(78), dp(40)))
        listOf("X", "Y", "Z").forEachIndexed { i, axis ->
            val edit = EditText(this@PrismStudioActivity).apply {
                setSingleLine(true)
                setText(values.getOrElse(i) { "0" })
                textSize = 11f
                setTextColor(Color.WHITE)
                setHintTextColor(muted)
                setHint(axis)
                setPadding(dp(4), 0, dp(4), 0)
                setBackgroundColor(surface)
            }
            edits.add(edit)
            addView(edit, LinearLayout.LayoutParams(0, dp(40), 1f).apply { setMargins(dp(2), 0, dp(2), 0) })
        }
        edits.forEach { edit -> edit.addTextChangedListener(SimpleTextWatcher {
            val parsed = edits.map { it.text.toString().toDoubleOrNull() }
            if (parsed.all { it != null }) saveTransform(key, parsed.map { it!! })
        }) }
    }

    private fun sliderRow(title: String, min: Int, max: Int, initial: Int, onChange: (Int) -> Unit = {}): View = LinearLayout(this).apply {
        orientation = LinearLayout.HORIZONTAL
        gravity = Gravity.CENTER_VERTICAL
        addView(label(title, 11, muted), LinearLayout.LayoutParams(dp(105), dp(42)))
        val seek = SeekBar(this@PrismStudioActivity).apply {
            this.max = max - min
            progress = initial.coerceIn(min, max) - min
            progressTintList = android.content.res.ColorStateList.valueOf(cyan)
            thumbTintList = android.content.res.ColorStateList.valueOf(violet)
        }
        seek.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(bar: SeekBar?, progress: Int, fromUser: Boolean) { if (fromUser) onChange(min + progress) }
            override fun onStartTrackingTouch(bar: SeekBar?) = Unit
            override fun onStopTrackingTouch(bar: SeekBar?) = Unit
        })
        addView(seek, LinearLayout.LayoutParams(0, dp(42), 1f))
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
        val target = File(File(projectDir, "assets/imported").apply { mkdirs() }, safeName)
        try {
            contentResolver.openInputStream(uri)?.use { input ->
                target.outputStream().use { output -> input.copyTo(output) }
            } ?: throw IllegalStateException("Selected document could not be opened")
            refreshAssets()
            showTab("Assets")
            message("Copied $safeName to this project's private assets · available offline")
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

    private class SimpleTextWatcher(private val changed: () -> Unit) : android.text.TextWatcher {
        override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) = Unit
        override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) = changed()
        override fun afterTextChanged(s: android.text.Editable?) = Unit
    }

    companion object {
        const val EXTRA_PROJECT_ROOT = "dev.prismengine.runtime.PROJECT_ROOT"
    }
}
