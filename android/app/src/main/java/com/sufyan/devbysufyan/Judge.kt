package com.sufyan.devbysufyan

import android.util.Base64
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import org.json.JSONObject
import java.util.concurrent.TimeUnit
import kotlin.concurrent.thread

data class Diag(val line: Int, val isError: Boolean, val msg: String)

data class RunResult(
    val ok: Boolean,
    val stage: String, // "compile" | "run"
    val stdout: String,
    val stderr: String,
    val timeMs: Long,
    val diags: List<Diag>
)

/**
 * Compile & Run through the Judge0 CE online compiler — the same service
 * and language ids the web client uses (C++ 54, C 50, Python 71), with the
 * same status handling and the same error-line parsing for the
 * diagnostics list.
 */
object Judge {
    private val http = OkHttpClient.Builder()
        .connectTimeout(15, TimeUnit.SECONDS)
        .readTimeout(40, TimeUnit.SECONDS)
        .build()
    private val LANG_IDS = mapOf("cpp" to 54, "c" to 50, "python" to 71)

    fun parseDiagnostics(text: String): List<Diag> {
        val out = mutableListOf<Diag>()
        val gcc = Regex(""":(\d+):(\d+):\s*(error|warning|note)\s*:?\s*(.*)""", RegexOption.IGNORE_CASE)
        val pyLine = Regex("""line (\d+)""")
        for (raw in text.split("\n")) {
            val m = gcc.find(raw)
            if (m != null) {
                val kind = m.groupValues[3].lowercase()
                out.add(Diag(m.groupValues[1].toInt(), kind != "warning",
                    (m.groupValues[3] + ": " + m.groupValues[4]).trim()))
                continue
            }
            val pm = pyLine.find(raw)
            if (pm != null && Regex("""error|exception|traceback""", RegexOption.IGNORE_CASE).containsMatchIn(text)) {
                out.add(Diag(pm.groupValues[1].toInt(), true, raw.trim()))
            }
        }
        return out
    }

    fun run(language: String, code: String, stdin: String, callback: (RunResult) -> Unit) {
        thread {
            val t0 = System.currentTimeMillis()
            fun done(r: RunResult) = callback(r)
            try {
                val b64 = { s: String -> Base64.encodeToString(s.toByteArray(Charsets.UTF_8), Base64.NO_WRAP) }
                val body = JSONObject()
                    .put("language_id", LANG_IDS[language] ?: 54)
                    .put("source_code", b64(code))
                    .put("stdin", b64(stdin))
                val req = Request.Builder()
                    .url("https://ce.judge0.com/submissions?base64_encoded=true&wait=true")
                    .post(body.toString().toRequestBody("application/json".toMediaType()))
                    .build()
                http.newCall(req).execute().use { resp ->
                    val timeMs = System.currentTimeMillis() - t0
                    if (!resp.isSuccessful) {
                        done(RunResult(false, "compile", "",
                            "Online compiler error (${resp.code}). Try again in a moment.", timeMs, emptyList()))
                        return@thread
                    }
                    val j = JSONObject(resp.body?.string() ?: "{}")
                    val dec = { key: String ->
                        val v = j.optString(key, "")
                        if (v.isEmpty()) "" else String(Base64.decode(v, Base64.DEFAULT), Charsets.UTF_8)
                    }
                    val statusId = j.optJSONObject("status")?.optInt("id") ?: 0
                    if (statusId == 6) {
                        val err = dec("compile_output").ifEmpty { "Compilation failed." }
                        done(RunResult(false, "compile", "", err.take(65536), timeMs, parseDiagnostics(err)))
                        return@thread
                    }
                    var stderr = dec("stderr")
                    if (statusId == 5) stderr += "\n[Time limit exceeded on the online compiler]"
                    else if (statusId != 3) {
                        val desc = j.optJSONObject("status")?.optString("description") ?: ""
                        if (desc.isNotEmpty()) stderr += (if (stderr.isEmpty()) "" else "\n") + "[$desc]"
                    }
                    val out = dec("stdout")
                    done(RunResult(statusId == 3, "run", out.take(65536), stderr.take(65536),
                        timeMs, parseDiagnostics(stderr)))
                }
            } catch (e: Exception) {
                done(RunResult(false, "compile", "",
                    "Could not reach the online compiler. Check the internet connection and try again.",
                    System.currentTimeMillis() - t0, emptyList()))
            }
        }
    }
}
