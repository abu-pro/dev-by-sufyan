package com.sufyan.devbysufyan

import android.os.Handler
import android.os.Looper
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import org.json.JSONObject
import java.util.concurrent.TimeUnit

/** A member's code document, exactly as the room server models it. */
data class RoomDoc(var language: String, var code: String, var version: Int = 0)

data class Member(val id: String, val name: String, val role: String, var doc: RoomDoc, var away: Boolean = false, var restricted: Boolean = false)

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
    doc = docFromJson(o.optJSONObject("doc") ?: JSONObject()),
    away = o.optBoolean("away", false),
    restricted = o.optBoolean("restricted", false)
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
 * phone and a PC can sit in the same room.
 *
 * v1.2.0 resilience: the server holds our seat for 90 s after a drop and
 * gives us a resumeToken in room_joined. Any unexpected socket loss now
 * triggers reconnect-with-backoff + resume instead of silently dumping
 * the user out of the room; connection state is reported to the UI via
 * onConnectionState so nothing ever fails silently again.
 * All listener callbacks arrive on OkHttp threads except state changes,
 * which are posted to the main thread by this class where noted; the UI
 * layer reposts everything to main anyway.
 */
class RoomClient(private val listener: Listener) {

    enum class ConnState { IDLE, CONNECTING, CONNECTED, RECONNECTING }

    interface Listener {
        fun onRoomJoined(selfId: String, role: String, code: String,
                         settings: RoomSettings, members: List<Member>,
                         chatLog: List<ChatMsg>)
        fun onMemberJoined(member: Member)
        fun onMemberLeft(memberId: String, name: String)
        fun onMemberAway(memberId: String, name: String)
        fun onMemberBack(memberId: String, name: String)
        fun onDocChanged(targetId: String, doc: RoomDoc)
        fun onDocDenied(targetId: String, doc: RoomDoc, message: String)
        fun onSettingsChanged(settings: RoomSettings)
        fun onChat(msg: ChatMsg)
        fun onTyping(fromId: String, fromName: String)
        fun onMemberRestricted(targetId: String, restricted: Boolean)
        fun onKicked(message: String)
        fun onRoomClosed(message: String)
        fun onResumeFailed(message: String)
        fun onError(message: String)
        fun onConnectionState(state: ConnState)
    }

    private val http = OkHttpClient.Builder()
        .pingInterval(20, TimeUnit.SECONDS)
        .connectTimeout(15, TimeUnit.SECONDS)
        .build()
    private val main = Handler(Looper.getMainLooper())
    private var ws: WebSocket? = null
    private var inRoom = false
    private var leaving = false

    // The seat the server is holding for us (set on every room_joined).
    private var seatCode = ""
    private var seatToken = ""
    // A create/join request waiting for the socket to open (also retried
    // if the socket dies before room_joined arrives).
    private var pendingFirst: JSONObject? = null
    private var reconnectAttempts = 0
    private var reconnectRunnable: Runnable? = null

    fun createRoom(name: String, doc: RoomDoc) = startFresh(
        JSONObject().put("type", "create_room").put("name", name)
            .put("doc", JSONObject().put("language", doc.language).put("code", doc.code))
    )

    fun joinRoom(name: String, code: String, doc: RoomDoc) = startFresh(
        JSONObject().put("type", "join_room").put("name", name).put("code", code)
            .put("doc", JSONObject().put("language", doc.language).put("code", doc.code))
    )

    private fun startFresh(firstMessage: JSONObject) {
        cancelReconnect()
        leaving = false
        inRoom = false
        seatCode = ""; seatToken = ""
        pendingFirst = firstMessage
        reconnectAttempts = 0
        listener.onConnectionState(ConnState.CONNECTING)
        closeSocketQuietly()
        openSocket()
    }

    private fun closeSocketQuietly() {
        try { ws?.close(1000, null) } catch (e: Exception) { /* stale */ }
        ws = null
    }

    private fun openSocket() {
        val req = Request.Builder().url(SERVER_URL).build()
        ws = http.newWebSocket(req, object : WebSocketListener() {
            override fun onOpen(webSocket: WebSocket, response: Response) {
                if (webSocket !== ws) return // a newer socket superseded us
                val first = pendingFirst
                if (first != null) {
                    webSocket.send(first.toString())
                } else if (seatToken.isNotEmpty()) {
                    webSocket.send(
                        JSONObject().put("type", "resume")
                            .put("code", seatCode).put("resumeToken", seatToken).toString()
                    )
                }
            }

            override fun onMessage(webSocket: WebSocket, text: String) {
                if (webSocket !== ws) return
                handle(text)
            }

            override fun onClosed(webSocket: WebSocket, code: Int, reason: String) {
                if (webSocket !== ws) return
                socketGone(null)
            }

            override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
                if (webSocket !== ws) return
                socketGone("Could not reach the room server. Check your internet connection.")
            }
        })
    }

    /** The live socket died. Either resume/retry, or surface the failure. */
    private fun socketGone(failureMessage: String?) {
        ws = null
        if (leaving) return
        val hadSeat = seatToken.isNotEmpty()
        val hadPending = pendingFirst != null
        inRoom = false
        if (!hadSeat && !hadPending) {
            if (failureMessage != null) listener.onError(failureMessage)
            listener.onConnectionState(ConnState.IDLE)
            return
        }
        scheduleReconnect(failureMessage)
    }

    private fun scheduleReconnect(failureMessage: String?) {
        if (leaving) return
        if (reconnectAttempts >= 6) {
            val hadSeat = seatToken.isNotEmpty()
            seatCode = ""; seatToken = ""; pendingFirst = null
            listener.onConnectionState(ConnState.IDLE)
            if (hadSeat) listener.onResumeFailed("Could not reconnect to the room — your seat has expired. Please join again.")
            else listener.onError(failureMessage ?: "Could not reach the room server. Check your internet connection.")
            return
        }
        reconnectAttempts++
        listener.onConnectionState(ConnState.RECONNECTING)
        val delay = minOf(15000L, 1000L * (1L shl (reconnectAttempts - 1)))
        val r = Runnable { if (!leaving) openSocket() }
        reconnectRunnable = r
        main.postDelayed(r, delay)
    }

    private fun cancelReconnect() {
        reconnectRunnable?.let { main.removeCallbacks(it) }
        reconnectRunnable = null
    }

    private fun handle(text: String) {
        val m = try { JSONObject(text) } catch (e: Exception) { return }
        when (m.optString("type")) {
            "room_joined" -> {
                val room = m.optJSONObject("room") ?: return
                inRoom = true
                pendingFirst = null
                reconnectAttempts = 0
                cancelReconnect()
                seatCode = room.optString("code")
                val tok = m.optString("resumeToken", "")
                if (tok.isNotEmpty()) seatToken = tok
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
                listener.onConnectionState(ConnState.CONNECTED)
                listener.onRoomJoined(
                    m.optString("selfId"), m.optString("role", "member"),
                    seatCode,
                    RoomSettings.fromJson(room.optJSONObject("settings") ?: JSONObject()),
                    members, chatLog
                )
            }
            "member_joined" -> m.optJSONObject("member")?.let { listener.onMemberJoined(memberFromJson(it)) }
            "member_left" -> listener.onMemberLeft(m.optString("memberId"), m.optString("name"))
            "member_away" -> listener.onMemberAway(m.optString("memberId"), m.optString("name"))
            "member_back" -> listener.onMemberBack(m.optString("memberId"), m.optString("name"))
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
            "member_restricted" -> listener.onMemberRestricted(m.optString("targetId"), m.optBoolean("restricted", false))
            "kicked" -> {
                inRoom = false; seatCode = ""; seatToken = ""; pendingFirst = null
                cancelReconnect()
                listener.onConnectionState(ConnState.IDLE)
                listener.onKicked(m.optString("message"))
            }
            "room_closed" -> {
                inRoom = false; seatCode = ""; seatToken = ""; pendingFirst = null
                cancelReconnect()
                listener.onConnectionState(ConnState.IDLE)
                listener.onRoomClosed(m.optString("message"))
            }
            "resume_failed" -> {
                inRoom = false; seatCode = ""; seatToken = ""; pendingFirst = null
                cancelReconnect()
                listener.onConnectionState(ConnState.IDLE)
                listener.onResumeFailed(m.optString("message"))
            }
            "error" -> listener.onError(m.optString("message"))
            // "pong" and unknown message types are ignored, like everywhere.
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

    /** v1.3.0: admin-only — restrict a member from editing others' code. */
    fun setMemberRestricted(targetId: String, restricted: Boolean) {
        if (!inRoom) return
        send(JSONObject().put("type", "member_restrict").put("targetId", targetId).put("restricted", restricted))
    }

    /** Rooms chat: the server echoes our own message back to us. */
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
        cancelReconnect()
        // v1.2.0: a deliberate leave removes our seat instantly server-side
        // (no 90 s grace), then the socket closes.
        if (inRoom) send(JSONObject().put("type", "leave_room"))
        inRoom = false
        seatCode = ""; seatToken = ""; pendingFirst = null
        closeSocketQuietly()
        listener.onConnectionState(ConnState.IDLE)
    }

    companion object {
        const val SERVER_URL = "wss://dev-by-sufyan.vercel.app/ws"
    }
}
