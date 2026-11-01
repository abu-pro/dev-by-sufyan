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
#include "roomclient.h"

#include <QAbstractSocket>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxyFactory>
#include <QNetworkProxyQuery>
#include <QProcessEnvironment>
#include <QSettings>
#include <QTimer>
#include <QWebSocket>

namespace {

QJsonObject docToJson(const RoomDoc &doc)
{
    QJsonObject object;
    object["language"] = doc.language;
    object["code"] = doc.code;
    return object;
}

RoomDoc docFromJson(const QJsonObject &object)
{
    RoomDoc doc;
    doc.language = object.value("language").toString("cpp");
    doc.code = object.value("code").toString();
    doc.version = object.value("version").toInt(0);
    return doc;
}

RoomMemberInfo memberFromJson(const QJsonObject &object)
{
    RoomMemberInfo member;
    member.id = object.value("id").toString();
    member.name = object.value("name").toString();
    member.role = object.value("role").toString("member");
    member.away = object.value("away").toBool(false);
    member.restricted = object.value("restricted").toBool(false);
    member.doc = docFromJson(object.value("doc").toObject());
    return member;
}

RoomSettings settingsFromJson(const QJsonObject &object)
{
    RoomSettings settings;
    settings.maxMembers = object.value("maxMembers").toInt(8);
    settings.membersCanEditEachOther =
            object.value("membersCanEditEachOther").toBool(false);
    settings.projectName = object.value("projectName").toString();
    return settings;
}

RoomChatMessage chatFromJson(const QJsonObject &object)
{
    RoomChatMessage message;
    message.fromId = object.value("fromId").toString();
    message.fromName = object.value("fromName").toString();
    message.text = object.value("text").toString();
    message.ts = object.value("ts").toVariant().toLongLong();
    return message;
}

// Reconnect backoff: quick first retries, then a steady 15 s rhythm.
// The server's resume grace is 90 s; the client gives up after ~70 s of
// wall-clock outage (checked in onDisconnected), leaving margin for one
// last attempt to land inside the grace window.
const int RECONNECT_DELAYS_MS[] = {1000, 2000, 4000, 8000, 15000};
const qint64 RECONNECT_GIVE_UP_MS = 70000;

} // namespace

RoomClient::RoomClient(QObject *parent) :
    QObject{parent},
    mSocket{new QWebSocket{QString(), QWebSocketProtocol::VersionLatest, this}},
    mReconnectTimer{new QTimer{this}},
    mPingTimer{new QTimer{this}},
    mRttTimer{new QTimer{this}}
{
    // Honor the OS / environment proxy settings (needed on networks
    // where outgoing connections must go through a proxy).
    static const bool proxyConfigured = []() {
        QNetworkProxyFactory::setUseSystemConfiguration(true);
        return true;
    }();
    Q_UNUSED(proxyConfigured);
    connect(mSocket, &QWebSocket::connected, this, &RoomClient::onConnected);
    connect(mSocket, &QWebSocket::textMessageReceived,
            this, &RoomClient::onTextMessageReceived);
    connect(mSocket, &QWebSocket::disconnected, this, &RoomClient::onDisconnected);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    connect(mSocket, &QWebSocket::errorOccurred,
            this, [this](QAbstractSocket::SocketError) {
        if (!mInRoom && mConnecting)
            emit errorReceived(mSocket->errorString());
    });
#else
    connect(mSocket,
            static_cast<void (QWebSocket::*)(QAbstractSocket::SocketError)>(
                &QWebSocket::error),
            this, [this](QAbstractSocket::SocketError) {
        if (!mInRoom && mConnecting)
            emit errorReceived(mSocket->errorString());
    });
#endif
    mReconnectTimer->setSingleShot(true);
    connect(mReconnectTimer, &QTimer::timeout,
            this, &RoomClient::onReconnectTimer);
    // Application-level keepalive: the server answers {type:"pong"} and
    // ignores nothing else about it; the traffic keeps proxies and NAT
    // mappings warm on flaky networks.
    mPingTimer->setInterval(25000);
    connect(mPingTimer, &QTimer::timeout, this, &RoomClient::onPingTimer);
    // v1.5.0: while in a room, probe every 5 s so the panel's ping
    // indicator stays live (the keepalive above is only every 25 s).
    mRttTimer->setInterval(5000);
    connect(mRttTimer, &QTimer::timeout, this, &RoomClient::onRttTimer);
}

RoomClient::~RoomClient() = default;

QString RoomClient::serverUrl()
{
    QString url = QProcessEnvironment::systemEnvironment()
            .value("DBS_ROOM_SERVER");
    if (url.isEmpty())
        url = "wss://dev-by-sufyan.vercel.app/ws";
    return url;
}

bool RoomClient::isConnected() const
{
    return mSocket->state() == QAbstractSocket::ConnectedState;
}

void RoomClient::openSocket()
{
    if (mSocket->state() == QAbstractSocket::ConnectedState
            || mSocket->state() == QAbstractSocket::ConnectingState)
        return;
    mConnecting = true;
    // QWebSocket does not reliably pick up the factory proxy on its own;
    // resolve the system proxy for this URL and set it explicitly.
    const QList<QNetworkProxy> proxies =
            QNetworkProxyFactory::systemProxyForQuery(
                QNetworkProxyQuery{QUrl{serverUrl()}});
    if (!proxies.isEmpty())
        mSocket->setProxy(proxies.constFirst());
    mSocket->open(QUrl(serverUrl()));
    // Connect watchdog, tagged with the socket generation so a stale
    // timer can never abort a newer attempt.
    const int generation = ++mSocketGeneration;
    QTimer::singleShot(20000, this, [this, generation]() {
        if (generation != mSocketGeneration)
            return;
        if (mConnecting && !isConnected()) {
            mConnecting = false;
            mSocket->abort();
            if (!mReconnectActive)
                emit errorReceived(
                            tr("Connection to the room server timed out."));
        }
    });
}

void RoomClient::createRoom(const QString &name, const RoomDoc &doc)
{
    mPendingAction = PendingAction::Create;
    mPendingName = name;
    mPendingCode.clear();
    mPendingDoc = doc;
    openSocket();
    if (isConnected())
        onConnected();
}

void RoomClient::joinRoom(const QString &name, const QString &code,
                          const RoomDoc &doc)
{
    mPendingAction = PendingAction::Join;
    mPendingName = name;
    mPendingCode = code;
    mPendingDoc = doc;
    // v1.3.1 — if a previous visit left us a retained seat for this
    // exact code, offer its token with the join: the server restores
    // our old identity/role/doc instead of entering us as a new
    // person. A token that matches nothing is a plain fresh join.
    QSettings settings;
    mPendingJoinToken =
            settings.value("rooms/lastCode").toString() == code
                ? settings.value("rooms/lastToken").toString()
                : QString();
    openSocket();
    if (isConnected())
        onConnected();
}

void RoomClient::sendDocUpdate(const QString &targetId, const RoomDoc &doc)
{
    if (!mInRoom)
        return;
    QJsonObject object;
    object["type"] = "doc_update";
    object["targetId"] = targetId;
    object["language"] = doc.language;
    object["code"] = doc.code;
    sendJson(object);
}

void RoomClient::sendSettingsUpdate(const RoomSettings &settings)
{
    if (!mInRoom)
        return;
    QJsonObject settingsObject;
    settingsObject["maxMembers"] = settings.maxMembers;
    settingsObject["membersCanEditEachOther"] =
            settings.membersCanEditEachOther;
    settingsObject["projectName"] = settings.projectName;
    QJsonObject object;
    object["type"] = "settings_update";
    object["settings"] = settingsObject;
    sendJson(object);
}

void RoomClient::sendChat(const QString &text)
{
    if (!mInRoom)
        return;
    QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return;
    QJsonObject object;
    object["type"] = "chat";
    object["text"] = trimmed.left(500);
    sendJson(object);
}

void RoomClient::sendTyping()
{
    if (!mInRoom)
        return;
    QJsonObject object;
    object["type"] = "typing";
    sendJson(object);
}

void RoomClient::kick(const QString &targetId)
{
    if (!mInRoom)
        return;
    QJsonObject object;
    object["type"] = "kick";
    object["targetId"] = targetId;
    sendJson(object);
}

void RoomClient::setMemberRestricted(const QString &targetId, bool restricted)
{
    if (!mInRoom)
        return;
    QJsonObject object;
    object["type"] = "member_restrict";
    object["targetId"] = targetId;
    object["restricted"] = restricted;
    sendJson(object);
}

void RoomClient::sendCursor(int line, int col)
{
    if (!mInRoom)
        return;
    QJsonObject object;
    object["type"] = "cursor";
    object["line"] = line;
    object["col"] = col;
    sendJson(object);
}

void RoomClient::sendPointLine(const QString &targetId, int line)
{
    if (!mInRoom)
        return;
    QJsonObject object;
    object["type"] = "point_line";
    object["targetId"] = targetId;
    object["line"] = line;
    sendJson(object);
}

void RoomClient::leave()
{
    mLeaving = true;
    // Deliberate exit: peers see us leave at once, but since v1.3.1 the
    // server RETAINS the seat for a while and our saved token
    // (rooms/lastCode + rooms/lastToken) deliberately survives, so
    // joining the same code again restores this identity and doc.
    if (mInRoom && isConnected()) {
        QJsonObject object;
        object["type"] = "leave_room";
        sendJson(object);
    }
    resetState();
    if (mSocket->state() != QAbstractSocket::UnconnectedState)
        mSocket->close();
    else
        mLeaving = false;
}

void RoomClient::abortConnection()
{
    if (mSocket->state() != QAbstractSocket::UnconnectedState)
        mSocket->abort();
}

// v1.3.1 — the saved seat (last room code + resume token) lives in
// QSettings so it survives restarts. It is refreshed on every
// room_joined and cleared only when the seat is definitively gone
// (resume refused, kicked, room closed) — never on a plain Leave.
void RoomClient::saveSeat()
{
    QSettings settings;
    settings.setValue("rooms/lastCode", mRoomCode);
    settings.setValue("rooms/lastToken", mResumeToken);
}

void RoomClient::clearSavedSeat()
{
    QSettings settings;
    settings.remove("rooms/lastCode");
    settings.remove("rooms/lastToken");
}

void RoomClient::resetState()
{
    mInRoom = false;
    mConnecting = false;
    mPendingAction = PendingAction::None;
    mSelfId.clear();
    mRole.clear();
    mRoomCode.clear();
    mResumeToken.clear();
    mReconnectActive = false;
    mReconnectAttempt = 0;
    mReconnectTimer->stop();
    mPingTimer->stop();
    mRttTimer->stop();
    mPingOutstanding = false;
}

void RoomClient::sendJson(const QJsonObject &object)
{
    if (mSocket->state() == QAbstractSocket::ConnectedState)
        mSocket->sendTextMessage(
                    QString::fromUtf8(QJsonDocument(object).toJson(
                                          QJsonDocument::Compact)));
}

void RoomClient::onConnected()
{
    mConnecting = false;
    if (mPendingAction == PendingAction::None)
        return;
    QJsonObject object;
    if (mPendingAction == PendingAction::Resume) {
        object["type"] = "resume";
        object["code"] = mRoomCode;
        object["resumeToken"] = mResumeToken;
    } else {
        if (mPendingAction == PendingAction::Create)
            object["type"] = "create_room";
        else
            object["type"] = "join_room";
        object["name"] = mPendingName;
        if (mPendingAction == PendingAction::Join) {
            object["code"] = mPendingCode;
            if (!mPendingJoinToken.isEmpty())
                object["resumeToken"] = mPendingJoinToken;
            mPendingJoinToken.clear();
        }
        object["doc"] = docToJson(mPendingDoc);
    }
    mPendingAction = PendingAction::None;
    sendJson(object);
}

void RoomClient::scheduleReconnect()
{
    if (mReconnectTimer->isActive())
        return;
    int index = qMin(mReconnectAttempt,
                     int(sizeof(RECONNECT_DELAYS_MS) / sizeof(int)) - 1);
    mReconnectTimer->start(RECONNECT_DELAYS_MS[index]);
}

void RoomClient::onReconnectTimer()
{
    if (!mReconnectActive || mLeaving)
        return;
    ++mReconnectAttempt;
    mPendingAction = PendingAction::Resume;
    openSocket();
    if (isConnected())
        onConnected();
}

void RoomClient::sendPing()
{
    if (!mInRoom || !isConnected())
        return;
    QJsonObject object;
    object["type"] = "ping";
    sendJson(object);
    // Only one measurement in flight: pongs are not numbered, so a
    // newer ping must not move the stamp while a pong is still owed.
    if (!mPingOutstanding) {
        mPingOutstanding = true;
        mPingStamp.start();
    }
}

void RoomClient::onPingTimer()
{
    sendPing();
}

void RoomClient::onRttTimer()
{
    if (!mInRoom || !isConnected())
        return;
    if (mPingOutstanding && mPingStamp.elapsed() > 10000) {
        // The last ping was never answered: report "no reply" for
        // the indicator, then start a fresh measurement below.
        mPingOutstanding = false;
        emit pingRttChanged(-1);
    }
    sendPing();
}

void RoomClient::onDisconnected()
{
    const bool wasActive = mInRoom || mConnecting;
    mConnecting = false;
    mPingTimer->stop();
    mRttTimer->stop();
    if (mLeaving) {
        mLeaving = false;
        resetState();
        return;
    }
    // Unexpected drop while holding a seat: the server keeps the seat
    // for its grace period, so try to resume instead of ending the
    // session. Only a drop outside a room (or a failed initial connect)
    // falls through to the terminal path.
    if (mInRoom && !mResumeToken.isEmpty()) {
        if (!mReconnectActive) {
            mReconnectActive = true;
            mReconnectAttempt = 0;
            mDropElapsed.start();
            emit reconnecting();
            emit pingRttChanged(-1); // v1.5.0: indicator shows no reply
        }
        if (mDropElapsed.elapsed() > RECONNECT_GIVE_UP_MS) {
            resetState();
            emit disconnected();
            return;
        }
        scheduleReconnect();
        return;
    }
    resetState();
    if (wasActive)
        emit disconnected();
}

void RoomClient::onTextMessageReceived(const QString &message)
{
    QJsonParseError parseError;
    QJsonDocument document = QJsonDocument::fromJson(
                message.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return;
    QJsonObject object = document.object();
    QString type = object.value("type").toString();

    if (type == "room_joined") {
        QJsonObject room = object.value("room").toObject();
        bool wasResume = mReconnectActive;
        mSelfId = object.value("selfId").toString();
        mRole = object.value("role").toString("member");
        mRoomCode = room.value("code").toString();
        mResumeToken = object.value("resumeToken").toString();
        mInRoom = true;
        mReconnectActive = false;
        mReconnectAttempt = 0;
        mReconnectTimer->stop();
        mPingTimer->start();
        mRttTimer->start();
        saveSeat();
        // A join (not a reconnect-resume) that the server marked as a
        // seat restore: the panel greets the user with "Welcome back".
        const bool restoredSeat =
                object.value("resumed").toBool(false) && !wasResume;
        QVector<RoomMemberInfo> members;
        for (const QJsonValue &value : room.value("members").toArray())
            members.append(memberFromJson(value.toObject()));
        QVector<RoomChatMessage> chatLog;
        for (const QJsonValue &value : room.value("chatLog").toArray())
            chatLog.append(chatFromJson(value.toObject()));
        emit roomJoined(mSelfId, mRole, mRoomCode,
                        settingsFromJson(room.value("settings").toObject()),
                        members, chatLog);
        if (wasResume)
            emit resumed();
        if (restoredSeat)
            emit seatRestored();
    } else if (type == "resume_failed") {
        QString failMessage = object.value("message").toString();
        clearSavedSeat();
        resetState();
        emit resumeFailed(failMessage.isEmpty()
            ? tr("Your seat in the room has expired. Please join again.")
            : failMessage);
    } else if (type == "member_joined") {
        emit memberJoined(memberFromJson(object.value("member").toObject()));
    } else if (type == "member_left") {
        emit memberLeft(object.value("memberId").toString(),
                        object.value("name").toString());
    } else if (type == "member_away") {
        emit memberAway(object.value("memberId").toString(),
                        object.value("name").toString());
    } else if (type == "member_back") {
        emit memberBack(object.value("memberId").toString(),
                        object.value("name").toString());
    } else if (type == "doc_changed") {
        emit docChanged(object.value("targetId").toString(),
                        docFromJson(object.value("doc").toObject()),
                        object.value("byName").toString());
    } else if (type == "doc_denied") {
        emit docDenied(object.value("targetId").toString(),
                       docFromJson(object.value("doc").toObject()),
                       object.value("message").toString());
    } else if (type == "settings_changed") {
        emit settingsChanged(
                    settingsFromJson(object.value("settings").toObject()));
    } else if (type == "chat_msg") {
        RoomChatMessage chatMessage = chatFromJson(object);
        emit chatReceived(chatMessage.fromId, chatMessage.fromName,
                          chatMessage.text, chatMessage.ts);
    } else if (type == "member_typing") {
        emit memberTyping(object.value("fromId").toString(),
                          object.value("fromName").toString());
    } else if (type == "member_cursor") {
        emit memberCursor(object.value("id").toString(),
                          object.value("line").toInt(),
                          object.value("col").toInt());
    } else if (type == "line_pointed") {
        emit linePointed(object.value("by").toString(),
                         object.value("targetId").toString(),
                         object.value("line").toInt());
    } else if (type == "member_restricted") {
        emit memberRestricted(object.value("targetId").toString(),
                              object.value("restricted").toBool(false));
    } else if (type == "kicked") {
        clearSavedSeat();
        resetState();
        emit kicked(object.value("message").toString());
    } else if (type == "room_closed") {
        clearSavedSeat();
        resetState();
        emit roomClosed(object.value("message").toString());
    } else if (type == "error") {
        emit errorReceived(object.value("message").toString());
    } else if (type == "pong") {
        // v1.5.0: answer to our app-level ping — turn it into the
        // round-trip time for the Rooms panel's ping indicator.
        if (mPingOutstanding) {
            mPingOutstanding = false;
            emit pingRttChanged(int(mPingStamp.elapsed()));
        }
    }
}
