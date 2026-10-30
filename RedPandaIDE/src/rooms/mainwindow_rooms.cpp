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
// Rooms integration for MainWindow (Dev by Sufyan).
//
// This file implements the MainWindow methods declared in mainwindow.h
// that connect the IDE with the live room server: the editor that is
// active when a room is created/joined becomes "my" room document, and
// every other member can be opened in a scratch editor backed by a temp
// file (so the tab shows the member's name and the code can even be
// compiled). All permission rules are enforced by the server; the code
// here only mirrors them to set editors read-only or editable.
#include "../mainwindow.h"
#include "ui_mainwindow.h"

#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QMenuBar>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>

#include "../editor.h"
#include "../editormanager.h"
#include "../settings.h"
#include "qsynedit/document.h"
#include "roomclient.h"
#include "roomspanel.h"

namespace {

const char *ROOM_STARTER_CPP =
        "#include <iostream>\n"
        "using namespace std;\n"
        "\n"
        "int main() {\n"
        "    cout << \"Hello from Dev by Sufyan!\" << endl;\n"
        "    return 0;\n"
        "}\n";

QString languageFromFilename(const QString &filename)
{
    QString suffix = QFileInfo(filename).suffix().toLower();
    if (suffix == "py")
        return "python";
    if (suffix == "c" || suffix == "h")
        return "c";
    return "cpp";
}

QString extensionForLanguage(const QString &language)
{
    if (language == "python")
        return "py";
    if (language == "c")
        return "c";
    return "cpp";
}

QString sanitizeForFilename(const QString &name)
{
    QString result;
    for (QChar ch : name) {
        if (ch.isLetterOrNumber() || ch == '_' || ch == '-')
            result += ch;
        else
            result += '_';
    }
    if (result.isEmpty())
        result = "member";
    return result;
}

} // namespace

void MainWindow::setupRooms()
{
    mRoomClient = new RoomClient(this);
    mRoomsPanel = new RoomsPanel(this);
    mRoomsDock = new QDockWidget(tr("Rooms"), this);
    mRoomsDock->setObjectName("dockRooms");
    mRoomsDock->setWidget(mRoomsPanel);
    addDockWidget(Qt::LeftDockWidgetArea, mRoomsDock);
    tabifyDockWidget(ui->dockExplorer, mRoomsDock);

    QMenu *roomsMenu = new QMenu(tr("Rooms"), this);
    roomsMenu->addAction(mRoomsDock->toggleViewAction());
    roomsMenu->addSeparator();
    roomsMenu->addAction(tr("Leave Room"), this, &MainWindow::onRoomLeaveRequested);
    ui->menubar->insertMenu(ui->menuHelp->menuAction(), roomsMenu);

    connect(mRoomsPanel, &RoomsPanel::createRequested,
            this, &MainWindow::onRoomCreateRequested);
    connect(mRoomsPanel, &RoomsPanel::joinRequested,
            this, &MainWindow::onRoomJoinRequested);
    connect(mRoomsPanel, &RoomsPanel::leaveRequested,
            this, &MainWindow::onRoomLeaveRequested);
    connect(mRoomsPanel, &RoomsPanel::openMemberRequested,
            this, &MainWindow::onRoomOpenMemberRequested);
    connect(mRoomsPanel, &RoomsPanel::kickRequested,
            this, &MainWindow::onRoomKickRequested);
    connect(mRoomsPanel, &RoomsPanel::applySettingsRequested,
            this, &MainWindow::onRoomApplySettingsRequested);

    connect(mRoomClient, &RoomClient::roomJoined,
            this, &MainWindow::onRoomJoined);
    connect(mRoomClient, &RoomClient::memberJoined,
            this, &MainWindow::onRoomMemberJoined);
    connect(mRoomClient, &RoomClient::memberLeft,
            this, &MainWindow::onRoomMemberLeft);
    connect(mRoomClient, &RoomClient::docChanged,
            this, &MainWindow::onRoomDocChanged);
    connect(mRoomClient, &RoomClient::docDenied,
            this, &MainWindow::onRoomDocDenied);
    connect(mRoomClient, &RoomClient::settingsChanged,
            this, &MainWindow::onRoomSettingsChanged);
    connect(mRoomClient, &RoomClient::kicked,
            this, &MainWindow::onRoomKicked);
    connect(mRoomClient, &RoomClient::roomClosed,
            this, &MainWindow::onRoomClosedMessage);
    connect(mRoomClient, &RoomClient::errorReceived,
            this, &MainWindow::onRoomError);
    connect(mRoomClient, &RoomClient::disconnected,
            this, &MainWindow::onRoomDisconnected);
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

const RoomMemberInfo *MainWindow::roomMember(const QString &memberId) const
{
    for (const RoomMemberInfo &member : mRoomMembers)
        if (member.id == memberId)
            return &member;
    return nullptr;
}

Editor *MainWindow::roomEditorForTarget(const QString &targetId) const
{
    if (mRoomClient && targetId == mRoomClient->selfId())
        return mMyRoomEditor.data();
    auto it = mMemberEditors.constFind(targetId);
    if (it != mMemberEditors.constEnd())
        return it.value().data();
    return nullptr;
}

bool MainWindow::canEditRoomMember(const RoomMemberInfo &member) const
{
    if (!mRoomClient || !mRoomClient->isInRoom())
        return false;
    if (member.id == mRoomClient->selfId())
        return true;
    if (mRoomClient->isAdmin())
        return true;
    return mRoomSettings.membersCanEditEachOther && member.role != "admin";
}

void MainWindow::watchRoomEditor(Editor *editor)
{
    if (!editor || mRoomWatchedEditors.contains(editor))
        return;
    mRoomWatchedEditors.insert(editor);
    auto document = editor->document();
    connect(document.get(), &QSynedit::Document::changed, this,
            [this, editor]() {
        if (!mRoomClient || !mRoomClient->isInRoom())
            return;
        if (mRoomApplyingRemote.contains(editor))
            return;
        if (editor == mMyRoomEditor.data()) {
            scheduleRoomSync(mRoomClient->selfId());
        } else {
            for (auto it = mMemberEditors.constBegin();
                 it != mMemberEditors.constEnd(); ++it) {
                if (it.value().data() == editor) {
                    scheduleRoomSync(it.key());
                    break;
                }
            }
        }
    });
    connect(editor, &QObject::destroyed, this, [this, editor]() {
        mRoomWatchedEditors.remove(editor);
        mRoomApplyingRemote.remove(editor);
        if (mMyRoomEditor.data() == editor)
            mMyRoomEditor = nullptr;
        for (auto it = mMemberEditors.begin(); it != mMemberEditors.end();) {
            if (it.value().data() == editor)
                it = mMemberEditors.erase(it);
            else
                ++it;
        }
    });
}

void MainWindow::scheduleRoomSync(const QString &targetId)
{
    if (targetId.isEmpty())
        return;
    QTimer *timer = mRoomSyncTimers.value(targetId, nullptr);
    if (!timer) {
        timer = new QTimer(this);
        timer->setSingleShot(true);
        timer->setInterval(450);
        connect(timer, &QTimer::timeout, this,
                [this, targetId]() { sendRoomDocNow(targetId); });
        mRoomSyncTimers.insert(targetId, timer);
    }
    timer->start();
}

void MainWindow::sendRoomDocNow(const QString &targetId)
{
    Editor *editor = roomEditorForTarget(targetId);
    if (!editor || !mRoomClient || !mRoomClient->isInRoom())
        return;
    RoomDoc doc;
    doc.code = editor->text();
    if (targetId == mRoomClient->selfId()) {
        doc.language = mMyRoomLanguage;
    } else {
        const RoomMemberInfo *member = roomMember(targetId);
        doc.language = member ? member->doc.language : QString("cpp");
    }
    mRoomClient->sendDocUpdate(targetId, doc);
    if (targetId != mRoomClient->selfId())
        editor->setModified(false); // scratch view: content is synced
}

void MainWindow::applyRemoteRoomDoc(Editor *editor, const RoomDoc &doc,
                                    bool clearModified)
{
    if (!editor)
        return;
    mRoomApplyingRemote.insert(editor);
    QSynedit::CharPos caret = editor->caretXY();
    editor->setContent(doc.code);
    if (caret.line > editor->lineCount())
        caret.line = editor->lineCount();
    if (caret.line < 1)
        caret.line = 1;
    editor->setCaretXY(caret);
    if (clearModified)
        editor->setModified(false);
    mRoomApplyingRemote.remove(editor);
}

void MainWindow::applyRoomEditability()
{
    for (auto it = mMemberEditors.constBegin();
         it != mMemberEditors.constEnd(); ++it) {
        Editor *editor = it.value().data();
        const RoomMemberInfo *member = roomMember(it.key());
        if (editor && member)
            editor->setReadOnly(!canEditRoomMember(*member));
    }
}

void MainWindow::endRoom(const QString &message)
{
    if (mRoomClient)
        mRoomClient->leave();
    QList<Editor *> editorsToClose;
    for (auto it = mMemberEditors.constBegin();
         it != mMemberEditors.constEnd(); ++it)
        if (it.value())
            editorsToClose.append(it.value().data());
    mMemberEditors.clear();
    for (Editor *editor : editorsToClose)
        mEditorManager->forceCloseEditor(editor);
    for (QTimer *timer : mRoomSyncTimers) {
        timer->stop();
        delete timer;
    }
    mRoomSyncTimers.clear();
    mRoomMembers.clear();
    mMyRoomEditor = nullptr;
    if (!mRoomBackingDir.isEmpty()) {
        QDir{mRoomBackingDir}.removeRecursively();
        mRoomBackingDir.clear();
    }
    if (mRoomsPanel)
        mRoomsPanel->setNotInRoom(message);
    statusBar()->showMessage(message, 8000);
}

// ---------------------------------------------------------------------------
// panel intents
// ---------------------------------------------------------------------------

void MainWindow::onRoomCreateRequested(const QString &name)
{
    Editor *editor = mEditorManager->getEditor();
    mMyRoomEditor = editor;
    RoomDoc doc;
    if (editor) {
        doc.code = editor->text();
        doc.language = languageFromFilename(editor->filename());
        watchRoomEditor(editor);
    } else {
        doc.language = "cpp";
        doc.code = ROOM_STARTER_CPP;
    }
    mMyRoomLanguage = doc.language;
    mRoomsPanel->setConnecting(tr("Connecting to the room server..."));
    mRoomClient->createRoom(name, doc);
}

void MainWindow::onRoomJoinRequested(const QString &name, const QString &code)
{
    Editor *editor = mEditorManager->getEditor();
    mMyRoomEditor = editor;
    RoomDoc doc;
    if (editor) {
        doc.code = editor->text();
        doc.language = languageFromFilename(editor->filename());
        watchRoomEditor(editor);
    } else {
        doc.language = "cpp";
        doc.code = ROOM_STARTER_CPP;
    }
    mMyRoomLanguage = doc.language;
    mRoomsPanel->setConnecting(tr("Connecting to the room server..."));
    mRoomClient->joinRoom(name, code, doc);
}

void MainWindow::onRoomLeaveRequested()
{
    if (mRoomClient && mRoomClient->isInRoom())
        endRoom(tr("You left the room."));
    else if (mRoomClient)
        mRoomClient->leave();
}

void MainWindow::onRoomOpenMemberRequested(const QString &memberId)
{
    if (!mRoomClient || !mRoomClient->isInRoom())
        return;
    if (Editor *existing = roomEditorForTarget(memberId)) {
        mEditorManager->activeEditor(existing, true);
        return;
    }
    const RoomMemberInfo *member = roomMember(memberId);
    if (!member)
        return;
    if (mRoomBackingDir.isEmpty()) {
        mRoomBackingDir = QStandardPaths::writableLocation(
                    QStandardPaths::TempLocation)
                + "/DevBySufyanRooms/" + mRoomClient->roomCode();
    }
    QDir().mkpath(mRoomBackingDir);
    QString baseName = sanitizeForFilename(member->name);
    QString fileName = mRoomBackingDir + "/" + baseName + "."
            + extensionForLanguage(member->doc.language);
    int dedupe = 2;
    while (mEditorManager->getOpenedEditor(fileName)) {
        // another member with the same display name already uses this file
        fileName = mRoomBackingDir + "/" + baseName + "_"
                + QString::number(dedupe++) + "."
                + extensionForLanguage(member->doc.language);
    }
    QFile file{fileName};
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        statusBar()->showMessage(
                    tr("Could not create a temporary file for %1's code.")
                    .arg(member->name), 5000);
        return;
    }
    file.write(member->doc.code.toUtf8());
    file.close();
    Editor *editor = nullptr;
    try {
        editor = mEditorManager->newEditor(
                    fileName, pSettings->editor().defaultEncoding(),
                    FileType::None, QString(), false, false);
    } catch (...) {
        editor = nullptr;
    }
    if (!editor) {
        statusBar()->showMessage(
                    tr("Could not open %1's code.").arg(member->name), 5000);
        return;
    }
    mMemberEditors.insert(memberId, editor);
    watchRoomEditor(editor);
    editor->setReadOnly(!canEditRoomMember(*member));
    mEditorManager->activeEditor(editor, true);
}

void MainWindow::onRoomKickRequested(const QString &memberId)
{
    if (mRoomClient)
        mRoomClient->kick(memberId);
}

void MainWindow::onRoomApplySettingsRequested(const RoomSettings &settings)
{
    if (!mRoomClient || !mRoomClient->isInRoom())
        return;
    mRoomSettings = settings; // the server does not echo our own update
    mRoomClient->sendSettingsUpdate(settings);
    applyRoomEditability();
    statusBar()->showMessage(tr("Room settings updated."), 3000);
}

// ---------------------------------------------------------------------------
// client events
// ---------------------------------------------------------------------------

void MainWindow::onRoomJoined(const QString &selfId, const QString &role,
                              const QString &code,
                              const RoomSettings &settings,
                              const QVector<RoomMemberInfo> &members)
{
    Q_UNUSED(selfId);
    mRoomMembers = members;
    mRoomSettings = settings;
    mRoomsPanel->setInRoom(code, role, settings);
    mRoomsPanel->setMembers(members, mRoomClient->selfId());
    applyRoomEditability();
    statusBar()->showMessage(
                tr("Joined room %1. Share the code with your friends.")
                .arg(code), 6000);
}

void MainWindow::onRoomMemberJoined(const RoomMemberInfo &member)
{
    for (RoomMemberInfo &existing : mRoomMembers)
        if (existing.id == member.id) {
            existing = member;
            mRoomsPanel->setMembers(mRoomMembers, mRoomClient->selfId());
            return;
        }
    mRoomMembers.append(member);
    mRoomsPanel->setMembers(mRoomMembers, mRoomClient->selfId());
    statusBar()->showMessage(tr("%1 joined the room.").arg(member.name), 4000);
}

void MainWindow::onRoomMemberLeft(const QString &memberId, const QString &name)
{
    for (int i = 0; i < mRoomMembers.size(); ++i)
        if (mRoomMembers[i].id == memberId) {
            mRoomMembers.removeAt(i);
            break;
        }
    if (Editor *editor = roomEditorForTarget(memberId)) {
        mMemberEditors.remove(memberId);
        mEditorManager->forceCloseEditor(editor);
    }
    mRoomsPanel->setMembers(mRoomMembers, mRoomClient->selfId());
    if (!name.isEmpty())
        statusBar()->showMessage(tr("%1 left the room.").arg(name), 4000);
}

void MainWindow::onRoomDocChanged(const QString &targetId, const RoomDoc &doc,
                                  const QString &byName)
{
    Q_UNUSED(byName);
    for (RoomMemberInfo &member : mRoomMembers)
        if (member.id == targetId)
            member.doc = doc;
    Editor *editor = roomEditorForTarget(targetId);
    if (editor)
        applyRemoteRoomDoc(editor, doc,
                           targetId != mRoomClient->selfId());
}

void MainWindow::onRoomDocDenied(const QString &targetId, const RoomDoc &doc,
                                 const QString &message)
{
    Editor *editor = roomEditorForTarget(targetId);
    if (editor)
        applyRemoteRoomDoc(editor, doc,
                           targetId != mRoomClient->selfId());
    mRoomsPanel->showMessage(message);
    statusBar()->showMessage(message, 6000);
}

void MainWindow::onRoomSettingsChanged(const RoomSettings &settings)
{
    mRoomSettings = settings;
    mRoomsPanel->setSettings(settings);
    applyRoomEditability();
}

void MainWindow::onRoomKicked(const QString &message)
{
    endRoom(message.isEmpty()
            ? tr("The admin removed you from the room.") : message);
}

void MainWindow::onRoomClosedMessage(const QString &message)
{
    endRoom(message.isEmpty()
            ? tr("The admin left — the room is closed.") : message);
}

void MainWindow::onRoomError(const QString &message)
{
    if (mRoomClient && mRoomClient->isInRoom()) {
        mRoomsPanel->showMessage(message);
        statusBar()->showMessage(message, 6000);
    } else if (mRoomsPanel) {
        mRoomsPanel->setNotInRoom(message);
    }
}

void MainWindow::onRoomDisconnected()
{
    endRoom(tr("Disconnected from the room server — the room was closed."));
}
