package com.sufyan.devbysufyan

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.OffsetMapping
import androidx.compose.ui.text.input.TransformedText
import androidx.compose.ui.text.input.VisualTransformation

private val CPP_KW = ("auto bool break case catch char class const continue default delete do double else enum " +
    "explicit extern false float for friend goto if inline int long mutable namespace new operator private " +
    "protected public register return short signed sizeof static struct switch template this throw true try " +
    "typedef typename union unsigned using virtual void volatile while cout cin endl std string vector").split(" ")
private val PY_KW = ("and as assert async await break class continue def del elif else except finally for from " +
    "global if import in is lambda nonlocal not or pass print raise return try while with yield True False None self").split(" ")

private val KW_COLOR = Color(0xFF7C4DFF)
private val STR_COLOR = Color(0xFF2E7D32)
private val COM_COLOR = Color(0xFF8A8D93)
private val NUM_COLOR = Color(0xFFD84315)
private val PRE_COLOR = Color(0xFF00838F)

/**
 * Regex tokenizer equivalent to the web client's: comments, preprocessor
 * lines (C/C++), strings, numbers, keywords — one pass, span styles only.
 */
fun highlightCode(code: String, language: String): AnnotatedString {
    val kws = if (language == "python") PY_KW else CPP_KW
    val kwAlt = kws.joinToString("|")
    val pattern = if (language == "python") {
        """(#[^\n]*)|("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')|\b(\d+(?:\.\d+)?)\b|\b($kwAlt)\b"""
    } else {
        """(//[^\n]*)|(^\s*#[^\n]*)|("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')|\b(\d+(?:\.\d+)?)\b|\b($kwAlt)\b""".trimIndent()
    }
    val re = Regex(pattern, setOf(RegexOption.MULTILINE))
    val styles = if (language == "python") {
        listOf(
            SpanStyle(color = COM_COLOR),                    // comment
            SpanStyle(color = STR_COLOR),                    // string
            SpanStyle(color = NUM_COLOR),                    // number
            SpanStyle(color = KW_COLOR, fontWeight = FontWeight.Bold) // keyword
        )
    } else {
        listOf(
            SpanStyle(color = COM_COLOR),                    // comment
            SpanStyle(color = PRE_COLOR),                    // preprocessor
            SpanStyle(color = STR_COLOR),                    // string
            SpanStyle(color = NUM_COLOR),                    // number
            SpanStyle(color = KW_COLOR, fontWeight = FontWeight.Bold) // keyword
        )
    }
    return buildAnnotatedString {
        append(code)
        for (m in re.findAll(code)) {
            for (gi in 1 until m.groups.size) {
                val g = m.groups[gi]
                if (g != null) {
                    addStyle(styles[gi - 1], g.range.first, g.range.last + 1)
                    break
                }
            }
        }
    }
}

/**
 * Applies a previously computed [AnnotatedString] when the text still
 * matches it; while the user is typing (text differs) it shows plain text
 * until the debounced re-highlight lands — no flicker, no per-keystroke
 * regex over the whole document.
 */
class CachedHighlightTransformation(
    private val cachedText: String,
    private val cached: AnnotatedString
) : VisualTransformation {
    override fun filter(text: AnnotatedString): TransformedText {
        return if (text.text == cachedText) TransformedText(cached, OffsetMapping.Identity)
        else TransformedText(text, OffsetMapping.Identity)
    }
}
