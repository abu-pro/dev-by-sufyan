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

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QMenuBar>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

#include "../editor.h"
#include "../editormanager.h"
#include "../settings.h"
#include "../visithistorymanager.h"
#include "../settingsdialog/settingsdialog.h"
#include "qsynedit/document.h"
#include "roomclient.h"
#include "roomchatwindow.h"
#include "roomspanel.h"
#include "welcomepage.h"

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

    // v1.3.0: the room chat is a standalone popup window, owned here.
    mRoomChatWindow = new RoomChatWindow(this);
    mRoomChatWindow->hide();
    connect(mRoomChatWindow, &RoomChatWindow::sendRequested,
            this, &MainWindow::onRoomChatSendRequested);
    connect(mRoomChatWindow, &RoomChatWindow::typing,
            this, &MainWindow::onRoomChatTyping);
    connect(mRoomChatWindow, &RoomChatWindow::becameVisible, this, [this]() {
        // Only clear the badge when the user can actually see the popup;
        // an auto-open behind an inactive main window keeps the count
        // until the app comes to the front.
        if (isActiveWindow()) {
            mChatUnread = 0;
            updateChatUnreadBadge();
        }
    });
    connect(qApp, &QApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive && mRoomChatWindow
                && mRoomChatWindow->isVisible() && mChatUnread > 0) {
            mChatUnread = 0;
            updateChatUnreadBadge();
        }
    });

    // Toolbar Messages button (with unread count in its text).
    mMessagesAction = new QAction(tr("Messages"), this);
    mMessagesAction->setToolTip(tr("Open the room chat window"));
    ui->toolbarMain->addAction(mMessagesAction);
    connect(mMessagesAction, &QAction::triggered,
            this, &MainWindow::showRoomChatWindow);

    // Rooms menu: chat + every panel tab reachable from the top menu,
    // then the room actions (v1.3.0 restructure).
    QMenu *roomsMenu = new QMenu(tr("Rooms"), this);
    // v1.3.2: compact rows — the app theme pads menu items very tall,
    // which made these entries look like widely spaced lines. This
    // stylesheet is scoped to the Rooms menu only; every other menu
    // keeps the theme's look.
    roomsMenu->setStyleSheet(
        "QMenu { font-size: 10.5pt; }"
        "QMenu::item { padding: 2px 24px 2px 12px; }"
        "QMenu::item:selected { background: #2f6fb2; color: #ffffff; }"
        "QMenu::separator { height: 3px; margin: 1px 8px; }"
        "QMenu::indicator { width: 14px; height: 14px;"
        " margin-left: 6px; }");
    // v1.3.1: the welcome/home page can be reopened at any time.
    roomsMenu->addAction(tr("Home"), this, &MainWindow::showWelcomePage);
    roomsMenu->addSeparator();
    roomsMenu->addAction(tr("Messages"), this, &MainWindow::showRoomChatWindow);
    roomsMenu->addSeparator();
    QAction *roomTabAction = roomsMenu->addAction(tr("Room"));
    connect(roomTabAction, &QAction::triggered, this,
            [this]() { selectRoomsTab(0); });
    QAction *membersTabAction = roomsMenu->addAction(tr("Members"));
    connect(membersTabAction, &QAction::triggered, this,
            [this]() { selectRoomsTab(1); });
    QAction *settingsTabAction = roomsMenu->addAction(tr("Project && Settings"));
    connect(settingsTabAction, &QAction::triggered, this,
            [this]() { selectRoomsTab(2); });
    roomsMenu->addSeparator();
    QAction *createAction = roomsMenu->addAction(tr("Create Room"));
    connect(createAction, &QAction::triggered, this, [this]() {
        mRoomsDock->show();
        mRoomsDock->raise();
        if (mRoomClient && mRoomClient->isInRoom())
            mRoomsPanel->selectTab(0);
        else
            mRoomsPanel->focusJoinField();
    });
    QAction *joinAction = roomsMenu->addAction(tr("Join Room"));
    connect(joinAction, &QAction::triggered, this, [this]() {
        mRoomsDock->show();
        mRoomsDock->raise();
        if (!(mRoomClient && mRoomClient->isInRoom()))
            mRoomsPanel->focusJoinField();
    });
    roomsMenu->addAction(tr("Leave Room"), this, &MainWindow::onRoomLeaveRequested);
    roomsMenu->addSeparator();
    roomsMenu->addAction(mRoomsDock->toggleViewAction());
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
    connect(mRoomsPanel, &RoomsPanel::restrictRequested,
            this, &MainWindow::onRoomRestrictRequested);
    connect(mRoomsPanel, &RoomsPanel::applySettingsRequested,
            this, &MainWindow::onRoomApplySettingsRequested);
    connect(mRoomsPanel, &RoomsPanel::messagesRequested,
            this, &MainWindow::showRoomChatWindow);
    connect(mRoomsPanel, &RoomsPanel::saveCodeRequested,
            this, &MainWindow::onRoomSaveCodeRequested);

    connect(mRoomClient, &RoomClient::roomJoined,
            this, &MainWindow::onRoomJoined);
    connect(mRoomClient, &RoomClient::memberJoined,
            this, &MainWindow::onRoomMemberJoined);
    connect(mRoomClient, &RoomClient::memberLeft,
            this, &MainWindow::onRoomMemberLeft);
    connect(mRoomClient, &RoomClient::memberAway,
            this, &MainWindow::onRoomMemberAway);
    connect(mRoomClient, &RoomClient::memberBack,
            this, &MainWindow::onRoomMemberBack);
    connect(mRoomClient, &RoomClient::reconnecting,
            this, &MainWindow::onRoomReconnecting);
    connect(mRoomClient, &RoomClient::resumed,
            this, &MainWindow::onRoomResumed);
    // v1.3.1: a manual rejoin restored the user's retained seat.
    connect(mRoomClient, &RoomClient::seatRestored, this, [this]() {
        if (mRoomsPanel && mRoomClient)
            mRoomsPanel->showMessage(
                tr("Welcome back! Your seat in room %1 was restored.")
                    .arg(mRoomClient->roomCode()));
    });
    connect(mRoomClient, &RoomClient::resumeFailed,
            this, &MainWindow::onRoomResumeFailed);
    connect(mRoomClient, &RoomClient::docChanged,
            this, &MainWindow::onRoomDocChanged);
    connect(mRoomClient, &RoomClient::docDenied,
            this, &MainWindow::onRoomDocDenied);
    connect(mRoomClient, &RoomClient::settingsChanged,
            this, &MainWindow::onRoomSettingsChanged);
    connect(mRoomClient, &RoomClient::chatReceived,
            this, &MainWindow::onRoomChatReceived);
    connect(mRoomClient, &RoomClient::memberTyping,
            this, &MainWindow::onRoomMemberTyping);
    connect(mRoomClient, &RoomClient::memberRestricted,
            this, &MainWindow::onRoomMemberRestricted);
    connect(mRoomClient, &RoomClient::kicked,
            this, &MainWindow::onRoomKicked);
    connect(mRoomClient, &RoomClient::roomClosed,
            this, &MainWindow::onRoomClosedMessage);
    connect(mRoomClient, &RoomClient::errorReceived,
            this, &MainWindow::onRoomError);
    connect(mRoomClient, &RoomClient::disconnected,
            this, &MainWindow::onRoomDisconnected);

    setupWelcomePage();
}

// ---------------------------------------------------------------------------
// v1.3.0: Messages popup, unread badge, welcome page
// ---------------------------------------------------------------------------

void MainWindow::updateChatUnreadBadge()
{
    if (mMessagesAction)
        mMessagesAction->setText(mChatUnread > 0
            ? tr("Messages (%1)").arg(mChatUnread)
            : tr("Messages"));
    if (mRoomsPanel)
        mRoomsPanel->setUnreadCount(mChatUnread);
}

void MainWindow::showRoomChatWindow()
{
    if (!mRoomClient || !mRoomClient->isInRoom()) {
        mRoomsDock->show();
        mRoomsDock->raise();
        mRoomsPanel->showMessage(
                    tr("Join a room first — Messages is the room chat."));
        return;
    }
    mChatUnread = 0;
    updateChatUnreadBadge();
    mRoomChatWindow->show();
    mRoomChatWindow->raise();
    mRoomChatWindow->activateWindow();
}

void MainWindow::selectRoomsTab(int index)
{
    mRoomsDock->show();
    mRoomsDock->raise();
    mRoomsPanel->selectTab(index);
}

void MainWindow::onRoomRestrictRequested(const QString &memberId,
                                         bool restricted)
{
    if (mRoomClient)
        mRoomClient->setMemberRestricted(memberId, restricted);
}

void MainWindow::onRoomMemberRestricted(const QString &targetId,
                                        bool restricted)
{
    QString name;
    for (RoomMemberInfo &member : mRoomMembers)
        if (member.id == targetId) {
            member.restricted = restricted;
            name = member.name;
        }
    if (mRoomsPanel)
        mRoomsPanel->setMembers(mRoomMembers, mRoomClient->selfId());
    applyRoomEditability();
    if (!name.isEmpty())
        statusBar()->showMessage(restricted
            ? tr("%1 can no longer edit other members' code.").arg(name)
            : tr("%1 can edit other members' code again.").arg(name), 5000);
}

void MainWindow::noteCommandLineFiles(bool present)
{
    mCommandLineFilesPresent = present;
}

void MainWindow::ensureHomeTab()
{
    if (!mWelcomePage)
        return;
    // The Home page is a permanent first tab of the editor area.
    if (ui->EditorTabsLeft->indexOf(mWelcomePage) < 0
            && ui->EditorTabsRight->indexOf(mWelcomePage) < 0)
        ui->EditorTabsLeft->insertTab(0, mWelcomePage, tr("Home"));
    // A permanent tab gets no close button (either side).
    QTabWidget *host = ui->EditorTabsLeft->indexOf(mWelcomePage) >= 0
            ? ui->EditorTabsLeft : ui->EditorTabsRight;
    const int idx = host->indexOf(mWelcomePage);
    host->tabBar()->setTabButton(idx, QTabBar::RightSide, nullptr);
    host->tabBar()->setTabButton(idx, QTabBar::LeftSide, nullptr);
}

void MainWindow::showWelcomePage()
{
    // Select the Home tab (Rooms > Home, startup, last editor closed).
    if (!mWelcomePage)
        return;
    ensureHomeTab();
    QTabWidget *host = ui->EditorTabsLeft->indexOf(mWelcomePage) >= 0
            ? ui->EditorTabsLeft : ui->EditorTabsRight;
    // If the split layout currently hides the pane that holds Home (all
    // editors live in the other pane), move Home into the visible pane
    // so it always lands somewhere the user can actually see.
    if (!host->isVisible()) {
        QTabWidget *other = host == ui->EditorTabsLeft
                ? ui->EditorTabsRight : ui->EditorTabsLeft;
        host->removeTab(host->indexOf(mWelcomePage));
        other->insertTab(0, mWelcomePage, tr("Home"));
        other->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
        other->tabBar()->setTabButton(0, QTabBar::LeftSide, nullptr);
        host = other;
    }
    refreshHomePage();
    host->setCurrentWidget(mWelcomePage);
    mWelcomePage->setFocus();
}

void MainWindow::refreshHomePage()
{
    if (!mWelcomePage || !mEditorManager)
        return;
    // Continue Coding only exists while editor tabs do.
    mWelcomePage->setContinueVisible(mEditorManager->pageCount() > 0);
    // Recent files: the same visit history as File > Recent Files,
    // most recent first, files that still exist, at most six.
    QStringList recent;
    if (mVisitHistoryManager) {
        for (const PVisitRecord &record : mVisitHistoryManager->files()) {
            if (recent.size() >= 6)
                break;
            if (record && QFileInfo::exists(record->filename))
                recent << record->filename;
        }
    }
    mWelcomePage->setRecentFiles(recent);
}

void MainWindow::finishStartupWithHome()
{
    if (!mEditorManager)
        return;
    // Startup must not leave a phantom document behind the Home page:
    // drop every untouched blank editor (a restored session keeps its
    // real file tabs; only the empty untitled ones go).
    QList<Editor *> blanks;
    for (int i = 0; i < mEditorManager->pageCount(); i++) {
        Editor *editor = (*mEditorManager)[i];
        if (editor && editor->isNew() && !editor->modified()
                && editor->empty())
            blanks << editor;
    }
    for (Editor *editor : blanks)
        mEditorManager->forceCloseEditor(editor);
    showWelcomePage();
}

void MainWindow::onWelcomeRecentFile(const QString &filename)
{
    if (!filename.isEmpty())
        openFile(filename);
}

void MainWindow::setupWelcomePage()
{
    // v1.3.2: the welcome page is no longer an overlay — it becomes the
    // permanent "Home" tab of the editor area (see ensureHomeTab()).
    // The approved full-bleed anime design is unchanged.
    mWelcomePage = new WelcomePage(this);
    mWelcomePage->setVersionText(tr("Version 1.3.2"));
    auto shortcutOf = [](QAction *action) {
        return action ? action->shortcut().toString() : QString();
    };
    mWelcomePage->setHotkeysText(
                tr("Hotkeys — New: %1 · Open: %2 · Save: %3 · Compile: %4 · Run: %5")
                .arg(shortcutOf(ui->actionNew), shortcutOf(ui->actionOpen),
                     shortcutOf(ui->actionSave), shortcutOf(ui->actionCompile),
                     shortcutOf(ui->actionRun)));
    connect(mWelcomePage, &WelcomePage::continueRequested, this, [this]() {
        // Jump to the first open editor tab (left pane first).
        for (QTabWidget *tabs : {ui->EditorTabsLeft,
                                 ui->EditorTabsRight}) {
            for (int i = 0; i < tabs->count(); i++) {
                if (qobject_cast<Editor *>(tabs->widget(i))) {
                    tabs->setCurrentIndex(i);
                    tabs->widget(i)->setFocus();
                    return;
                }
            }
        }
    });
    connect(mWelcomePage, &WelcomePage::newDocumentRequested,
            this, &MainWindow::onWelcomeNewDocument);
    connect(mWelcomePage, &WelcomePage::openDocumentRequested, this, [this]() {
        ui->actionOpen->trigger();
    });
    connect(mWelcomePage, &WelcomePage::newProjectRequested, this, [this]() {
        ui->actionNew_Project->trigger();
    });
    connect(mWelcomePage, &WelcomePage::roomsRequested, this, [this]() {
        mRoomsDock->show();
        mRoomsDock->raise();
    });
    connect(mWelcomePage, &WelcomePage::themeRequested, this, [this]() {
        changeOptions(SettingsDialog::tr("Appearance"),
                      SettingsDialog::tr("Environment"));
    });
    connect(mWelcomePage, &WelcomePage::fontRequested, this, [this]() {
        changeOptions(SettingsDialog::tr("Font"),
                      SettingsDialog::tr("Editor"));
    });
    connect(mWelcomePage, &WelcomePage::recentFileRequested,
            this, &MainWindow::onWelcomeRecentFile);
    // Keep the Home content fresh: the Continue Coding tile and the
    // recent-files list follow editors opening and closing.
    connect(mEditorManager, &EditorManager::editorOpenned,
            this, [this]() { refreshHomePage(); });
    connect(mEditorManager, &EditorManager::editorClosed,
            this, [this]() { refreshHomePage(); });
    ensureHomeTab();
    refreshHomePage();
}

void MainWindow::onWelcomeNewDocument()
{
    // His flow: a new document is saved to disk FIRST, then opened.
    QString startDir = QStandardPaths::writableLocation(
                QStandardPaths::DocumentsLocation);
    QString path = QFileDialog::getSaveFileName(
                this, tr("New Document — choose where to save it"),
                startDir + "/main.cpp",
                tr("C++ files (*.cpp);;C files (*.c);;"
                   "Python files (*.py);;Text files (*.txt)"));
    if (path.isEmpty())
        return;
    QFileInfo info{path};
    if (!info.exists() || info.size() == 0) {
        QFile file{path};
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            statusBar()->showMessage(
                        tr("Could not create the file %1.").arg(path), 6000);
            return;
        }
        QString language = languageFromFilename(path);
        if (language == "python")
            file.write("# Dev by Sufyan\nprint(\"Hello from Dev by Sufyan!\")\n");
        else if (language == "c")
            file.write("#include <stdio.h>\n\nint main() {\n"
                       "    printf(\"Hello from Dev by Sufyan!\\n\");\n"
                       "    return 0;\n}\n");
        else if (info.suffix().compare("txt", Qt::CaseInsensitive) != 0)
            file.write(ROOM_STARTER_CPP);
        file.close();
    }
    Editor *editor = nullptr;
    try {
        editor = mEditorManager->newEditor(
                    path, pSettings->editor().defaultEncoding(),
                    FileType::None, QString(), false, false);
    } catch (...) {
        editor = nullptr;
    }
    if (editor)
        mEditorManager->activeEditor(editor, true);
    else
        statusBar()->showMessage(
                    tr("The file was saved, but could not be opened: %1")
                    .arg(path), 6000);
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
    // v1.3.0 mirror of the server rule: a member the admin restricted
    // may not edit anyone else's code, even with peer editing on.
    const RoomMemberInfo *self = roomMember(mRoomClient->selfId());
    if (self && self->restricted)
        return false;
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
    // v1.3.0: leaving (or the room closing) closes and clears the popup.
    if (mRoomChatWindow) {
        mRoomChatWindow->clearAll();
        mRoomChatWindow->hide();
    }
    mChatUnread = 0;
    updateChatUnreadBadge();
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
                              const QVector<RoomMemberInfo> &members,
                              const QVector<RoomChatMessage> &chatLog)
{
    Q_UNUSED(selfId);
    mRoomMembers = members;
    mRoomSettings = settings;
    mRoomsPanel->setInRoom(code, role, settings);
    mRoomsPanel->setMembers(members, mRoomClient->selfId());
    if (mRoomChatWindow) {
        mRoomChatWindow->setSelfId(mRoomClient->selfId());
        mRoomChatWindow->setChatLog(chatLog);
    }
    mChatUnread = 0;
    updateChatUnreadBadge();
    // After a resume, the fresh snapshot carries everyone's current
    // code: refresh any member editors that are already open. Our own
    // editor is deliberately NOT overwritten — it may hold edits typed
    // while offline, which onRoomResumed() pushes to the server.
    for (const RoomMemberInfo &member : members) {
        if (member.id == mRoomClient->selfId())
            continue;
        if (Editor *editor = roomEditorForTarget(member.id))
            applyRemoteRoomDoc(editor, member.doc, true);
    }
    applyRoomEditability();
    statusBar()->showMessage(
                tr("Joined room %1. Share the code with your friends.")
                .arg(code), 6000);
}

// ---------------------------------------------------------------------------
// Rooms v2: chat, typing, save code
// ---------------------------------------------------------------------------

void MainWindow::onRoomChatSendRequested(const QString &text)
{
    if (mRoomClient)
        mRoomClient->sendChat(text);
}

void MainWindow::onRoomChatTyping()
{
    if (mRoomClient)
        mRoomClient->sendTyping();
}

void MainWindow::onRoomChatReceived(const QString &fromId,
                                    const QString &fromName,
                                    const QString &text, qint64 ts)
{
    if (mRoomChatWindow)
        mRoomChatWindow->appendChatMessage(fromId, fromName, text, ts);
    // v1.3.0: a message from ANOTHER member opens the popup by itself.
    // Our own messages never trigger anything.
    if (!mRoomClient || fromId == mRoomClient->selfId() || !mRoomChatWindow)
        return;
    bool appActive = isActiveWindow();
    if (!mRoomChatWindow->isVisible()) {
        mRoomChatWindow->show();
        mRoomChatWindow->raise();
    }
    if (!appActive) {
        // The user is in another app: count it as unread and flash the
        // taskbar entry once.
        mChatUnread++;
        updateChatUnreadBadge();
        QApplication::alert(this);
    }
}

void MainWindow::onRoomMemberTyping(const QString &fromId,
                                    const QString &fromName)
{
    Q_UNUSED(fromId);
    if (mRoomChatWindow)
        mRoomChatWindow->showPeerTyping(fromName);
}

void MainWindow::onRoomSaveCodeRequested()
{
    if (!mRoomClient || !mRoomClient->isInRoom())
        return;
    // Whose code? The member whose editor is on screen first, then the
    // member selected in the panel list, then our own document.
    QString targetId;
    QString code;
    Editor *active = mEditorManager->getEditor();
    if (active) {
        for (auto it = mMemberEditors.constBegin();
             it != mMemberEditors.constEnd(); ++it) {
            if (it.value().data() == active) {
                targetId = it.key();
                code = active->text();
                break;
            }
        }
        if (targetId.isEmpty() && active == mMyRoomEditor.data()) {
            targetId = mRoomClient->selfId();
            code = active->text();
        }
    }
    if (targetId.isEmpty()) {
        QString selected = mRoomsPanel->selectedMemberId();
        if (!selected.isEmpty())
            targetId = selected;
    }
    if (targetId.isEmpty())
        targetId = mRoomClient->selfId();
    const RoomMemberInfo *member = roomMember(targetId);
    if (!member)
        return;
    if (code.isEmpty()) {
        if (Editor *editor = roomEditorForTarget(targetId))
            code = editor->text();
        else
            code = member->doc.code;
    }
    QString language = member->doc.language;
    QString defaultName = sanitizeForFilename(member->name) + "."
            + extensionForLanguage(language);
    QString filter = language == "python"
            ? tr("Python files (*.py);;All files (*)")
            : language == "c"
                  ? tr("C files (*.c);;All files (*)")
                  : tr("C++ files (*.cpp);;All files (*)");
    QString path = QFileDialog::getSaveFileName(
                this, tr("Save %1's code").arg(member->name),
                defaultName, filter);
    if (path.isEmpty())
        return;
    QFile file{path};
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        statusBar()->showMessage(
                    tr("Could not save the code to %1.").arg(path), 6000);
        return;
    }
    file.write(code.toUtf8());
    file.close();
    statusBar()->showMessage(
                tr("Saved %1's code to %2.")
                    .arg(member->name, QFileInfo(path).fileName()), 5000);
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

// ---------------------------------------------------------------------------
// v1.2.0: connection stability — away/back, reconnecting, resume results
// ---------------------------------------------------------------------------

void MainWindow::onRoomMemberAway(const QString &memberId, const QString &name)
{
    for (RoomMemberInfo &member : mRoomMembers)
        if (member.id == memberId)
            member.away = true;
    if (mRoomsPanel)
        mRoomsPanel->setMembers(mRoomMembers, mRoomClient->selfId());
    if (!name.isEmpty())
        statusBar()->showMessage(
                    tr("%1 lost connection — their seat is held for a while.")
                    .arg(name), 5000);
}

void MainWindow::onRoomMemberBack(const QString &memberId, const QString &name)
{
    for (RoomMemberInfo &member : mRoomMembers)
        if (member.id == memberId)
            member.away = false;
    if (mRoomsPanel)
        mRoomsPanel->setMembers(mRoomMembers, mRoomClient->selfId());
    if (!name.isEmpty())
        statusBar()->showMessage(tr("%1 is back.").arg(name), 4000);
}

void MainWindow::onRoomReconnecting()
{
    if (mRoomsPanel)
        mRoomsPanel->setReconnecting(true);
    if (mRoomChatWindow)
        mRoomChatWindow->setComposerEnabled(false);
    statusBar()->showMessage(
                tr("Connection lost — trying to reconnect…"), 8000);
}

void MainWindow::onRoomResumed()
{
    if (mRoomsPanel)
        mRoomsPanel->setReconnecting(false);
    if (mRoomChatWindow)
        mRoomChatWindow->setComposerEnabled(true);
    statusBar()->showMessage(
                tr("Reconnected — you are back in the room."), 5000);
    // Edits made while offline never reached the server: push our
    // current document so everyone is in sync again.
    if (mMyRoomEditor)
        scheduleRoomSync(mRoomClient->selfId());
}

void MainWindow::onRoomResumeFailed(const QString &message)
{
    endRoom(message.isEmpty()
            ? tr("Your seat in the room has expired. Please join again.")
            : message);
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
    // RoomClient now emits this only when the session is really over
    // (no seat to resume, or reconnecting gave up within the grace
    // period) — transient drops are handled by the resume machinery.
    endRoom(tr("Disconnected from the room server — could not reconnect, "
               "so the room was closed."));
}
