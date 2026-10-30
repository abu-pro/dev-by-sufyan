package com.sufyan.devbysufyan

import android.content.ContentValues
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.Environment
import android.provider.MediaStore
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Save
import androidx.compose.material.icons.filled.Share
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import java.io.File

@Composable
fun EditorScreen(state: AppState) {
    val ctx = LocalContext.current
    val inRoom = state.inRoom
    val viewingId = if (inRoom) state.viewingId else ""
    val doc = state.currentDoc()
    val owner = if (inRoom) state.member(viewingId) else null
    val mine = !inRoom || viewingId == state.selfId
    val editable = !inRoom || state.canEdit(viewingId)

    var stdin by remember(viewingId) { mutableStateOf("") }
    var output by remember(viewingId) { mutableStateOf("") }
    var diags by remember(viewingId) { mutableStateOf<List<Diag>>(emptyList()) }
    var running by remember(viewingId) { mutableStateOf(false) }
    var jumpLine by remember(viewingId) { mutableStateOf(0) }

    fun run() {
        if (running) return
        running = true
        output = "Compiling…"
        Judge.run(doc.language, doc.code, stdin) { r ->
            state.post {
                running = false
                diags = r.diags
                output = buildString {
                    if (r.stage == "compile" && !r.ok) {
                        append("Compilation failed — tap an error below to jump to its line.")
                        if (r.stderr.isNotEmpty()) append("\n\n").append(r.stderr)
                    } else {
                        if (r.stdout.isNotEmpty()) append(r.stdout)
                        if (r.stderr.isNotEmpty()) {
                            if (isNotEmpty() && !endsWith("\n")) append("\n")
                            append(r.stderr)
                        }
                        if (r.stdout.isEmpty() && r.stderr.isEmpty()) append("(no output)")
                        append("\n\n— Finished in ${r.timeMs} ms (online compiler) —")
                    }
                }
            }
        }
    }

    Column(modifier = Modifier.fillMaxSize()) {
        // header
        Row(
            modifier = Modifier.fillMaxWidth().background(Color.White).padding(horizontal = 4.dp, vertical = 6.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            IconButton(onClick = { state.screen = if (inRoom) Screen.ROOM else Screen.HOME }) {
                Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
            }
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    when {
                        !inRoom -> "My code"
                        mine -> "My code"
                        else -> "${owner?.name ?: "Member"}'s code"
                    },
                    fontWeight = FontWeight.Bold, fontSize = 17.sp
                )
                Text(
                    LANG_LABELS[doc.language] ?: doc.language,
                    fontSize = 12.sp, color = Color(0xFF6B7280)
                )
            }
            IconButton(onClick = { shareCode(ctx, doc) }) {
                Icon(Icons.Filled.Share, contentDescription = "Share code")
            }
            IconButton(onClick = {
                val name = saveCodeToDownloads(ctx, owner?.name ?: "main", doc)
                state.toast(name)
            }) {
                Icon(Icons.Filled.Save, contentDescription = "Save code")
            }
            Button(
                onClick = { run() },
                enabled = !running,
                colors = ButtonDefaults.buttonColors(containerColor = BrandRed),
                shape = RoundedCornerShape(10.dp),
                modifier = Modifier.padding(end = 8.dp)
            ) {
                if (running) CircularProgressIndicator(color = Color.White, modifier = Modifier.height(18.dp).width(18.dp))
                else Icon(Icons.Filled.PlayArrow, contentDescription = null)
                Spacer(Modifier.width(4.dp))
                Text("Run")
            }
        }

        // who's-code banner
        if (inRoom && !mine) {
            val (bg, fg, text) = if (editable)
                Triple(Color(0xFFE8F5E9), Color(0xFF215C24),
                    "You are editing ${owner?.name}'s code — changes sync live.")
            else
                Triple(Color(0xFFFFF8E1), Color(0xFF6D5410),
                    "Viewing ${owner?.name}'s code — view only" +
                        if (owner?.role == "admin") " (the admin's code is always view-only)." else ".")
            Text(text, color = fg, fontSize = 13.sp,
                modifier = Modifier.fillMaxWidth().background(bg).padding(horizontal = 14.dp, vertical = 8.dp))
        }

        // language chips (only when this doc can be edited)
        if (editable) {
            Row(
                modifier = Modifier.fillMaxWidth().background(Color.White).padding(horizontal = 12.dp, vertical = 4.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text("Language:", fontSize = 13.sp, color = Color(0xFF6B7280))
                for (lang in listOf("cpp", "c", "python")) {
                    FilterChip(
                        selected = doc.language == lang,
                        onClick = {
                            if (doc.language != lang) {
                                val updated = doc.copy(language = lang)
                                if (inRoom) {
                                    owner?.doc = updated
                                    state.client.sendDocUpdate(viewingId, updated)
                                } else {
                                    state.soloDoc = updated
                                }
                            }
                        },
                        label = { Text(LANG_LABELS.getValue(lang)) }
                    )
                }
            }
        }

        CodeEditor(
            docKey = if (inRoom) viewingId else "solo",
            initialCode = doc.code,
            language = doc.language,
            readOnly = !editable,
            externalCode = doc.code,
            jumpToLine = jumpLine,
            onJumpConsumed = { jumpLine = 0 },
            onCodeChange = { text ->
                val updated = doc.copy(code = text)
                if (inRoom) {
                    owner?.doc = updated
                    state.client.sendDocUpdate(viewingId, updated)
                } else {
                    state.soloDoc = updated
                }
            },
            modifier = Modifier.weight(1f).fillMaxWidth().background(Color(0xFFFCFCFD))
        )

        // bottom panel: stdin + output + diagnostics
        Column(
            modifier = Modifier.fillMaxWidth().background(Color.White)
                .padding(horizontal = 12.dp, vertical = 8.dp)
        ) {
            OutlinedTextField(
                value = stdin, onValueChange = { stdin = it },
                label = { Text("Program input (stdin) — optional") },
                singleLine = false, maxLines = 2,
                modifier = Modifier.fillMaxWidth()
            )
            if (diags.isNotEmpty()) {
                Column(modifier = Modifier.padding(top = 6.dp)) {
                    for (d in diags.take(8)) {
                        Text(
                            "Line ${d.line} — ${d.msg}",
                            color = if (d.isError) BrandRed else Color(0xFFB26A00),
                            fontSize = 13.sp,
                            modifier = Modifier.fillMaxWidth().clickable { jumpLine = d.line }
                                .padding(vertical = 3.dp)
                        )
                    }
                }
            }
            if (output.isNotEmpty()) {
                Text(
                    output,
                    fontFamily = FontFamily.Monospace, fontSize = 13.sp,
                    modifier = Modifier.fillMaxWidth().heightIn(max = 170.dp)
                        .verticalScroll(rememberScrollState()).padding(top = 6.dp)
                )
            }
        }
    }
}

private fun shareCode(ctx: Context, doc: RoomDoc) {
    val intent = Intent(Intent.ACTION_SEND).apply {
        type = "text/plain"
        putExtra(Intent.EXTRA_SUBJECT, "Code (${LANG_LABELS[doc.language] ?: doc.language}) — Dev by Sufyan")
        putExtra(Intent.EXTRA_TEXT, doc.code)
    }
    ctx.startActivity(Intent.createChooser(intent, "Share code"))
}

/** Saves into the device Downloads folder; returns a user-facing message. */
private fun saveCodeToDownloads(ctx: Context, baseName: String, doc: RoomDoc): String {
    val safe = baseName.replace(Regex("[^A-Za-z0-9_-]"), "_").ifEmpty { "main" }
    val fileName = "$safe.${LANG_EXT[doc.language] ?: "txt"}"
    return try {
        if (Build.VERSION.SDK_INT >= 29) {
            val values = ContentValues().apply {
                put(MediaStore.Downloads.DISPLAY_NAME, fileName)
                put(MediaStore.Downloads.MIME_TYPE, "text/plain")
                put(MediaStore.Downloads.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS)
            }
            val uri = ctx.contentResolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values)
                ?: return "Could not save the file."
            ctx.contentResolver.openOutputStream(uri)?.use { it.write(doc.code.toByteArray(Charsets.UTF_8)) }
            "Saved to Downloads/$fileName"
        } else {
            val dir = ctx.getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS) ?: ctx.filesDir
            File(dir, fileName).writeText(doc.code)
            "Saved to ${dir.absolutePath}/$fileName"
        }
    } catch (e: Exception) {
        "Could not save the file: ${e.message}"
    }
}
