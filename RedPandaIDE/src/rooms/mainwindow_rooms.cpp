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
#include <QCursor>
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
#include "dbstheme.h"
#include "dbsfx.h"

#include <QPainter>
#include <QPixmap>
#include <QTabBar>
#include <QVariantAnimation>

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
        showRoomsDockAnimated();
        mRoomsDock->raise();
        if (mRoomClient && mRoomClient->isInRoom())
            mRoomsPanel->selectTab(0);
        else
            mRoomsPanel->focusJoinField();
    });
    QAction *joinAction = roomsMenu->addAction(tr("Join Room"));
    connect(joinAction, &QAction::triggered, this, [this]() {
        showRoomsDockAnimated();
        mRoomsDock->raise();
        if (!(mRoomClient && mRoomClient->isInRoom()))
            mRoomsPanel->focusJoinField();
    });
    roomsMenu->addAction(tr("Leave Room"), this, &MainWindow::onRoomLeaveRequested);
    roomsMenu->addSeparator();
    // v1.4.0: admin runs everyone's code on one input and compares the
    // outputs; Side by Side puts a member's live code next to your own.
    QAction *runAllAction = roomsMenu->addAction(tr("Run All && Compare (Admin)"));
    connect(runAllAction, &QAction::triggered,
            this, &MainWindow::dbsRunAllCompare);
    mSideBySideAction = roomsMenu->addAction(tr("Side by Side"));
    mSideBySideAction->setCheckable(true);
    connect(mSideBySideAction, &QAction::triggered,
            this, &MainWindow::toggleSideBySide);
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
    // v1.4.0: live cursors + admin line pointing.
    connect(mRoomClient, &RoomClient::memberCursor,
            this, &MainWindow::onRoomMemberCursor);
    connect(mRoomClient, &RoomClient::linePointed,
            this, &MainWindow::onRoomLinePointed);
    // v1.5.0: live ping indicator in the Rooms panel header.
    connect(mRoomClient, &RoomClient::pingRttChanged,
            mRoomsPanel, &RoomsPanel::setPingRtt);
    mRoomCursorThrottle = new QTimer(this);
    mRoomCursorThrottle->setSingleShot(true);
    mRoomCursorThrottle->setInterval(200);
    connect(mRoomCursorThrottle, &QTimer::timeout, this, [this]() {
        if (!mRoomClient || !mRoomClient->isInRoom())
            return;
        if (mRoomCursorPendLine >= 0
                && (mRoomCursorPendLine != mRoomCursorLastLine
                    || mRoomCursorPendCol != mRoomCursorLastCol)) {
            mRoomCursorLastLine = mRoomCursorPendLine;
            mRoomCursorLastCol = mRoomCursorPendCol;
            mRoomClient->sendCursor(mRoomCursorPendLine, mRoomCursorPendCol);
            mRoomCursorThrottle->start();
        }
    });
    connect(mRoomClient, &RoomClient::kicked,
            this, &MainWindow::onRoomKicked);
    connect(mRoomClient, &RoomClient::roomClosed,
            this, &MainWindow::onRoomClosedMessage);
    connect(mRoomClient, &RoomClient::errorReceived,
            this, &MainWindow::onRoomError);
    connect(mRoomClient, &RoomClient::disconnected,
            this, &MainWindow::onRoomDisconnected);

    setupWelcomePage();
    // v1.6.0: theme packs, full-screen Home, backdrop, effects.
    setupDbsThemes();
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
        showRoomsDockAnimated();
        mRoomsDock->raise();
        mRoomsPanel->showMessage(
                    tr("Join a room first — Messages is the room chat."));
        return;
    }
    mChatUnread = 0;
    updateChatUnreadBadge();
    mRoomChatWindow->show();
    dbsFadeInWindow(mRoomChatWindow);  // Part 7 fade-in
    mRoomChatWindow->raise();
    mRoomChatWindow->activateWindow();
}

void MainWindow::selectRoomsTab(int index)
{
    showRoomsDockAnimated();
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
    mWelcomePage->setVersionText(tr("Version 1.6.0"));
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
    connect(mWelcomePage, &WelcomePage::templatesRequested,
            this, &MainWindow::onWelcomeTemplates);
    connect(mWelcomePage, &WelcomePage::openDocumentRequested, this, [this]() {
        ui->actionOpen->trigger();
    });
    connect(mWelcomePage, &WelcomePage::newProjectRequested, this, [this]() {
        ui->actionNew_Project->trigger();
    });
    connect(mWelcomePage, &WelcomePage::roomsRequested, this, [this]() {
        showRoomsDockAnimated();
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
// v1.5.1 — program templates for the Home page's "Program Templates"
// tile: five beginner classics, each in C++, C and Python. Short,
// commented, ready to run and edit. Picking one follows the same
// save-first flow as New Document, with the template as the content.
// ---------------------------------------------------------------------------

namespace {

struct DbsProgramTemplate {
    const char *name; // shown in the picker menu
    const char *slug; // default file name (without extension)
    const char *cpp;
    const char *c;
    const char *python;
};

const DbsProgramTemplate DBS_TEMPLATES[] = {
    { "Multiplication Table", "multiplication-table",
      "#include <iostream>\n"
      "using namespace std;\n"
      "\n"
      "int main() {\n"
      "    int n;\n"
      "    cout << \"Enter a number: \";\n"
      "    cin >> n;\n"
      "    for (int i = 1; i <= 10; i++) {\n"
      "        cout << n << \" x \" << i << \" = \" << n * i << endl;\n"
      "    }\n"
      "    return 0;\n"
      "}\n",
      "#include <stdio.h>\n"
      "\n"
      "int main() {\n"
      "    int n, i;\n"
      "    printf(\"Enter a number: \");\n"
      "    scanf(\"%d\", &n);\n"
      "    for (i = 1; i <= 10; i++) {\n"
      "        printf(\"%d x %d = %d\\n\", n, i, n * i);\n"
      "    }\n"
      "    return 0;\n"
      "}\n",
      "n = int(input(\"Enter a number: \"))\n"
      "for i in range(1, 11):\n"
      "    print(n, \"x\", i, \"=\", n * i)\n" },
    { "Factorial", "factorial",
      "#include <iostream>\n"
      "using namespace std;\n"
      "\n"
      "int main() {\n"
      "    int n;\n"
      "    long long fact = 1;\n"
      "    cout << \"Enter a number: \";\n"
      "    cin >> n;\n"
      "    for (int i = 1; i <= n; i++) {\n"
      "        fact = fact * i;\n"
      "    }\n"
      "    cout << \"Factorial of \" << n << \" = \" << fact << endl;\n"
      "    return 0;\n"
      "}\n",
      "#include <stdio.h>\n"
      "\n"
      "int main() {\n"
      "    int n, i;\n"
      "    long long fact = 1;\n"
      "    printf(\"Enter a number: \");\n"
      "    scanf(\"%d\", &n);\n"
      "    for (i = 1; i <= n; i++) {\n"
      "        fact = fact * i;\n"
      "    }\n"
      "    printf(\"Factorial of %d = %lld\\n\", n, fact);\n"
      "    return 0;\n"
      "}\n",
      "n = int(input(\"Enter a number: \"))\n"
      "fact = 1\n"
      "for i in range(1, n + 1):\n"
      "    fact = fact * i\n"
      "print(\"Factorial of\", n, \"=\", fact)\n" },
    { "Prime Check", "prime-check",
      "#include <iostream>\n"
      "using namespace std;\n"
      "\n"
      "int main() {\n"
      "    int n, i;\n"
      "    bool isPrime = true;\n"
      "    cout << \"Enter a number: \";\n"
      "    cin >> n;\n"
      "    if (n < 2) {\n"
      "        isPrime = false;\n"
      "    }\n"
      "    for (i = 2; i < n; i++) {\n"
      "        if (n % i == 0) {\n"
      "            isPrime = false;\n"
      "            break;\n"
      "        }\n"
      "    }\n"
      "    if (isPrime) {\n"
      "        cout << n << \" is a prime number.\" << endl;\n"
      "    } else {\n"
      "        cout << n << \" is not a prime number.\" << endl;\n"
      "    }\n"
      "    return 0;\n"
      "}\n",
      "#include <stdio.h>\n"
      "\n"
      "int main() {\n"
      "    int n, i;\n"
      "    int isPrime = 1;\n"
      "    printf(\"Enter a number: \");\n"
      "    scanf(\"%d\", &n);\n"
      "    if (n < 2) {\n"
      "        isPrime = 0;\n"
      "    }\n"
      "    for (i = 2; i < n; i++) {\n"
      "        if (n % i == 0) {\n"
      "            isPrime = 0;\n"
      "            break;\n"
      "        }\n"
      "    }\n"
      "    if (isPrime) {\n"
      "        printf(\"%d is a prime number.\\n\", n);\n"
      "    } else {\n"
      "        printf(\"%d is not a prime number.\\n\", n);\n"
      "    }\n"
      "    return 0;\n"
      "}\n",
      "n = int(input(\"Enter a number: \"))\n"
      "is_prime = True\n"
      "if n < 2:\n"
      "    is_prime = False\n"
      "for i in range(2, n):\n"
      "    if n % i == 0:\n"
      "        is_prime = False\n"
      "        break\n"
      "if is_prime:\n"
      "    print(n, \"is a prime number.\")\n"
      "else:\n"
      "    print(n, \"is not a prime number.\")\n" },
    { "Simple Calculator", "simple-calculator",
      "#include <iostream>\n"
      "using namespace std;\n"
      "\n"
      "int main() {\n"
      "    double a, b;\n"
      "    char op;\n"
      "    cout << \"Enter first number: \";\n"
      "    cin >> a;\n"
      "    cout << \"Enter operator (+ - * /): \";\n"
      "    cin >> op;\n"
      "    cout << \"Enter second number: \";\n"
      "    cin >> b;\n"
      "\n"
      "    switch (op) {\n"
      "        case '+':\n"
      "            cout << \"Result = \" << a + b << endl;\n"
      "            break;\n"
      "        case '-':\n"
      "            cout << \"Result = \" << a - b << endl;\n"
      "            break;\n"
      "        case '*':\n"
      "            cout << \"Result = \" << a * b << endl;\n"
      "            break;\n"
      "        case '/':\n"
      "            if (b == 0) {\n"
      "                cout << \"Cannot divide by zero.\" << endl;\n"
      "            } else {\n"
      "                cout << \"Result = \" << a / b << endl;\n"
      "            }\n"
      "            break;\n"
      "        default:\n"
      "            cout << \"Unknown operator.\" << endl;\n"
      "    }\n"
      "    return 0;\n"
      "}\n",
      "#include <stdio.h>\n"
      "\n"
      "int main() {\n"
      "    double a, b;\n"
      "    char op;\n"
      "    printf(\"Enter first number: \");\n"
      "    scanf(\"%lf\", &a);\n"
      "    printf(\"Enter operator (+ - * /): \");\n"
      "    scanf(\" %c\", &op);\n"
      "    printf(\"Enter second number: \");\n"
      "    scanf(\"%lf\", &b);\n"
      "\n"
      "    switch (op) {\n"
      "        case '+':\n"
      "            printf(\"Result = %g\\n\", a + b);\n"
      "            break;\n"
      "        case '-':\n"
      "            printf(\"Result = %g\\n\", a - b);\n"
      "            break;\n"
      "        case '*':\n"
      "            printf(\"Result = %g\\n\", a * b);\n"
      "            break;\n"
      "        case '/':\n"
      "            if (b == 0) {\n"
      "                printf(\"Cannot divide by zero.\\n\");\n"
      "            } else {\n"
      "                printf(\"Result = %g\\n\", a / b);\n"
      "            }\n"
      "            break;\n"
      "        default:\n"
      "            printf(\"Unknown operator.\\n\");\n"
      "    }\n"
      "    return 0;\n"
      "}\n",
      "a = float(input(\"Enter first number: \"))\n"
      "op = input(\"Enter operator (+ - * /): \")\n"
      "b = float(input(\"Enter second number: \"))\n"
      "\n"
      "if op == \"+\":\n"
      "    print(\"Result =\", a + b)\n"
      "elif op == \"-\":\n"
      "    print(\"Result =\", a - b)\n"
      "elif op == \"*\":\n"
      "    print(\"Result =\", a * b)\n"
      "elif op == \"/\":\n"
      "    if b == 0:\n"
      "        print(\"Cannot divide by zero.\")\n"
      "    else:\n"
      "        print(\"Result =\", a / b)\n"
      "else:\n"
      "    print(\"Unknown operator.\")\n" },
    { "Sum and Average", "sum-and-average",
      "#include <iostream>\n"
      "using namespace std;\n"
      "\n"
      "int main() {\n"
      "    int n, i;\n"
      "    double num, sum = 0;\n"
      "    cout << \"How many numbers? \";\n"
      "    cin >> n;\n"
      "    for (i = 1; i <= n; i++) {\n"
      "        cout << \"Enter number \" << i << \": \";\n"
      "        cin >> num;\n"
      "        sum = sum + num;\n"
      "    }\n"
      "    cout << \"Sum = \" << sum << endl;\n"
      "    cout << \"Average = \" << sum / n << endl;\n"
      "    return 0;\n"
      "}\n",
      "#include <stdio.h>\n"
      "\n"
      "int main() {\n"
      "    int n, i;\n"
      "    double num, sum = 0;\n"
      "    printf(\"How many numbers? \");\n"
      "    scanf(\"%d\", &n);\n"
      "    for (i = 1; i <= n; i++) {\n"
      "        printf(\"Enter number %d: \", i);\n"
      "        scanf(\"%lf\", &num);\n"
      "        sum = sum + num;\n"
      "    }\n"
      "    printf(\"Sum = %g\\n\", sum);\n"
      "    printf(\"Average = %g\\n\", sum / n);\n"
      "    return 0;\n"
      "}\n",
      "n = int(input(\"How many numbers? \"))\n"
      "total = 0\n"
      "for i in range(1, n + 1):\n"
      "    num = float(input(\"Enter number \" + str(i) + \": \"))\n"
      "    total = total + num\n"
      "print(\"Sum =\", total)\n"
      "print(\"Average =\", total / n)\n" },
};

} // namespace

void MainWindow::onWelcomeTemplates()
{
    // A small picker menu in the app's own menu style — the Home
    // screen itself stays uncluttered: one glass tile, no template
    // names on the wallpaper.
    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { font-size: 10.5pt; }"
        "QMenu::item { padding: 2px 24px 2px 12px; }"
        "QMenu::item:selected { background: #2f6fb2; color: #ffffff; }");
    const int count = int(sizeof(DBS_TEMPLATES) / sizeof(DBS_TEMPLATES[0]));
    auto addLanguage = [&](const QString &title, int lang) {
        QMenu *sub = menu.addMenu(title);
        for (int i = 0; i < count; ++i) {
            QAction *act = sub->addAction(
                        QString::fromLatin1(DBS_TEMPLATES[i].name));
            act->setData(i * 10 + lang); // decode after exec()
        }
    };
    addLanguage(tr("C++"), 0);
    addLanguage(tr("C"), 1);
    addLanguage(tr("Python"), 2);
    QAction *chosen = menu.exec(QCursor::pos());
    if (!chosen)
        return;
    const int tmplIndex = chosen->data().toInt() / 10;
    const int lang = chosen->data().toInt() % 10;
    if (tmplIndex < 0 || tmplIndex >= count)
        return;
    const DbsProgramTemplate &tmpl = DBS_TEMPLATES[tmplIndex];
    const char *codeText = lang == 0 ? tmpl.cpp
                                     : (lang == 1 ? tmpl.c : tmpl.python);
    const QString ext = lang == 0 ? ".cpp" : (lang == 1 ? ".c" : ".py");
    const QString filter = lang == 0 ? tr("C++ files (*.cpp)")
                                     : (lang == 1 ? tr("C files (*.c)")
                                                  : tr("Python files (*.py)"));

    // His save-first flow, exactly like New Document.
    QString startDir = QStandardPaths::writableLocation(
                QStandardPaths::DocumentsLocation);
    QString path = QFileDialog::getSaveFileName(
                this, tr("New from template — choose where to save it"),
                startDir + "/" + QString::fromLatin1(tmpl.slug) + ext,
                filter);
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
        file.write(codeText);
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
    if (editor) {
        mEditorManager->activeEditor(editor, true);
        statusBar()->showMessage(
                    tr("Template '%1' is open — edit it, then run it "
                       "like any program.").arg(
                        QString::fromLatin1(tmpl.name)), 8000);
    } else {
        statusBar()->showMessage(
                    tr("The file was saved, but could not be opened: %1")
                    .arg(path), 6000);
    }
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
    // v1.4.0: my caret in my room editor is shared with the room
    // (throttled in maybeSendRoomCursor). Only my own editor sends —
    // every member's client speaks for its own caret.
    connect(editor, &QSynedit::QSynEdit::statusChanged, this,
            [this, editor](QSynedit::StatusChanges changes) {
        if (changes.testFlag(QSynedit::StatusChange::CaretX)
                || changes.testFlag(QSynedit::StatusChange::CaretY))
            maybeSendRoomCursor(editor);
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
    // v1.4.0: room presence state dies with the room.
    if (mMyRoomEditor) {
        mMyRoomEditor->setRoomCursorLine(-1, QColor(), QString());
        mMyRoomEditor->setPointedLine(-1);
    }
    mSideBySideMemberId.clear();
    if (mSideBySideAction)
        mSideBySideAction->setChecked(false);
    mRoomCursorLastLine = mRoomCursorLastCol = -1;
    mRoomCursorPendLine = mRoomCursorPendCol = -1;
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
    // v1.4.0: if the side-by-side member left, the pairing is over.
    if (mSideBySideMemberId == memberId) {
        mSideBySideMemberId.clear();
        if (mSideBySideAction)
            mSideBySideAction->setChecked(false);
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

// ---------------------------------------------------------------------------
// v1.4.0: live cursors, admin line pointing, side-by-side
// ---------------------------------------------------------------------------

void MainWindow::maybeSendRoomCursor(Editor *editor)
{
    if (!editor || editor != mMyRoomEditor.data())
        return;
    if (!mRoomClient || !mRoomClient->isInRoom())
        return;
    QSynedit::CharPos caret = editor->caretXY();
    mRoomCursorPendLine = caret.line;
    mRoomCursorPendCol = caret.ch;
    if (mRoomCursorThrottle && !mRoomCursorThrottle->isActive()) {
        if (mRoomCursorPendLine != mRoomCursorLastLine
                || mRoomCursorPendCol != mRoomCursorLastCol) {
            mRoomCursorLastLine = mRoomCursorPendLine;
            mRoomCursorLastCol = mRoomCursorPendCol;
            mRoomClient->sendCursor(mRoomCursorPendLine, mRoomCursorPendCol);
        }
        mRoomCursorThrottle->start();
    }
}

void MainWindow::onRoomMemberCursor(const QString &memberId, int line, int col)
{
    Q_UNUSED(col);
    if (!mRoomClient || memberId == mRoomClient->selfId())
        return;
    Editor *editor = roomEditorForTarget(memberId);
    if (!editor)
        return;
    const RoomMemberInfo *member = roomMember(memberId);
    editor->setRoomCursorLine(line, RoomsPanel::memberColor(memberId),
                              member ? member->name : QString());
}

void MainWindow::onRoomLinePointed(const QString &byId,
                                   const QString &targetId, int line)
{
    const RoomMemberInfo *by = roomMember(byId);
    const RoomMemberInfo *target = roomMember(targetId);
    QString byName = by ? by->name : tr("The admin");
    QString targetName = target ? target->name : tr("a member");
    if (Editor *editor = roomEditorForTarget(targetId)) {
        editor->setPointedLine(line);
        QPointer<Editor> guard(editor);
        QTimer::singleShot(10000, this, [guard]() {
            if (guard)
                guard->setPointedLine(-1);
        });
    }
    if (mRoomClient && targetId == mRoomClient->selfId()) {
        // It is MY code being pointed at: bring the line to me.
        if (Editor *editor = roomEditorForTarget(targetId)) {
            mEditorManager->activeEditor(editor, false);
            if (line >= 0 && line < editor->lineCount())
                editor->setCaretXYCentered({1, line});
        }
        statusBar()->showMessage(
                    tr("%1 pointed at line %2 in your code — look there.")
                    .arg(byName).arg(line + 1), 10000);
        if (mRoomsPanel)
            mRoomsPanel->showMessage(
                        tr("%1 pointed at line %2 in your code.")
                        .arg(byName).arg(line + 1));
    } else {
        statusBar()->showMessage(
                    tr("%1 pointed at line %2 in %3's code.")
                    .arg(byName).arg(line + 1).arg(targetName), 8000);
    }
}

void MainWindow::toggleSideBySide()
{
    auto uncheck = [this]() {
        if (mSideBySideAction)
            mSideBySideAction->setChecked(false);
    };
    if (!mRoomClient || !mRoomClient->isInRoom()) {
        uncheck();
        statusBar()->showMessage(tr("Join a room first — Side by Side "
                                    "shows a member's code next to yours."), 5000);
        return;
    }
    bool turningOn = mSideBySideAction && mSideBySideAction->isChecked();
    if (!turningOn) {
        // Restore: move the member editor back to the main pane.
        if (!mSideBySideMemberId.isEmpty()) {
            if (Editor *editor = roomEditorForTarget(mSideBySideMemberId)) {
                if (mEditorManager->findPageControlForEditor(editor)
                        == ui->EditorTabsRight)
                    mEditorManager->swapEditor(editor);
            }
            mSideBySideMemberId.clear();
        }
        return;
    }
    // Whose code goes on the side? The member selected in the panel,
    // else the first other member.
    QString targetId = mRoomsPanel ? mRoomsPanel->selectedMemberId() : QString();
    if (targetId.isEmpty() || targetId == mRoomClient->selfId()) {
        targetId.clear();
        for (const RoomMemberInfo &member : mRoomMembers)
            if (member.id != mRoomClient->selfId()) {
                targetId = member.id;
                break;
            }
    }
    if (targetId.isEmpty()) {
        uncheck();
        statusBar()->showMessage(tr("No other member in the room yet."), 5000);
        return;
    }
    // Note: mMyRoomEditor can be null — since v1.3.2 the common flow is
    // creating/joining a room straight from the Home tab, with no file
    // of your own bound as your room code. Side by Side still makes
    // sense then: the member's code goes to the side pane and the main
    // pane keeps whatever it shows (Home or your file).
    if (!roomEditorForTarget(targetId))
        onRoomOpenMemberRequested(targetId);
    Editor *memberEditor = roomEditorForTarget(targetId);
    if (!memberEditor) {
        uncheck();
        return;
    }
    QTabWidget *memberPane = mEditorManager->findPageControlForEditor(memberEditor);
    if (memberPane == ui->EditorTabsLeft)
        mEditorManager->swapEditor(memberEditor);
    mSideBySideMemberId = targetId;
    // Keep my own editor in front on my side when I have one; the
    // member's stays visible in the other pane.
    if (mMyRoomEditor)
        mEditorManager->activeEditor(mMyRoomEditor.data(), true);
    const RoomMemberInfo *member = roomMember(targetId);
    statusBar()->showMessage(
                tr("Side by Side: your code next to %1's live code.")
                .arg(member ? member->name : targetId), 5000);
}

void MainWindow::onRoomDisconnected()
{
    // RoomClient now emits this only when the session is really over
    // (no seat to resume, or reconnecting gave up within the grace
    // period) — transient drops are handled by the resume machinery.
    endRoom(tr("Disconnected from the room server — could not reconnect, "
               "so the room was closed."));
}

// ---------------------------------------------------------------------------
// v1.6.0: theme packs, full-screen Home, panel backdrop, special effects
// ---------------------------------------------------------------------------

void MainWindow::setupDbsThemes()
{
    // Themes menu (same compact styling as the Rooms menu).
    QMenu *themesMenu = new QMenu(tr("Themes"), this);
    themesMenu->setStyleSheet(
        "QMenu { font-size: 10.5pt; }"
        "QMenu::item { padding: 2px 24px 2px 12px; }"
        "QMenu::item:selected { background: #2f6fb2; color: #ffffff; }"
        "QMenu::separator { height: 3px; margin: 1px 8px; }"
        "QMenu::indicator { width: 14px; height: 14px;"
        " margin-left: 6px; }");
    auto addPackAction = [this, themesMenu](const QString &label,
                                            const QString &id,
                                            const QIcon &icon) {
        QAction *action = themesMenu->addAction(label);
        action->setCheckable(true);
        action->setData(id);
        if (!icon.isNull())
            action->setIcon(icon);
        connect(action, &QAction::triggered, this,
                [this, themesMenu, action, id]() {
            // Radio behaviour without an action group: pack actions
            // are the checkable ones carrying a data() id.
            for (QAction *other : themesMenu->actions())
                if (other != action && other->isCheckable()
                        && other->data().isValid())
                    other->setChecked(false);
            action->setChecked(true);
            // Part 7: theme switches fade through the veil. The
            // midpoint applies the LATEST requested pack, so a
            // second click during the fade is never overridden by
            // the first click's captured choice.
            if (mEffectsOverlay && dbsAnimationsEnabled()
                    && id != mAppliedPackId) {
                mVeilPackId = id;
                mVeilPackSet = true;
                mEffectsOverlay->fadeThrough([this]() {
                    if (mVeilPackSet) {
                        const QString pid = mVeilPackId;
                        mVeilPackSet = false;
                        if (pid != mAppliedPackId)
                            applyDbsThemePack(pid, true);
                    }
                });
            } else {
                applyDbsThemePack(id, true);
            }
        });
        return action;
    };
    addPackAction(tr("Classic (no pack)"), QString(), QIcon());
    for (const DbsThemePack &pack : dbsThemePacks())
        addPackAction(pack.name, pack.id,
                      pack.iconPath.isEmpty()
                          ? QIcon()
                          : QIcon(pack.iconPath));
    themesMenu->addSeparator();
    themesMenu->addAction(tr("Customize Home..."), this,
                          &MainWindow::openCustomizeHome);
    mEffectsAction = themesMenu->addAction(tr("Special Effects"));
    mEffectsAction->setCheckable(true);
    mEffectsAction->setChecked(dbsSpecialEffectsEnabled());
    connect(mEffectsAction, &QAction::toggled, this, [this](bool on) {
        dbsSetSpecialEffectsEnabled(on);
        refreshDbsEffects();
    });
    // v1.6.0 Part 7: the Animations toggle (veil transitions, tile
    // hover glow, window/menu fades). Default on, like effects.
    QAction *animationsAction =
        themesMenu->addAction(tr("Animations"));
    animationsAction->setCheckable(true);
    animationsAction->setChecked(dbsAnimationsEnabled());
    connect(animationsAction, &QAction::toggled, this,
            [](bool on) {
        dbsSetAnimationsEnabled(on);
        QApplication::setEffectEnabled(Qt::UI_AnimateMenu, on);
        QApplication::setEffectEnabled(Qt::UI_FadeMenu, on);
    });
    QApplication::setEffectEnabled(Qt::UI_AnimateMenu,
                                   dbsAnimationsEnabled());
    QApplication::setEffectEnabled(Qt::UI_FadeMenu,
                                   dbsAnimationsEnabled());
    themesMenu->addSeparator();
    themesMenu->addAction(tr("Colour Scheme && Font..."), this,
                          [this]() {
        changeOptions(SettingsDialog::tr("Appearance"),
                      SettingsDialog::tr("Environment"));
    });
    connect(themesMenu, &QMenu::aboutToShow, this,
            [this, themesMenu]() {
        for (QAction *action : themesMenu->actions())
            if (action->isCheckable() && action->data().isValid())
                action->setChecked(action->data().toString()
                                   == mAppliedPackId);
        if (mEffectsAction)
            mEffectsAction->setChecked(dbsSpecialEffectsEnabled());
    });
    ui->menubar->insertMenu(ui->menuHelp->menuAction(), themesMenu);

    connect(mWelcomePage, &WelcomePage::customizeHomeRequested,
            this, &MainWindow::openCustomizeHome);

    // Full-screen Home follows the editor tabs of both panes.
    connect(ui->EditorTabsLeft, &QTabWidget::currentChanged,
            this, [this]() { dbsRefreshHomeFullscreen(); });
    connect(ui->EditorTabsRight, &QTabWidget::currentChanged,
            this, [this]() { dbsRefreshHomeFullscreen(); });

    // The effects overlay floats above the whole main window.
    mEffectsOverlay = new DbsEffectsOverlay(this);
    mEffectsOverlay->setRoomsRectProvider([this]() -> QRect {
        if (!mRoomsDock || !mRoomsDock->isVisible())
            return QRect();
        return QRect(mRoomsDock->mapTo(this, QPoint(0, 0)),
                     mRoomsDock->size());
    });
    // Part 6 (corrected): the toolbar strip anchors the "toolbox"
    // technique streaks (Thunder Breathing and friends). The
    // toolbar list is captured once; the provider unions the
    // visible horizontal toolbars.
    const QList<QToolBar *> toolbars = findChildren<QToolBar *>();
    mEffectsOverlay->setToolbarRectProvider(
        [this, toolbars]() -> QRect {
        QRect r;
        for (QToolBar *tb : toolbars) {
            if (tb->isVisible() && tb->orientation() == Qt::Horizontal)
                r = r.united(QRect(tb->mapTo(this, QPoint(0, 0)),
                                   tb->size()));
        }
        return r;
    });
    mEffectsOverlay->setFxEnabled(dbsSpecialEffectsEnabled());

    // Starting pack: the saved one; otherwise the pack whose editor
    // scheme is already in use (e.g. chosen via Appearance earlier).
    QString packId = dbsActivePackId();
    if (packId.isEmpty() && pSettings) {
        if (const DbsThemePack *pack =
                dbsPackForScheme(pSettings->editor().colorScheme()))
            packId = pack->id;
    }
    applyDbsThemePack(packId, false);
    // The startup Home may already be current before the tab
    // connections above existed — evaluate the chrome once.
    dbsRefreshHomeFullscreen();
    // ...and once more shortly after startup: files from the
    // command line / session restore can land after the first
    // evaluation (and after a veil midpoint), so re-sync the
    // chrome with the settled tab state.
    QTimer::singleShot(900, this,
                       [this]() { dbsRefreshHomeFullscreen(); });
}

void MainWindow::applyDbsThemePack(const QString &packId,
                                   bool updateScheme)
{
    mAppliedPackId = packId;
    dbsSetActivePackId(packId);
    const DbsThemePack *pack = dbsThemePackById(packId);
    if (pack && updateScheme && pSettings) {
        pSettings->editor().setColorScheme(pack->schemeName);
        pSettings->editor().save();
        updateEditorColorSchemes();
    }
    if (mWelcomePage) {
        if (pack) {
            mWelcomePage->setThemeArtworks(
                pack->wallpapers, dbsPinnedHomeArt(pack->id));
        } else {
            const QString pin =
                dbsPinnedHomeArt(QStringLiteral("classic"));
            if (!pin.isEmpty())
                mWelcomePage->setThemeArtworks({pin}, pin);
            else
                mWelcomePage->clearThemeArtworks();
        }
    }
    updateDbsBackdrop();
    refreshDbsEffects();
}

void MainWindow::openCustomizeHome()
{
    const DbsThemePack *pack = dbsThemePackById(mAppliedPackId);
    if (dbsShowCustomizeHomeDialog(this, pack))
        applyDbsThemePack(mAppliedPackId, false);
}

void MainWindow::updateDbsBackdrop()
{
    const QList<QWidget *> targets = {
        ui->centralwidget, ui->dockExplorer, ui->dockMessages,
        mRoomsDock, mRoomsPanel, ui->EditorTabsLeft,
        ui->EditorTabsRight,
    };
    const DbsThemePack *pack = dbsThemePackById(mAppliedPackId);
    if (!pack) {
        for (QWidget *widget : targets) {
            if (widget && mBackdropSaved.contains(widget)) {
                widget->setPalette(mBackdropSaved.value(widget));
                mBackdropSaved.remove(widget);
            }
        }
        return;
    }
    QString artPath = dbsPinnedHomeArt(pack->id);
    if (artPath.isEmpty() || !pack->wallpapers.contains(artPath))
        artPath = pack->wallpapers.first();
    QPixmap art(artPath);
    if (art.isNull())
        return;
    // A dimmed, pre-scaled copy of the Home wallpaper, cached on
    // disk so the palette brush can paint it behind the panels at
    // no per-frame decoding cost. The editor itself keeps its solid
    // readable background — this only textures the window chrome
    // and panel frames.
    art = art.scaledToWidth(qMin(1400, art.width()),
                            Qt::SmoothTransformation);
    QPixmap dimmed(art.size());
    dimmed.fill(Qt::black);
    {
        QPainter painter(&dimmed);
        painter.setOpacity(0.34);
        painter.drawPixmap(0, 0, art);
        painter.end();
    }
    const QString cacheDir =
        QStringLiteral("%1/cache")
            .arg(QFileInfo(pSettings->filename()).absolutePath());
    QDir().mkpath(cacheDir);
    const QString filePath =
        cacheDir + QStringLiteral("/dbs-backdrop.png");
    if (!dimmed.save(filePath, "PNG"))
        return;
    QPixmap backdropPix(filePath);
    if (backdropPix.isNull())
        return;
    const QBrush brush(backdropPix);
    for (QWidget *widget : targets) {
        if (!widget)
            continue;
        if (!mBackdropSaved.contains(widget))
            mBackdropSaved.insert(widget, widget->palette());
        QPalette pal = widget->palette();
        pal.setBrush(QPalette::Window, brush);
        widget->setAutoFillBackground(true);
        widget->setPalette(pal);
    }
}

void MainWindow::refreshDbsEffects()
{
    if (!mEffectsOverlay)
        return;
    const DbsThemePack *pack = dbsThemePackById(mAppliedPackId);
    mEffectsOverlay->setFxKind(pack ? pack->fxKind : DbsFxKind::None);
    mEffectsOverlay->setFxEnabled(dbsSpecialEffectsEnabled());
}

void MainWindow::dbsRefreshHomeFullscreen()
{
    if (!mWelcomePage)
        return;
    QTabWidget *hostTabs = ui->EditorTabsLeft;
    if (ui->EditorTabsRight->indexOf(mWelcomePage) >= 0)
        hostTabs = ui->EditorTabsRight;
    QTabWidget *otherTabs = hostTabs == ui->EditorTabsLeft
                                ? ui->EditorTabsRight
                                : ui->EditorTabsLeft;
    bool otherHasEditor = false;
    if (otherTabs->isVisible()) {
        for (int i = 0; i < otherTabs->count(); i++)
            if (qobject_cast<Editor *>(otherTabs->widget(i)))
                otherHasEditor = true;
    }
    const bool wantFullscreen =
        hostTabs->currentWidget() == mWelcomePage && !otherHasEditor;
    if (wantFullscreen == mHomeFullscreen)
        return;
    // Part 7: the layout switch fades through the effects veil —
    // the old view darkens briefly, the chrome toggles under cover,
    // and the new view is revealed. No veil (or animations off)
    // applies the switch immediately.
    if (mEffectsOverlay && dbsAnimationsEnabled() && !mVeilRefresh) {
        mEffectsOverlay->fadeThrough([this]() {
            // Recompute at midpoint time: a file may have opened
            // (or closed) since this transition was scheduled —
            // applying the captured decision would strand the
            // chrome in the wrong state.
            mVeilRefresh = true;
            dbsRefreshHomeFullscreen();
            mVeilRefresh = false;
        });
    } else {
        dbsApplyHomeFullscreen(wantFullscreen);
    }
}

void MainWindow::dbsUpdateFaceAvoid()
{
    if (!mEffectsOverlay)
        return;
    if (!mHomeFullscreen || !mWelcomePage) {
        mEffectsOverlay->setAvoidRect({});
        return;
    }
    const QRectF fr = mWelcomePage->currentFaceRect();
    if (fr.isEmpty()) {
        mEffectsOverlay->setAvoidRect({});
        return;
    }
    const QPoint tl = mWelcomePage->mapTo(this, fr.topLeft().toPoint());
    const QPoint br =
        mWelcomePage->mapTo(this, fr.bottomRight().toPoint());
    mEffectsOverlay->setAvoidRect(QRect(tl, br).normalized());
}

void MainWindow::dbsApplyHomeFullscreen(bool fullscreen)
{
    if (fullscreen == mHomeFullscreen)
        return;
    const bool wantFullscreen = fullscreen;
    if (wantFullscreen) {
        // Home owns the whole window: every dock, toolbar, tab bar
        // and the status bar step aside (the menu bar stays).
        mHomeFullscreen = true;
        mChromeSaved.clear();
        for (QDockWidget *dock : findChildren<QDockWidget *>()) {
            mChromeSaved.insert(dock, dock->isVisible());
            dock->hide();
        }
        for (QToolBar *bar : findChildren<QToolBar *>()) {
            mChromeSaved.insert(bar, bar->isVisible());
            bar->hide();
        }
        for (QTabWidget *tabs :
             {ui->EditorTabsLeft, ui->EditorTabsRight}) {
            if (QTabBar *tabBar = tabs->findChild<QTabBar *>()) {
                mChromeSaved.insert(tabBar, tabBar->isVisible());
                tabBar->hide();
            }
        }
        mStatusBarWasVisible = statusBar()->isVisible();
        statusBar()->hide();
        // Face avoidance: the effects overlay now keeps every
        // streak out of the Home artwork's face zone. The art can
        // rotate while Home is up, so re-sync on a slow timer.
        if (!mFaceAvoidTimer) {
            mFaceAvoidTimer = new QTimer(this);
            mFaceAvoidTimer->setInterval(800);
            connect(mFaceAvoidTimer, &QTimer::timeout, this,
                    [this]() { dbsUpdateFaceAvoid(); });
        }
        dbsUpdateFaceAvoid();
        mFaceAvoidTimer->start();
    } else {
        // Back to the IDE: restore exactly what was visible before.
        mHomeFullscreen = false;
        for (auto it = mChromeSaved.begin(); it != mChromeSaved.end();
             ++it)
            if (it.key())
                it.key()->setVisible(it.value());
        mChromeSaved.clear();
        statusBar()->setVisible(mStatusBarWasVisible);
        if (mFaceAvoidTimer)
            mFaceAvoidTimer->stop();
        if (mEffectsOverlay)
            mEffectsOverlay->setAvoidRect({});
    }
}

void MainWindow::showRoomsDockAnimated()
{
    if (!mRoomsDock)
        return;
    const bool wasHidden = !mRoomsDock->isVisible();
    mRoomsDock->show();
    mRoomsDock->raise();
    if (!wasHidden || !dbsAnimationsEnabled())
        return;
    // Part 7: slide the dock in from zero width (200 ms, OutCubic).
    // resizeDocks drives the main-window layout; a handful of
    // layout passes on a small surface, cheap enough for Winlator.
    const int target = qMax(mRoomsDock->width(), 200);
    auto *anim = new QVariantAnimation(this);
    anim->setDuration(280);
    anim->setStartValue(0);
    anim->setEndValue(target);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(anim, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &value) {
        resizeDocks({mRoomsDock}, {value.toInt()}, Qt::Horizontal);
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}
