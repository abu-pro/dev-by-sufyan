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

} // namespace

RoomClient::RoomClient(QObject *parent) :
    QObject{parent},
    mSocket{new QWebSocket{QString(), QWebSocketProtocol::VersionLatest, this}}
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
    QTimer::singleShot(20000, this, [this]() {
        if (mConnecting && !isConnected()) {
            mConnecting = false;
            mSocket->abort();
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

void RoomClient::leave()
{
    mLeaving = true;
    resetState();
    if (mSocket->state() != QAbstractSocket::UnconnectedState)
        mSocket->close();
}

void RoomClient::resetState()
{
    mInRoom = false;
    mConnecting = false;
    mPendingAction = PendingAction::None;
    mSelfId.clear();
    mRole.clear();
    mRoomCode.clear();
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
    if (mPendingAction == PendingAction::Create)
        object["type"] = "create_room";
    else
        object["type"] = "join_room";
    object["name"] = mPendingName;
    if (mPendingAction == PendingAction::Join)
        object["code"] = mPendingCode;
    object["doc"] = docToJson(mPendingDoc);
    mPendingAction = PendingAction::None;
    sendJson(object);
}

void RoomClient::onDisconnected()
{
    bool wasActive = mInRoom || mConnecting;
    bool wasLeaving = mLeaving;
    mLeaving = false;
    resetState();
    if (wasActive && !wasLeaving)
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
        mSelfId = object.value("selfId").toString();
        mRole = object.value("role").toString("member");
        mRoomCode = room.value("code").toString();
        mInRoom = true;
        QVector<RoomMemberInfo> members;
        for (const QJsonValue &value : room.value("members").toArray())
            members.append(memberFromJson(value.toObject()));
        QVector<RoomChatMessage> chatLog;
        for (const QJsonValue &value : room.value("chatLog").toArray())
            chatLog.append(chatFromJson(value.toObject()));
        emit roomJoined(mSelfId, mRole, mRoomCode,
                        settingsFromJson(room.value("settings").toObject()),
                        members, chatLog);
    } else if (type == "member_joined") {
        emit memberJoined(memberFromJson(object.value("member").toObject()));
    } else if (type == "member_left") {
        emit memberLeft(object.value("memberId").toString(),
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
        RoomChatMessage message = chatFromJson(object);
        emit chatReceived(message.fromId, message.fromName, message.text,
                          message.ts);
    } else if (type == "member_typing") {
        emit memberTyping(object.value("fromId").toString(),
                          object.value("fromName").toString());
    } else if (type == "kicked") {
        resetState();
        emit kicked(object.value("message").toString());
    } else if (type == "room_closed") {
        resetState();
        emit roomClosed(object.value("message").toString());
    } else if (type == "error") {
        emit errorReceived(object.value("message").toString());
    }
}
