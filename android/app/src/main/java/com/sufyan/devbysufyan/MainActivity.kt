package com.sufyan.devbysufyan

import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

val BrandRed = Color(0xFFE02323)
val BrandRedDark = Color(0xFFB31B1B)

val STARTERS = mapOf(
    "cpp" to "#include <iostream>\nusing namespace std;\n\nint main() {\n    cout << \"Hello from Dev by Sufyan!\" << endl;\n    return 0;\n}\n",
    "c" to "#include <stdio.h>\n\nint main() {\n    printf(\"Hello from Dev by Sufyan!\\n\");\n    return 0;\n}\n",
    "python" to "# Welcome to Dev by Sufyan\nprint(\"Hello from Dev by Sufyan!\")\n"
)
val LANG_LABELS = mapOf("cpp" to "C++", "c" to "C", "python" to "Python")
val LANG_EXT = mapOf("cpp" to "cpp", "c" to "c", "python" to "py")

/** Same fixed palette + hash as the PC app: a member keeps one color everywhere. */
private val MEMBER_COLORS = listOf(
    0xFFE02323, 0xFF1E88E5, 0xFF43A047, 0xFFFB8C00,
    0xFF8E24AA, 0xFF00ACC1, 0xFF6D4C41, 0xFF3949AB
).map { Color(it) }

fun memberColor(memberId: String): Color {
    var hash = 0
    for (ch in memberId) hash = hash * 31 + ch.code
    return MEMBER_COLORS[Math.floorMod(hash, MEMBER_COLORS.size)]
}

enum class Screen { HOME, EDITOR, ROOM }

/** All app state in one place; RoomClient callbacks are reposted to main. */
class AppState(private val activity: ComponentActivity) : RoomClient.Listener {
    var screen by mutableStateOf(Screen.HOME)

    // solo editing
    var soloDoc by mutableStateOf(RoomDoc("cpp", STARTERS.getValue("cpp")))

    // room state
    var inRoom by mutableStateOf(false)
    var selfId by mutableStateOf("")
    var role by mutableStateOf("member")
    var roomCode by mutableStateOf("")
    val members = mutableStateListOf<Member>()
    var settings by mutableStateOf(RoomSettings())
    val chat = mutableStateListOf<ChatMsg>()
    var chatUnread by mutableStateOf(0) // v1.3.0: messages seen while in the editor
    var typingText by mutableStateOf("")
    var viewingId by mutableStateOf("") // whose doc the editor shows; "" = solo
    var connState by mutableStateOf(RoomClient.ConnState.IDLE)

    val client = RoomClient(this)
    private val main = Handler(Looper.getMainLooper())
    private val typingTimers = mutableMapOf<String, Runnable>()

    fun post(block: () -> Unit) { main.post(block) }
    fun toast(msg: String) = post {
        Toast.makeText(activity, msg, Toast.LENGTH_LONG).show()
    }

    fun member(id: String): Member? = members.firstOrNull { it.id == id }
    fun isAdmin() = role == "admin"
    fun canEdit(targetId: String): Boolean {
        val target = member(targetId) ?: return targetId == selfId
        if (targetId == selfId) return true
        if (isAdmin()) return true
        // v1.3.0 mirror of the server rule: a restricted member cannot
        // edit anyone else's code, even with peer editing on.
        if (member(selfId)?.restricted == true) return false
        return settings.membersCanEditEachOther && target.role != "admin"
    }

    fun currentDoc(): RoomDoc =
        if (inRoom) member(viewingId)?.doc ?: soloDoc else soloDoc

    fun createRoom(name: String) = client.createRoom(name, soloDoc)
    fun joinRoom(name: String, code: String) = client.joinRoom(name, code, soloDoc)

    fun leaveRoom(silent: Boolean = false) {
        client.leave()
        exitRoomUi(null)
        if (!silent) toast("You left the room.")
    }

    private fun exitRoomUi(msg: String?) {
        inRoom = false
        selfId = ""; role = "member"; roomCode = ""
        members.clear(); chat.clear(); typingText = ""
        chatUnread = 0
        viewingId = ""
        connState = RoomClient.ConnState.IDLE
        screen = Screen.HOME
        if (msg != null) toast(msg)
    }

    // ---- RoomClient.Listener (arrive on OkHttp threads -> repost) ----
    override fun onRoomJoined(selfId: String, role: String, code: String,
                              settings: RoomSettings, members: List<Member>,
                              chatLog: List<ChatMsg>) = post {
        // A room_joined while we are already flagged in-room is a resume:
        // the UI never left the room while the client reconnected.
        val wasResume = this.inRoom
        this.selfId = selfId; this.role = role; this.roomCode = code
        this.settings = settings
        this.members.clear(); this.members.addAll(members)
        this.chat.clear(); this.chat.addAll(chatLog)
        // On a resume, keep viewing whoever we were viewing if they remain.
        if (this.members.none { it.id == this.viewingId }) this.viewingId = selfId
        this.inRoom = true
        this.screen = Screen.ROOM
        if (!wasResume) {
            toast(if (role == "admin") "Room $code created — share the code!" else "Joined room $code")
        }
    }

    override fun onMemberJoined(member: Member) = post {
        if (this.members.none { it.id == member.id }) this.members.add(member)
        toast("${member.name} joined the room")
    }

    override fun onMemberLeft(memberId: String, name: String) = post {
        this.members.removeAll { it.id == memberId }
        clearTyping(memberId)
        if (viewingId == memberId) viewingId = selfId
        if (name.isNotEmpty()) toast("$name left the room")
    }

    override fun onMemberAway(memberId: String, name: String) = post {
        // Replace the list item so Compose recomposes the member row.
        val idx = this.members.indexOfFirst { it.id == memberId }
        if (idx >= 0) this.members[idx] = this.members[idx].copy(away = true)
        if (name.isNotEmpty()) toast("$name lost connection — seat held for a bit")
    }

    override fun onMemberBack(memberId: String, name: String) = post {
        val idx = this.members.indexOfFirst { it.id == memberId }
        if (idx >= 0) this.members[idx] = this.members[idx].copy(away = false)
        if (name.isNotEmpty()) toast("$name is back")
    }

    override fun onDocChanged(targetId: String, doc: RoomDoc) = post {
        member(targetId)?.doc = doc
    }

    override fun onDocDenied(targetId: String, doc: RoomDoc, message: String) = post {
        member(targetId)?.doc = doc
        toast(message.ifEmpty { "This code is view-only." })
    }

    override fun onSettingsChanged(settings: RoomSettings) = post {
        this.settings = settings
        toast("Admin updated the room settings")
    }

    override fun onChat(msg: ChatMsg) = post {
        chat.add(msg)
        if (chat.size > 200) chat.removeRange(0, chat.size - 200)
        clearTyping(msg.fromId)
        // v1.3.0: badge the room screen's chat header when a message from
        // someone else lands while the user is viewing code (no forced
        // navigation on mobile).
        if (msg.fromId != selfId && screen != Screen.ROOM) chatUnread++
    }

    override fun onMemberRestricted(targetId: String, restricted: Boolean) = post {
        val idx = this.members.indexOfFirst { it.id == targetId }
        if (idx >= 0) this.members[idx] = this.members[idx].copy(restricted = restricted)
        if (targetId == selfId) {
            toast(if (restricted) "The admin has restricted you from editing other members' code."
                  else "The admin lifted your editing restriction.")
        }
    }

    override fun onTyping(fromId: String, fromName: String) = post {
        typingTimers.remove(fromId)?.let { main.removeCallbacks(it) }
        typingNames[fromId] = member(fromId)?.name ?: fromName
        rebuildTypingText()
        val r = Runnable {
            typingNames.remove(fromId); typingTimers.remove(fromId); rebuildTypingText()
        }
        typingTimers[fromId] = r
        main.postDelayed(r, 2600)
    }

    private val typingNames = mutableMapOf<String, String>()
    private fun rebuildTypingText() {
        val names = typingNames.values.toList()
        typingText = when {
            names.isEmpty() -> ""
            names.size == 1 -> "${names[0]} is typing…"
            names.size == 2 -> "${names[0]} and ${names[1]} are typing…"
            else -> "Several people are typing…"
        }
    }

    private fun clearTyping(id: String) {
        typingTimers.remove(id)?.let { main.removeCallbacks(it) }
        if (typingNames.remove(id) != null) rebuildTypingText()
    }

    override fun onKicked(message: String) = post {
        exitRoomUi(message.ifEmpty { "The admin removed you from the room." })
    }

    override fun onRoomClosed(message: String) = post {
        exitRoomUi(message.ifEmpty { "The admin left — the room is closed." })
    }

    override fun onError(message: String) = post {
        toast(message)
    }

    override fun onResumeFailed(message: String) = post {
        exitRoomUi(message.ifEmpty { "Your seat in the room has expired. Please join again." })
    }

    override fun onConnectionState(state: RoomClient.ConnState) = post {
        connState = state
    }
}

class MainActivity : ComponentActivity() {
    lateinit var state: AppState

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        state = AppState(this)
        setContent {
            MaterialTheme(
                colorScheme = lightColorScheme(
                    primary = BrandRed,
                    onPrimary = Color.White,
                    secondary = BrandRedDark,
                    surface = Color.White,
                    background = Color(0xFFF7F7F9)
                )
            ) {
                Surface(modifier = Modifier.fillMaxSize()) {
                    androidx.compose.foundation.layout.Box(modifier = Modifier.fillMaxSize()) {
                        when (state.screen) {
                            Screen.HOME -> HomeScreen(state)
                            Screen.EDITOR -> EditorScreen(state)
                            Screen.ROOM -> RoomScreen(state)
                        }
                        // Never fail silently: a create/join in flight shows
                        // a clear progress card instead of a dead screen.
                        if (state.connState == RoomClient.ConnState.CONNECTING) {
                            androidx.compose.material3.Card(
                                shape = RoundedCornerShape(16.dp),
                                colors = androidx.compose.material3.CardDefaults.cardColors(containerColor = Color.White),
                                modifier = Modifier.align(Alignment.Center).padding(32.dp)
                            ) {
                                Column(
                                    modifier = Modifier.padding(24.dp),
                                    horizontalAlignment = Alignment.CenterHorizontally
                                ) {
                                    androidx.compose.material3.CircularProgressIndicator(color = BrandRed)
                                    Spacer(Modifier.height(14.dp))
                                    Text("Connecting to the room server…", fontWeight = FontWeight.SemiBold)
                                    Text("This can take a few seconds on a slow network.",
                                        fontSize = 12.5.sp, color = Color(0xFF6B7280),
                                        textAlign = TextAlign.Center)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        when (state.screen) {
            Screen.EDITOR -> state.screen = if (state.inRoom) Screen.ROOM else Screen.HOME
            Screen.ROOM -> { /* stay; leaving is explicit via Leave Room */ }
            Screen.HOME -> super.onBackPressed()
        }
    }
}

@Composable
fun HomeScreen(state: AppState) {
    // remember is essential: without it the state is recreated on every
    // recomposition and the dialogs can never open (v1.1.0's dead
    // Create/Join Room bug).
    var showCreate by remember { mutableStateOf(false) }
    var showJoin by remember { mutableStateOf(false) }
    Column(
        modifier = Modifier.fillMaxSize().padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        Column(
            modifier = Modifier.size(104.dp).clip(CircleShape)
                .background(BrandRed),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center
        ) {
            Text("DEV", color = Color.White, fontSize = 30.sp, fontWeight = FontWeight.ExtraBold)
            Text("by Sufyan", color = Color(0xFFFFD9D9), fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
        }
        Spacer(Modifier.height(18.dp))
        Text("Dev by Sufyan", fontSize = 30.sp, fontWeight = FontWeight.Bold)
        Text(
            "Code in C, C++ and Python — and code together in live Rooms.",
            fontSize = 15.sp, color = Color(0xFF555B63), textAlign = TextAlign.Center,
            modifier = Modifier.padding(top = 6.dp, start = 12.dp, end = 12.dp)
        )
        Spacer(Modifier.height(30.dp))
        Text("Start a new file", fontWeight = FontWeight.SemiBold, color = Color(0xFF555B63))
        Spacer(Modifier.height(8.dp))
        Row3LangButtons { lang ->
            state.soloDoc = RoomDoc(lang, STARTERS.getValue(lang))
            state.viewingId = ""
            state.screen = Screen.EDITOR
        }
        Spacer(Modifier.height(26.dp))
        Button(
            onClick = { showCreate = true },
            colors = ButtonDefaults.buttonColors(containerColor = BrandRed),
            shape = RoundedCornerShape(12.dp),
            modifier = Modifier.fillMaxWidth().height(54.dp)
        ) { Text("Create a Room", fontSize = 17.sp, fontWeight = FontWeight.SemiBold) }
        Spacer(Modifier.height(12.dp))
        OutlinedButton(
            onClick = { showJoin = true },
            shape = RoundedCornerShape(12.dp),
            modifier = Modifier.fillMaxWidth().height(54.dp)
        ) { Text("Join a Room", fontSize = 17.sp, fontWeight = FontWeight.SemiBold, color = BrandRed) }
        Spacer(Modifier.height(30.dp))
        Text("Made by Abu Sufyan", color = Color(0xFF9AA0A6), fontSize = 13.sp)
    }

    if (showCreate) {
        NameDialog(
            title = "Create a Room",
            confirmText = "Create",
            withCode = false,
            onDismiss = { showCreate = false },
            onConfirm = { name, _ -> showCreate = false; state.createRoom(name) }
        )
    }
    if (showJoin) {
        NameDialog(
            title = "Join a Room",
            confirmText = "Join",
            withCode = true,
            onDismiss = { showJoin = false },
            onConfirm = { name, code -> showJoin = false; state.joinRoom(name, code) }
        )
    }
}

@Composable
private fun Row3LangButtons(onPick: (String) -> Unit) {
    androidx.compose.foundation.layout.Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(10.dp)
    ) {
        for (lang in listOf("cpp", "c", "python")) {
            OutlinedButton(
                onClick = { onPick(lang) },
                shape = RoundedCornerShape(12.dp),
                modifier = Modifier.weight(1f).height(50.dp)
            ) { Text(LANG_LABELS.getValue(lang), fontSize = 16.sp, fontWeight = FontWeight.SemiBold) }
        }
    }
}

@Composable
fun NameDialog(
    title: String,
    confirmText: String,
    withCode: Boolean,
    onDismiss: () -> Unit,
    onConfirm: (String, String) -> Unit
) {
    val ctx = androidx.compose.ui.platform.LocalContext.current
    val prefs = rememberPrefs(ctx)
    // remember is essential: without it every keystroke recomposes and
    // resets the field to the saved value, so a name can never be typed.
    var name by remember { mutableStateOf(prefs) }
    var code by remember { mutableStateOf("") }
    var error by remember { mutableStateOf("") }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = {
            Column {
                OutlinedTextField(
                    value = name, onValueChange = { name = it.take(24); error = "" },
                    label = { Text("Your name") }, singleLine = true,
                    modifier = Modifier.fillMaxWidth()
                )
                if (withCode) {
                    Spacer(Modifier.height(10.dp))
                    OutlinedTextField(
                        value = code,
                        onValueChange = { code = it.filter { ch -> ch.isDigit() }.take(4); error = "" },
                        label = { Text("4-digit room code") }, singleLine = true,
                        modifier = Modifier.fillMaxWidth()
                    )
                }
                if (error.isNotEmpty()) {
                    Spacer(Modifier.height(8.dp))
                    Text(error, color = BrandRed, fontSize = 13.sp)
                }
            }
        },
        confirmButton = {
            Button(onClick = {
                if (name.isBlank()) { error = "Please enter your name."; return@Button }
                if (withCode && code.length != 4) { error = "Enter the 4-digit room code."; return@Button }
                saveNamePref(ctx, name.trim())
                onConfirm(name.trim(), code)
            }, colors = ButtonDefaults.buttonColors(containerColor = BrandRed)) { Text(confirmText) }
        },
        dismissButton = { OutlinedButton(onClick = onDismiss) { Text("Cancel") } }
    )
}

private fun rememberPrefs(ctx: android.content.Context): String =
    ctx.getSharedPreferences("dbs", 0).getString("name", "") ?: ""

private fun saveNamePref(ctx: android.content.Context, name: String) {
    ctx.getSharedPreferences("dbs", 0).edit().putString("name", name).apply()
}
