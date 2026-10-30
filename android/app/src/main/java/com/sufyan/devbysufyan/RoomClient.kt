package com.sufyan.devbysufyan

import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import org.json.JSONObject
import java.util.concurrent.TimeUnit

/** A member's code document, exactly as the room server models it. */
data class RoomDoc(var language: String, var code: String, var version: Int = 0)

data class Member(val id: String, val name: String, val role: String, var doc: RoomDoc)

data class ChatMsg(val fromId: String, val fromName: String, val text: String, val ts: Long)

data class RoomSettings(
    var maxMembers: Int = 8,
    var membersCanEditEachOther: Boolean = false,
    var projectName: String = ""
) {
    companion object {
        fun fromJson(o: JSONObject): RoomSettings = RoomSettings(
            maxMembers = o.optInt("maxMembers", 8),
            membersCanEditEachOther = o.optBoolean("membersCanEditEachOther", false),
            projectName = o.optString("projectName", "")
        )
    }

    fun toJson(): JSONObject = JSONObject()
        .put("maxMembers", maxMembers)
        .put("membersCanEditEachOther", membersCanEditEachOther)
        .put("projectName", projectName)
}

fun docFromJson(o: JSONObject): RoomDoc = RoomDoc(
    language = o.optString("language", "cpp"),
    code = o.optString("code", ""),
    version = o.optInt("version", 0)
)

fun memberFromJson(o: JSONObject): Member = Member(
    id = o.optString("id"),
    name = o.optString("name"),
    role = o.optString("role", "member"),
    doc = docFromJson(o.optJSONObject("doc") ?: JSONObject())
)

fun chatFromJson(o: JSONObject): ChatMsg = ChatMsg(
    fromId = o.optString("fromId"),
    fromName = o.optString("fromName"),
    text = o.optString("text"),
    ts = o.optLong("ts", 0)
)

/**
 * Speaks the Dev by Sufyan room protocol (JSON over WebSocket) with the
 * live room server — the same server the PC app and the web app use, so a
 * phone and a PC can sit in the same room. All listener callbacks arrive
 * on OkHttp threads; the UI layer reposts them to the main thread.
 */
class RoomClient(private val listener: Listener) {

    interface Listener {
        fun onRoomJoined(selfId: String, role: String, code: String,
                         settings: RoomSettings, members: List<Member>,
                         chatLog: List<ChatMsg>)
        fun onMemberJoined(member: Member)
        fun onMemberLeft(memberId: String, name: String)
        fun onDocChanged(targetId: String, doc: RoomDoc)
        fun onDocDenied(targetId: String, doc: RoomDoc, message: String)
        fun onSettingsChanged(settings: RoomSettings)
        fun onChat(msg: ChatMsg)
        fun onTyping(fromId: String, fromName: String)
        fun onKicked(message: String)
        fun onRoomClosed(message: String)
        fun onError(message: String)
        fun onDisconnected()
    }

    private val http = OkHttpClient.Builder()
        .pingInterval(20, TimeUnit.SECONDS)
        .build()
    private var ws: WebSocket? = null
    private var inRoom = false
    private var leaving = false

    fun createRoom(name: String, doc: RoomDoc) = open {
        JSONObject().put("type", "create_room").put("name", name)
            .put("doc", JSONObject().put("language", doc.language).put("code", doc.code))
    }

    fun joinRoom(name: String, code: String, doc: RoomDoc) = open {
        JSONObject().put("type", "join_room").put("name", name).put("code", code)
            .put("doc", JSONObject().put("language", doc.language).put("code", doc.code))
    }

    private fun open(firstMessage: () -> JSONObject) {
        leaving = false
        val req = Request.Builder().url(SERVER_URL).build()
        ws = http.newWebSocket(req, object : WebSocketListener() {
            override fun onOpen(webSocket: WebSocket, response: Response) {
                webSocket.send(firstMessage().toString())
            }

            override fun onMessage(webSocket: WebSocket, text: String) {
                handle(text)
            }

            override fun onClosed(webSocket: WebSocket, code: Int, reason: String) {
                val wasInRoom = inRoom
                inRoom = false
                if (wasInRoom && !leaving) listener.onDisconnected()
            }

            override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
                val wasInRoom = inRoom
                inRoom = false
                if (!leaving) {
                    if (wasInRoom) listener.onDisconnected()
                    else listener.onError("Could not reach the room server. Check your internet connection.")
                }
            }
        })
    }

    private fun handle(text: String) {
        val m = try { JSONObject(text) } catch (e: Exception) { return }
        when (m.optString("type")) {
            "room_joined" -> {
                val room = m.optJSONObject("room") ?: return
                inRoom = true
                val members = mutableListOf<Member>()
                val arr = room.optJSONArray("members")
                if (arr != null) for (i in 0 until arr.length()) {
                    members.add(memberFromJson(arr.getJSONObject(i)))
                }
                val chatLog = mutableListOf<ChatMsg>()
                val log = room.optJSONArray("chatLog")
                if (log != null) for (i in 0 until log.length()) {
                    chatLog.add(chatFromJson(log.getJSONObject(i)))
                }
                listener.onRoomJoined(
                    m.optString("selfId"), m.optString("role", "member"),
                    room.optString("code"),
                    RoomSettings.fromJson(room.optJSONObject("settings") ?: JSONObject()),
                    members, chatLog
                )
            }
            "member_joined" -> m.optJSONObject("member")?.let { listener.onMemberJoined(memberFromJson(it)) }
            "member_left" -> listener.onMemberLeft(m.optString("memberId"), m.optString("name"))
            "doc_changed" -> m.optJSONObject("doc")?.let {
                listener.onDocChanged(m.optString("targetId"), docFromJson(it))
            }
            "doc_denied" -> m.optJSONObject("doc")?.let {
                listener.onDocDenied(m.optString("targetId"), docFromJson(it), m.optString("message"))
            }
            "settings_changed" -> m.optJSONObject("settings")?.let {
                listener.onSettingsChanged(RoomSettings.fromJson(it))
            }
            "chat_msg" -> listener.onChat(chatFromJson(m))
            "member_typing" -> listener.onTyping(m.optString("fromId"), m.optString("fromName"))
            "kicked" -> { inRoom = false; listener.onKicked(m.optString("message")) }
            "room_closed" -> { inRoom = false; listener.onRoomClosed(m.optString("message")) }
            "error" -> listener.onError(m.optString("message"))
            // Unknown message types are ignored, like every other client.
        }
    }

    private fun send(o: JSONObject) {
        ws?.send(o.toString())
    }

    fun sendDocUpdate(targetId: String, doc: RoomDoc) {
        if (!inRoom) return
        send(JSONObject().put("type", "doc_update").put("targetId", targetId)
            .put("language", doc.language).put("code", doc.code))
    }

    fun sendSettingsUpdate(settings: RoomSettings) {
        if (!inRoom) return
        send(JSONObject().put("type", "settings_update").put("settings", settings.toJson()))
    }

    fun kick(targetId: String) {
        if (!inRoom) return
        send(JSONObject().put("type", "kick").put("targetId", targetId))
    }

    /** Rooms v2 chat: the server echoes our own message back to us. */
    fun sendChat(text: String) {
        if (!inRoom) return
        val t = text.trim()
        if (t.isEmpty()) return
        send(JSONObject().put("type", "chat").put("text", t.take(500)))
    }

    fun sendTyping() {
        if (!inRoom) return
        send(JSONObject().put("type", "typing"))
    }

    fun leave() {
        leaving = true
        inRoom = false
        ws?.close(1000, null)
        ws = null
    }

    companion object {
        const val SERVER_URL = "wss://dev-by-sufyan.vercel.app/ws"
    }
}
