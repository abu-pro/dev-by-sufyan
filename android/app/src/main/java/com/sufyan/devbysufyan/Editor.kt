package com.sufyan.devbysufyan

import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.delay

val EditorTextStyle = TextStyle(
    fontFamily = FontFamily.Monospace,
    fontSize = 14.sp,
    lineHeight = 21.sp,
    color = Color(0xFF1B1B1F)
)

/**
 * Monospace code editor with a line-number gutter. Highlighting is
 * debounced (recomputed ~160 ms after typing pauses) and typing sync is
 * debounced separately (~450 ms, like the PC and web clients), so the
 * editor never lags or flickers while typing.
 */
@Composable
fun CodeEditor(
    docKey: String,
    initialCode: String,
    language: String,
    readOnly: Boolean,
    externalCode: String,
    jumpToLine: Int,
    onJumpConsumed: () -> Unit,
    onCodeChange: (String) -> Unit,
    modifier: Modifier = Modifier
) {
    var tfv by remember(docKey) { mutableStateOf(TextFieldValue(initialCode, TextRange(0))) }
    var lastSent by remember(docKey) { mutableStateOf(initialCode) }
    var cacheText by remember(docKey) { mutableStateOf(initialCode) }
    var cache by remember(docKey) { mutableStateOf(highlightCode(initialCode, language)) }
    val scrollState = rememberScrollState()
    val density = LocalDensity.current

    // typing -> debounced live sync out
    LaunchedEffect(tfv.text) {
        val text = tfv.text
        if (text != lastSent) {
            delay(450)
            lastSent = text
            onCodeChange(text)
        }
    }
    // remote update -> apply, unless it is our own edit coming back
    LaunchedEffect(externalCode) {
        if (externalCode != tfv.text && externalCode != lastSent) {
            val sel = minOf(tfv.selection.start, externalCode.length)
            tfv = TextFieldValue(externalCode, TextRange(sel))
            lastSent = externalCode
        }
    }
    // debounced re-highlight
    LaunchedEffect(tfv.text, language) {
        val text = tfv.text
        delay(160)
        cache = highlightCode(text, language)
        cacheText = text
    }
    // diagnostics tap -> caret to that line
    LaunchedEffect(jumpToLine) {
        if (jumpToLine > 0) {
            val lines = tfv.text.split("\n")
            var offset = 0
            for (i in 0 until minOf(jumpToLine - 1, lines.size)) offset += lines[i].length + 1
            tfv = tfv.copy(selection = TextRange(offset))
            val y = with(density) { ((jumpToLine - 3).coerceAtLeast(0) * 21.sp.toPx()).toInt() }
            scrollState.animateScrollTo(y)
            onJumpConsumed()
        }
    }

    val transformation = remember(cacheText, cache) { CachedHighlightTransformation(cacheText, cache) }
    val lineCount = tfv.text.count { it == '\n' } + 1
    val numbers = remember(lineCount) { (1..lineCount).joinToString("\n") }

    Row(modifier = modifier.verticalScroll(scrollState)) {
        Text(
            text = numbers,
            style = EditorTextStyle.copy(color = Color(0xFF9AA0A6), textAlign = TextAlign.End),
            modifier = Modifier.width(46.dp).fillMaxHeight().padding(end = 10.dp, top = 10.dp, bottom = 24.dp)
        )
        BasicTextField(
            value = tfv,
            onValueChange = { if (!readOnly) tfv = it },
            readOnly = readOnly,
            textStyle = EditorTextStyle,
            visualTransformation = transformation,
            modifier = Modifier.weight(1f).fillMaxHeight().padding(top = 10.dp, bottom = 24.dp, end = 12.dp)
        )
    }
}
