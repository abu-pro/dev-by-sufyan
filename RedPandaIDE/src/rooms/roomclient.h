/*
 * Copyright (C) 2026 Abu Sufyan
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef ROOMS_ROOMCLIENT_H
#define ROOMS_ROOMCLIENT_H

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QVector>

class QJsonObject;
class QTimer;
class QWebSocket;

struct RoomDoc {
    QString language; // "cpp" | "c" | "python"
    QString code;
    int version = 0;
};

struct RoomMemberInfo {
    QString id;
    QString name;
    QString role; // "admin" | "member"
    bool away = false; // v1.2.0: connection dropped, seat held for resume
    bool restricted = false; // v1.3.0: admin barred this member from editing others' code
    RoomDoc doc;
};

struct RoomSettings {
    int maxMembers = 8;
    bool membersCanEditEachOther = false;
    QString projectName; // Rooms v2: optional shared project title
};

struct RoomChatMessage {
    QString fromId;
    QString fromName;
    QString text;
    qint64 ts = 0; // milliseconds since epoch
};

// RoomClient speaks the Dev by Sufyan room protocol (JSON over WebSocket)
// with the live room server. It contains no UI code: all server events are
// forwarded as signals, all user actions are plain method calls.
//
// v1.2.0 connection model: the server holds a dropped member's seat for a
// 90 s grace period. When the socket drops unexpectedly while we are in a
// room, RoomClient reconnects with backoff and sends a "resume" request
// with the token from room_joined; the server answers with a fresh
// room_joined (same identity, current docs and chat history) and the
// session continues. "disconnected()" is therefore only emitted when the
// session is really over (gave up, or the drop happened outside a room).
class RoomClient : public QObject
{
    Q_OBJECT
public:
    explicit RoomClient(QObject *parent = nullptr);
    ~RoomClient() override;

    // The server endpoint. Defaults to the live server; can be overridden
    // with the DBS_ROOM_SERVER environment variable (used by tests).
    static QString serverUrl();

    void createRoom(const QString &name, const RoomDoc &doc);
    void joinRoom(const QString &name, const QString &code, const RoomDoc &doc);
    void sendDocUpdate(const QString &targetId, const RoomDoc &doc);
    void sendSettingsUpdate(const RoomSettings &settings);
    void kick(const QString &targetId);
    // v1.3.0: admin-only — restrict one member from editing other
    // members' code (or lift the restriction). The server enforces it;
    // the member_restricted broadcast confirms the change to everyone.
    void setMemberRestricted(const QString &targetId, bool restricted);
    // Rooms v2 chat: the server broadcasts the message back to everyone in
    // the room (including us), so there is no local echo here.
    void sendChat(const QString &text);
    // Rooms v2 typing indicator; the server throttles the relay (2 s).
    void sendTyping();
    // Deliberate exit: tells the server (leave_room) so the seat is freed
    // at once, then closes the connection and forgets the room.
    void leave();
    // Abruptly drops the socket without any protocol goodbye, simulating
    // a network failure. In a room, the reconnect/resume machinery takes
    // over. (Used by the protocol harness; harmless in production.)
    void abortConnection();

    bool isConnected() const;
    bool isInRoom() const { return mInRoom; }
    bool isReconnecting() const { return mReconnectActive; }
    QString selfId() const { return mSelfId; }
    QString role() const { return mRole; }
    bool isAdmin() const { return mRole == "admin"; }
    QString roomCode() const { return mRoomCode; }

signals:
    void roomJoined(const QString &selfId, const QString &role,
                    const QString &code, const RoomSettings &settings,
                    const QVector<RoomMemberInfo> &members,
                    const QVector<RoomChatMessage> &chatLog);
    void memberJoined(const RoomMemberInfo &member);
    void memberLeft(const QString &memberId, const QString &name);
    void memberAway(const QString &memberId, const QString &name);
    void memberBack(const QString &memberId, const QString &name);
    void docChanged(const QString &targetId, const RoomDoc &doc, const QString &byName);
    void docDenied(const QString &targetId, const RoomDoc &doc, const QString &message);
    void settingsChanged(const RoomSettings &settings);
    void chatReceived(const QString &fromId, const QString &fromName,
                      const QString &text, qint64 ts);
    void memberTyping(const QString &fromId, const QString &fromName);
    void memberRestricted(const QString &targetId, bool restricted);
    void kicked(const QString &message);
    void roomClosed(const QString &message);
    void errorReceived(const QString &message);
    // The socket dropped while in a room; a resume attempt sequence has
    // started. roomJoined + resumed() follow on success.
    void reconnecting();
    // A dropped session was successfully resumed (roomJoined has just
    // been emitted with the refreshed state).
    void resumed();
    // The server refused the resume (seat expired): the session is over.
    void resumeFailed(const QString &message);
    // The connection ended for good while connecting or in a room,
    // without a protocol-level goodbye and with no resume possible.
    void disconnected();

private slots:
    void onConnected();
    void onTextMessageReceived(const QString &message);
    void onDisconnected();
    void onReconnectTimer();
    void onPingTimer();

private:
    void openSocket();
    void sendJson(const QJsonObject &object);
    void resetState();
    void scheduleReconnect();

    QWebSocket *mSocket;
    QTimer *mReconnectTimer;
    QTimer *mPingTimer;
    bool mInRoom = false;
    bool mConnecting = false;
    bool mLeaving = false;
    QString mSelfId;
    QString mRole;
    QString mRoomCode;

    // v1.2.0 resume state
    QString mResumeToken;
    bool mReconnectActive = false;
    int mReconnectAttempt = 0;
    int mSocketGeneration = 0;
    QElapsedTimer mDropElapsed;

    // request that is sent as soon as the socket connects
    enum class PendingAction { None, Create, Join, Resume };
    PendingAction mPendingAction = PendingAction::None;
    QString mPendingName;
    QString mPendingCode;
    RoomDoc mPendingDoc;
};

#endif // ROOMS_ROOMCLIENT_H
