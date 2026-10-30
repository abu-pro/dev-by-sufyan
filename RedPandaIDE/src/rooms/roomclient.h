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

#include <QObject>
#include <QString>
#include <QVector>

class QJsonObject;
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
    RoomDoc doc;
};

struct RoomSettings {
    int maxMembers = 8;
    bool membersCanEditEachOther = false;
};

// RoomClient speaks the Dev by Sufyan room protocol (JSON over WebSocket)
// with the live room server. It contains no UI code: all server events are
// forwarded as signals, all user actions are plain method calls.
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
    // Close the connection and forget the room. Emits nothing.
    void leave();

    bool isConnected() const;
    bool isInRoom() const { return mInRoom; }
    QString selfId() const { return mSelfId; }
    QString role() const { return mRole; }
    bool isAdmin() const { return mRole == "admin"; }
    QString roomCode() const { return mRoomCode; }

signals:
    void roomJoined(const QString &selfId, const QString &role,
                    const QString &code, const RoomSettings &settings,
                    const QVector<RoomMemberInfo> &members);
    void memberJoined(const RoomMemberInfo &member);
    void memberLeft(const QString &memberId, const QString &name);
    void docChanged(const QString &targetId, const RoomDoc &doc, const QString &byName);
    void docDenied(const QString &targetId, const RoomDoc &doc, const QString &message);
    void settingsChanged(const RoomSettings &settings);
    void kicked(const QString &message);
    void roomClosed(const QString &message);
    void errorReceived(const QString &message);
    // The socket closed while we were connecting or in a room, without a
    // protocol-level goodbye (network drop, server restart, we were kicked
    // and the server closed the socket right after the "kicked" message).
    void disconnected();

private slots:
    void onConnected();
    void onTextMessageReceived(const QString &message);
    void onDisconnected();

private:
    void openSocket();
    void sendJson(const QJsonObject &object);
    void resetState();

    QWebSocket *mSocket;
    bool mInRoom = false;
    bool mConnecting = false;
    bool mLeaving = false;
    QString mSelfId;
    QString mRole;
    QString mRoomCode;

    // request that is sent as soon as the socket connects
    enum class PendingAction { None, Create, Join };
    PendingAction mPendingAction = PendingAction::None;
    QString mPendingName;
    QString mPendingCode;
    RoomDoc mPendingDoc;
};

#endif // ROOMS_ROOMCLIENT_H
