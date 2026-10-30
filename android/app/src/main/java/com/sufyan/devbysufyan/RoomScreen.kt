package com.sufyan.devbysufyan

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

@Composable
fun RoomScreen(state: AppState) {
    val ctx = LocalContext.current
    Column(
        modifier = Modifier.fillMaxSize().imePadding().verticalScroll(rememberScrollState())
            .padding(16.dp)
    ) {
        // ---- header: big code, project, role ----
        Card(
            colors = CardDefaults.cardColors(containerColor = Color.White),
            shape = RoundedCornerShape(16.dp),
            modifier = Modifier.fillMaxWidth()
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("Room code", color = Color(0xFF6B7280), fontSize = 13.sp)
                    Spacer(Modifier.weight(1f))
                    Text(
                        if (state.isAdmin()) "You are the Admin 👑" else "You are a member",
                        color = BrandRed, fontWeight = FontWeight.SemiBold, fontSize = 13.sp
                    )
                }
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(state.roomCode, fontSize = 40.sp, fontWeight = FontWeight.ExtraBold,
                        letterSpacing = 6.sp)
                    Spacer(Modifier.width(10.dp))
                    OutlinedButton(onClick = {
                        val cm = ctx.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                        cm.setPrimaryClip(ClipData.newPlainText("room code", state.roomCode))
                        state.toast("Room code ${state.roomCode} copied")
                    }) { Text("Copy") }
                }
                if (state.settings.projectName.isNotEmpty()) {
                    Spacer(Modifier.height(8.dp))
                    Text(
                        "📁 Project: ${state.settings.projectName}",
                        fontWeight = FontWeight.SemiBold, fontSize = 15.sp, color = BrandRedDark,
                        modifier = Modifier.background(Color(0xFFFDECEA), RoundedCornerShape(8.dp))
                            .padding(horizontal = 10.dp, vertical = 6.dp)
                    )
                }
                Spacer(Modifier.height(12.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    Button(
                        onClick = { state.viewingId = state.selfId; state.screen = Screen.EDITOR },
                        colors = ButtonDefaults.buttonColors(containerColor = BrandRed),
                        shape = RoundedCornerShape(10.dp),
                        modifier = Modifier.weight(1f).height(48.dp)
                    ) { Text("Open my code", fontSize = 15.sp, fontWeight = FontWeight.SemiBold) }
                    OutlinedButton(
                        onClick = { state.leaveRoom() },
                        shape = RoundedCornerShape(10.dp),
                        modifier = Modifier.weight(1f).height(48.dp)
                    ) { Text("Leave Room", color = BrandRed, fontSize = 15.sp) }
                }
            }
        }

        Spacer(Modifier.height(18.dp))
        Text("Members (${state.members.size})", fontWeight = FontWeight.Bold, fontSize = 17.sp)
        Spacer(Modifier.height(8.dp))

        // ---- members ----
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            for (m in state.members) {
                MemberRow(state, m)
            }
        }

        Spacer(Modifier.height(18.dp))

        // ---- chat ----
        ChatSection(state)

        // ---- admin settings ----
        if (state.isAdmin()) {
            Spacer(Modifier.height(18.dp))
            AdminSettings(state)
        }

        Spacer(Modifier.height(20.dp))
        Text(
            "Tip: tap a member to see their code. Chat and code sync live between phones and PCs.",
            color = Color(0xFF9AA0A6), fontSize = 12.5.sp
        )
    }
}

@Composable
private fun MemberRow(state: AppState, m: Member) {
    val isSelf = m.id == state.selfId
    Card(
        colors = CardDefaults.cardColors(containerColor = Color.White),
        shape = RoundedCornerShape(12.dp),
        modifier = Modifier.fillMaxWidth()
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Box(modifier = Modifier.size(12.dp).clip(CircleShape).background(memberColor(m.id)))
            Spacer(Modifier.width(10.dp))
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    m.name + if (isSelf) " (You)" else "",
                    fontWeight = FontWeight.SemiBold, fontSize = 15.sp
                )
                Text(
                    (LANG_LABELS[m.doc.language] ?: m.doc.language) +
                        if (m.role == "admin") " · Admin" else "",
                    fontSize = 12.sp, color = Color(0xFF6B7280)
                )
            }
            if (!isSelf) {
                TextButton(onClick = {
                    state.viewingId = m.id
                    state.screen = Screen.EDITOR
                }) { Text(if (state.canEdit(m.id)) "Open" else "View", color = BrandRed, fontWeight = FontWeight.SemiBold) }
                if (state.isAdmin()) {
                    TextButton(onClick = { state.client.kick(m.id) }) {
                        Text("Kick", color = Color(0xFFB3261E))
                    }
                }
            }
        }
    }
}

@Composable
private fun ChatSection(state: AppState) {
    var input by remember { mutableStateOf("") }
    var lastTypingSent by remember { mutableStateOf(0L) }
    val listState = rememberLazyListState()

    LaunchedEffect(state.chat.size) {
        if (state.chat.isNotEmpty()) listState.animateScrollToItem(state.chat.size - 1)
    }

    Card(
        colors = CardDefaults.cardColors(containerColor = Color.White),
        shape = RoundedCornerShape(16.dp),
        modifier = Modifier.fillMaxWidth()
    ) {
        Column(modifier = Modifier.padding(14.dp)) {
            Text("💬 Room Chat", fontWeight = FontWeight.Bold, fontSize = 17.sp)
            Spacer(Modifier.height(10.dp))
            if (state.chat.isEmpty()) {
                Text("No messages yet — say hello! 👋", color = Color(0xFF9AA0A6), fontSize = 13.5.sp,
                    modifier = Modifier.padding(vertical = 8.dp))
            } else {
                LazyColumn(
                    state = listState,
                    modifier = Modifier.fillMaxWidth().height(260.dp),
                    verticalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    items(state.chat) { msg -> ChatBubble(state, msg) }
                }
            }
            Box(modifier = Modifier.height(20.dp).padding(top = 4.dp)) {
                if (state.typingText.isNotEmpty()) {
                    Text(state.typingText, fontSize = 12.5.sp, color = Color(0xFF6B7280),
                        fontStyle = androidx.compose.ui.text.font.FontStyle.Italic)
                }
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                OutlinedTextField(
                    value = input,
                    onValueChange = {
                        input = it
                        val now = System.currentTimeMillis()
                        if (now - lastTypingSent > 2000) {
                            lastTypingSent = now
                            state.client.sendTyping()
                        }
                    },
                    placeholder = { Text("Type a message…") },
                    singleLine = true,
                    modifier = Modifier.weight(1f)
                )
                Spacer(Modifier.width(8.dp))
                Button(
                    onClick = {
                        val t = input.trim()
                        if (t.isNotEmpty()) {
                            state.client.sendChat(t)
                            input = ""
                        }
                    },
                    colors = ButtonDefaults.buttonColors(containerColor = BrandRed),
                    shape = RoundedCornerShape(10.dp),
                    modifier = Modifier.height(52.dp)
                ) { Text("Send", fontWeight = FontWeight.SemiBold) }
            }
        }
    }
}

@Composable
private fun ChatBubble(state: AppState, msg: ChatMsg) {
    val own = msg.fromId == state.selfId
    val time = remember(msg.ts) {
        if (msg.ts > 0) SimpleDateFormat("h:mm a", Locale.getDefault()).format(Date(msg.ts)) else ""
    }
    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = if (own) Arrangement.End else Arrangement.Start) {
        Column(
            modifier = Modifier.fillMaxWidth(0.85f)
                .background(
                    if (own) BrandRed else Color(0xFFEEF3F8),
                    RoundedCornerShape(14.dp, 14.dp, 14.dp, if (own) 14.dp else 4.dp)
                )
                .padding(horizontal = 11.dp, vertical = 7.dp)
        ) {
            if (!own) {
                Text(msg.fromName, fontWeight = FontWeight.Bold, fontSize = 12.sp,
                    color = memberColor(msg.fromId))
            }
            Text(msg.text, fontSize = 14.5.sp, color = if (own) Color.White else Color(0xFF1B1B1F))
            if (time.isNotEmpty()) {
                Text(time, fontSize = 10.sp,
                    color = if (own) Color(0xFFFFD9D9) else Color(0xFF9AA0A6),
                    modifier = Modifier.align(Alignment.End))
            }
        }
    }
}

@Composable
private fun AdminSettings(state: AppState) {
    var project by remember(state.settings.projectName) { mutableStateOf(state.settings.projectName) }
    var maxText by remember(state.settings.maxMembers) { mutableStateOf(state.settings.maxMembers.toString()) }
    var peer by remember(state.settings.membersCanEditEachOther) { mutableStateOf(state.settings.membersCanEditEachOther) }
    Card(
        colors = CardDefaults.cardColors(containerColor = Color.White),
        shape = RoundedCornerShape(16.dp),
        modifier = Modifier.fillMaxWidth()
    ) {
        Column(modifier = Modifier.padding(14.dp)) {
            Text("Admin settings", fontWeight = FontWeight.Bold, fontSize = 17.sp)
            Spacer(Modifier.height(10.dp))
            OutlinedTextField(
                value = project, onValueChange = { project = it.take(60) },
                label = { Text("Project name (shown to everyone)") },
                singleLine = true, modifier = Modifier.fillMaxWidth()
            )
            Spacer(Modifier.height(10.dp))
            OutlinedTextField(
                value = maxText,
                onValueChange = { maxText = it.filter { ch -> ch.isDigit() }.take(2) },
                label = { Text("Max members (2–50)") },
                singleLine = true, modifier = Modifier.fillMaxWidth()
            )
            Spacer(Modifier.height(6.dp))
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Members can edit each other's code", modifier = Modifier.weight(1f), fontSize = 14.5.sp)
                Switch(checked = peer, onCheckedChange = { peer = it })
            }
            Spacer(Modifier.height(10.dp))
            Button(
                onClick = {
                    val s = RoomSettings(
                        maxMembers = maxText.toIntOrNull()?.coerceIn(2, 50) ?: 8,
                        membersCanEditEachOther = peer,
                        projectName = project.trim()
                    )
                    state.settings = s
                    state.client.sendSettingsUpdate(s)
                    state.toast("Room settings updated")
                },
                colors = ButtonDefaults.buttonColors(containerColor = BrandRed),
                shape = RoundedCornerShape(10.dp),
                modifier = Modifier.fillMaxWidth().height(48.dp)
            ) { Text("Apply settings", fontWeight = FontWeight.SemiBold) }
            Text("Your own code is always view-only for everyone else.",
                fontSize = 12.sp, color = Color(0xFF9AA0A6), modifier = Modifier.padding(top = 8.dp))
        }
    }
}
