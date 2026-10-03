package dev.prismengine.runtime

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.graphics.Color
import android.os.Bundle
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import org.json.JSONObject
import java.io.File

/** Offline project hub. Starter kits are packaged as source assets inside the APK. */
class ProjectManagerActivity : Activity() {
    private val bg = Color.rgb(10, 10, 18)
    private val panel = Color.rgb(24, 24, 38)
    private val violet = Color.rgb(124, 58, 237)
    private val cyan = Color.rgb(0, 217, 255)
    private val gold = Color.rgb(255, 201, 60)
    private val muted = Color.rgb(155, 155, 179)
    private lateinit var body: LinearLayout
    private lateinit var search: EditText
    private val projectsDir by lazy { File(filesDir, "PrismProjects").apply { mkdirs() } }
    private data class Template(val id: String, val name: String, val dimension: String, val genre: String)

    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        @Suppress("DEPRECATION")
        window.decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_FULLSCREEN or
            View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(bg)
            setPadding(dp(16), dp(10), dp(16), dp(10))
        }
        val header = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            addView(text("◈  PRISM ENGINE", 20, gold, true), LinearLayout.LayoutParams(0, dp(48), 1f))
            addView(text("PROJECT HUB · OFFLINE", 10, cyan))
        }
        root.addView(header)
        root.addView(text("Every Angle. Every World. One File.", 12, muted))
        search = EditText(this).apply {
            hint = "Search projects and starter kits"
            setSingleLine(true)
            setTextColor(Color.WHITE)
            setHintTextColor(muted)
            textSize = 14f
            setPadding(dp(12), 0, dp(12), 0)
            setBackgroundColor(panel)
        }
        root.addView(search, LinearLayout.LayoutParams(-1, dp(46)).apply { topMargin = dp(8) })
        root.addView(button("＋  NEW BLANK PROJECT", violet) { promptBlankProject() })
        body = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        val scroll = ScrollView(this).apply { isFillViewport = true; addView(body) }
        root.addView(scroll, LinearLayout.LayoutParams(-1, 0, 1f))
        setContentView(root)
        search.addTextChangedListener(SimpleTextWatcher { renderSections() })
        renderSections()
    }

    override fun onResume() {
        super.onResume()
        if (::body.isInitialized) renderSections()
    }

    private fun renderSections() {
        if (!::body.isInitialized) return
        body.removeAllViews()
        val query = search.text?.toString()?.trim()?.lowercase().orEmpty()
        val projects = projectsDir.listFiles()?.filter { File(it, "project.prism.json").isFile }
            ?.sortedBy { projectName(it).lowercase() }.orEmpty()
        body.addView(section("MY PROJECTS  ·  ${projects.size}"))
        if (projects.isEmpty()) body.addView(text("Your projects stay in private device storage. Create one below or start from a kit.", 12, muted))
        projects.filter { query.isEmpty() || projectName(it).lowercase().contains(query) }.forEach { dir -> body.addView(projectCard(dir)) }

        val templates = availableTemplates().filter {
            query.isEmpty() || "${it.name} ${it.genre} ${it.dimension}".lowercase().contains(query)
        }
        body.addView(section("STARTER KITS  ·  ${templates.size} OFFLINE SOURCE PACKS"))
        body.addView(text("2D and 3D scenes · PrismScript · OBJ model sources · SVG textures · PBR materials · UI and touch layouts", 11, muted))
        templates.forEach { template -> body.addView(templateCard(template)) }
    }

    private fun availableTemplates(): List<Template> {
        val result = mutableListOf<Template>()
        val dirs = assets.list("")?.filter { it.matches(Regex("[a-z0-9_]+")) }.orEmpty()
        for (id in dirs) {
            try {
                val info = JSONObject(assets.open("$id/project.prism.json").bufferedReader().use { it.readText() })
                result += Template(
                    id,
                    info.optString("name", info.optString("Name", id)),
                    info.optString("dimension", "3D"),
                    info.optString("genre", "Game starter"),
                )
            } catch (_: Exception) {
                // Root-level files such as sample.prism and non-project folders are not templates.
            }
        }
        return result.sortedBy { it.name.lowercase() }
    }

    private fun projectCard(dir: File): View = card().apply {
        addView(text(projectName(dir), 16, Color.WHITE, true))
        addView(text("LOCAL PROJECT  ·  ${dir.name}", 10, cyan))
        val actions = LinearLayout(this@ProjectManagerActivity).apply { orientation = LinearLayout.HORIZONTAL }
        actions.addView(button("OPEN", violet) { openProject(dir) }, LinearLayout.LayoutParams(0, dp(42), 1f))
        actions.addView(button("DUPLICATE", panel) { duplicateProject(dir) }, LinearLayout.LayoutParams(0, dp(42), 1f))
        actions.addView(button("DELETE", panel) { confirmDelete(dir) }, LinearLayout.LayoutParams(0, dp(42), 1f))
        addView(actions)
    }

    private fun templateCard(template: Template): View = card().apply {
        val title = LinearLayout(this@ProjectManagerActivity).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            addView(text(template.name, 15, Color.WHITE, true), LinearLayout.LayoutParams(0, dp(32), 1f))
            addView(text(template.dimension.uppercase(), 10, gold, true))
        }
        addView(title)
        addView(text("${template.genre}  ·  ${template.id}", 11, cyan))
        addView(text("PrismScript + scene hierarchy + source models/textures + material + HUD + touch profile", 11, muted))
        addView(button("CREATE PROJECT FROM KIT", panel) { createFromTemplate(template) })
    }

    private fun promptBlankProject() {
        val input = EditText(this).apply { hint = "Project name"; setSingleLine(true); setTextColor(Color.WHITE) }
        AlertDialog.Builder(this)
            .setTitle("New offline project")
            .setMessage("A local Android APK project folder will be created on this device.")
            .setView(input)
            .setNegativeButton("Cancel", null)
            .setPositiveButton("Create") { _, _ -> createBlank(input.text.toString()) }
            .show()
    }

    private fun createBlank(raw: String) {
        val display = raw.trim().ifBlank { "New Prism Game" }
        val slug = slug(display)
        val target = uniqueProjectDir(slug)
        target.mkdirs()
        listOf("scenes", "scripts", "assets/models", "assets/textures", "assets/materials", "ui", "controls").forEach { File(target, it).mkdirs() }
        val manifest = JSONObject()
            .put("name", display).put("packageId", "dev.prismengine.project.$slug")
            .put("version", "1.0.0").put("dimension", "3D")
            .put("entryScene", "scenes/main.scene.json").put("script", "scripts/main.prism")
            .put("export", JSONObject().put("only", "android-apk")).put("offline", true)
        File(target, "project.prism.json").writeText(manifest.toString(2) + "\n")
        val scene = JSONObject().put("version", 1).put("name", "Main")
            .put("dimension", "3D").put("entities", org.json.JSONArray())
        File(target, "scenes/main.scene.json").writeText(scene.toString(2) + "\n")
        File(target, "scripts/main.prism").writeText(
            "// $display · offline PrismScript entry point\nvar score = 0;\nfunc start() { print(\"$display ready\"); }\nfunc update(dt) { if (dt > 0) { score += 0; } }\nstart();\n",
        )
        File(target, "ui/main.ui.json").writeText("{\"version\":1,\"nodes\":[{\"type\":\"Label\",\"name\":\"Title\",\"text\":\"$display\"}]}\n")
        File(target, "controls/touch.json").writeText("{\"version\":1,\"layout\":\"landscape\",\"controls\":[]}\n")
        openProject(target)
    }

    private fun createFromTemplate(template: Template) {
        val target = uniqueProjectDir(template.id)
        try {
            copyAssetTree(template.id, target)
            openProject(target)
        } catch (e: Exception) {
            AlertDialog.Builder(this).setTitle("Could not create project")
                .setMessage(e.message ?: "Source pack unavailable").setPositiveButton("OK", null).show()
        }
    }

    private fun copyAssetTree(assetPath: String, target: File) {
        val names = assets.list(assetPath).orEmpty()
        if (names.isEmpty()) {
            target.parentFile?.mkdirs()
            assets.open(assetPath).use { input -> target.outputStream().use { output -> input.copyTo(output) } }
            return
        }
        target.mkdirs()
        for (name in names) copyAssetTree("$assetPath/$name", File(target, name))
    }

    private fun duplicateProject(source: File) {
        val target = uniqueProjectDir("${source.name}_copy")
        source.copyRecursively(target, overwrite = false)
        openProject(target)
    }

    private fun confirmDelete(dir: File) {
        AlertDialog.Builder(this).setTitle("Delete ${projectName(dir)}?")
            .setMessage("This permanently removes the local project and its imported assets from private app storage.")
            .setNegativeButton("Cancel", null)
            .setPositiveButton("Delete") { _, _ -> dir.deleteRecursively(); renderSections() }.show()
    }

    private fun uniqueProjectDir(base: String): File {
        var candidate = File(projectsDir, base)
        var index = 2
        while (candidate.exists()) candidate = File(projectsDir, "${base}_$index").also { index++ }
        return candidate
    }

    private fun openProject(dir: File) {
        startActivity(Intent(this, PrismStudioActivity::class.java).putExtra(PrismStudioActivity.EXTRA_PROJECT_ROOT, dir.absolutePath))
    }

    private fun projectName(dir: File): String = try {
        val j = JSONObject(File(dir, "project.prism.json").readText())
        j.optString("name", j.optString("Name", dir.name))
    } catch (_: Exception) { dir.name }

    private fun slug(value: String) = value.lowercase().replace(Regex("[^a-z0-9]+"), "_").trim('_').ifBlank { "prism_game" }
    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()

    private fun section(value: String) = text(value, 11, gold, true).apply { setPadding(dp(2), dp(15), dp(2), dp(7)) }
    private fun text(value: String, size: Int, color: Int, bold: Boolean = false) = TextView(this).apply {
        text = value; textSize = size.toFloat(); setTextColor(color); setPadding(dp(3), dp(3), dp(3), dp(3))
        if (bold) typeface = android.graphics.Typeface.DEFAULT_BOLD
    }
    private fun card() = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL; setPadding(dp(10), dp(8), dp(10), dp(8)); setBackgroundColor(panel)
        val lp = LinearLayout.LayoutParams(-1, -2); lp.setMargins(0, dp(4), 0, dp(4)); layoutParams = lp
    }
    private fun button(caption: String, color: Int, click: () -> Unit) = Button(this).apply {
        text = caption; textSize = 11f; isAllCaps = false; setTextColor(Color.WHITE)
        setBackgroundTintList(android.content.res.ColorStateList.valueOf(color)); setOnClickListener { click() }
    }

    private class SimpleTextWatcher(val changed: () -> Unit) : android.text.TextWatcher {
        override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) = Unit
        override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) = changed()
        override fun afterTextChanged(s: android.text.Editable?) = Unit
    }
}
