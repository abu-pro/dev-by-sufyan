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
#include "roomspanel.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDateTime>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {

// Fixed 8-color palette; a member's color is a stable hash of their id,
// so the same person keeps the same color on every client.
const char *MEMBER_COLORS[] = {
    "#e02323", "#1e88e5", "#43a047", "#fb8c00",
    "#8e24aa", "#00acc1", "#6d4c41", "#3949ab",
};

QIcon dotIcon(const QColor &color)
{
    QPixmap pixmap{14, 14};
    pixmap.fill(Qt::transparent);
    QPainter painter{&pixmap};
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(color);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(2, 2, 10, 10);
    return QIcon{pixmap};
}

} // namespace

RoomsPanel::RoomsPanel(QWidget *parent) :
    QWidget{parent}
{
    auto *layout = new QVBoxLayout{this};
    // v1.3.2: tightened from 8/8 — the panel reads as neat compact
    // lines instead of widely spaced rows.
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(5);

    // ---- Header: status + reconnect banner + Messages ------------------
    mStatusLabel = new QLabel{tr("Not in a room"), this};
    mStatusLabel->setWordWrap(true);
    layout->addWidget(mStatusLabel);

    mConnLabel = new QLabel{tr("Connection lost — reconnecting…"), this};
    mConnLabel->setWordWrap(true);
    mConnLabel->setStyleSheet(
        "background:#fff4e5; border:1px solid #f0c36d; border-radius:4px;"
        " color:#7a4d00; padding:4px 6px; font-weight:bold;");
    mConnLabel->setVisible(false);
    layout->addWidget(mConnLabel);

    // v1.5.0: live ping indicator — a coloured dot + the round-trip
    // time to the room server, updated by setPingRtt() while in a
    // room (green = good, yellow = slow, red = very slow / no reply).
    mPingLabel = new QLabel{this};
    mPingLabel->setToolTip(
        tr("Ping: how long a small message takes to reach the room "
           "server and come back. Green is good, yellow is slow, "
           "red means very slow or no reply."));
    mPingLabel->setVisible(false);
    layout->addWidget(mPingLabel);

    mMessagesButton = new QPushButton{tr("Messages"), this};
    mMessagesButton->setToolTip(tr("Open the room chat window"));
    mMessagesButton->setVisible(false);
    layout->addWidget(mMessagesButton);

    // ---- Create / join (visible while not in a room) --------------------
    mJoinBox = new QWidget{this};
    auto *joinLayout = new QVBoxLayout{mJoinBox};
    joinLayout->setContentsMargins(0, 0, 0, 0);
    joinLayout->setSpacing(4);
    mNameEdit = new QLineEdit{mJoinBox};
    mNameEdit->setPlaceholderText(tr("Your name"));
    mNameEdit->setMaxLength(24);
    joinLayout->addWidget(mNameEdit);
    mCreateButton = new QPushButton{tr("Create Room"), mJoinBox};
    joinLayout->addWidget(mCreateButton);
    auto *joinRow = new QHBoxLayout;
    mCodeEdit = new QLineEdit{mJoinBox};
    mCodeEdit->setPlaceholderText(tr("4-digit code"));
    mCodeEdit->setMaxLength(4);
    mCodeEdit->setInputMethodHints(Qt::ImhDigitsOnly);
    joinRow->addWidget(mCodeEdit);
    mJoinButton = new QPushButton{tr("Join Room"), mJoinBox};
    joinRow->addWidget(mJoinButton);
    joinLayout->addLayout(joinRow);
    layout->addWidget(mJoinBox);
    // v1.5.1: the join controls must hug their natural height — the
    // v1.5.0 panel let this box soak up the panel's empty space.
    mJoinBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    // v1.6.1: keep the form COMPACT on wide docks too. The Rooms
    // dock can be restored very wide (half the window), and the
    // name/code fields then stretched into giant empty boxes
    // ("room box kitna lamba hoya para ha"). Cap the form's width
    // and pin every control to its natural height.
    mJoinBox->setMaximumWidth(440);
    for (QWidget *control :
         {static_cast<QWidget *>(mNameEdit),
          static_cast<QWidget *>(mCreateButton),
          static_cast<QWidget *>(mCodeEdit),
          static_cast<QWidget *>(mJoinButton)})
        control->setSizePolicy(control->sizePolicy().horizontalPolicy(),
                               QSizePolicy::Fixed);

    // ---- Tabs (visible while in a room) ---------------------------------
    mTabs = new QTabWidget{this};
    mTabs->setVisible(false);
    // Stretch 0 at birth: the panel starts NOT in a room; setInRoom()
    // raises this to 1 and setNotInRoom() drops it back to 0.
    layout->addWidget(mTabs, 0);
    // v1.5.1: a trailing spring UNDER everything. While not in a room
    // the hidden tab widget's stretch used to leave a giant empty void
    // in the panel (flagged from the v1.5.0 screenshot); now this
    // spring absorbs the leftover space below the compact join
    // controls, and setInRoom() hands the stretch to the tabs instead.
    layout->addStretch();

    // Room tab
    auto *roomTab = new QWidget{mTabs};
    auto *roomLayout = new QVBoxLayout{roomTab};
    roomLayout->setSpacing(5);
    auto *codeRow = new QHBoxLayout;
    codeRow->addWidget(new QLabel{tr("Room code:"), roomTab});
    mCodeLabel = new QLabel{roomTab};
    mCodeLabel->setStyleSheet(
        "font-size:18pt; font-weight:bold; letter-spacing:2px;");
    codeRow->addWidget(mCodeLabel);
    mCopyCodeButton = new QPushButton{tr("Copy"), roomTab};
    mCopyCodeButton->setToolTip(tr("Copy the room code"));
    codeRow->addWidget(mCopyCodeButton);
    codeRow->addStretch();
    roomLayout->addLayout(codeRow);
    mProjectLabel = new QLabel{roomTab};
    mProjectLabel->setWordWrap(true);
    mProjectLabel->setStyleSheet(
        "background:#fdecea; border:1px solid #f0b8b2; border-radius:4px;"
        " padding:4px 6px; font-weight:bold; color:#8f1f14;");
    mProjectLabel->setVisible(false);
    roomLayout->addWidget(mProjectLabel);
    roomLayout->addStretch();
    mLeaveButton = new QPushButton{tr("Leave Room"), roomTab};
    roomLayout->addWidget(mLeaveButton);
    mTabs->addTab(roomTab, tr("Room"));

    // Members tab
    auto *membersTab = new QWidget{mTabs};
    auto *membersLayout = new QVBoxLayout{membersTab};
    membersLayout->setSpacing(4);
    mMembersList = new QListWidget{membersTab};
    mMembersList->setContextMenuPolicy(Qt::CustomContextMenu);
    // v1.3.2: compact member rows (neat single lines, less air).
    mMembersList->setStyleSheet(
        "QListWidget::item { padding: 1px 4px; }");
    membersLayout->addWidget(mMembersList);
    mSaveCodeButton = new QPushButton{tr("Save Code…"), membersTab};
    mSaveCodeButton->setToolTip(
        tr("Save the code you are viewing to a file on this computer"));
    membersLayout->addWidget(mSaveCodeButton);
    auto *memberActions = new QHBoxLayout;
    mRestrictButton = new QPushButton{tr("Restrict editing"), membersTab};
    mRestrictButton->setToolTip(tr("Bar the selected member from editing "
                                   "other members' code (admin only)"));
    memberActions->addWidget(mRestrictButton);
    mKickButton = new QPushButton{tr("Kick"), membersTab};
    memberActions->addWidget(mKickButton);
    membersLayout->addLayout(memberActions);
    // 2026-11-04: the admin can let one member edit the ADMIN's own
    // code (and take it back). Mirrors the web client's per-member
    // "Let edit my code" / "Revoke my-code edit" button.
    mGrantButton = new QPushButton{tr("Let edit my code"), membersTab};
    mGrantButton->setToolTip(tr("Give the selected member permission "
                                "to edit YOUR (the admin's) code"));
    membersLayout->addWidget(mGrantButton);
    // 2026-11-04: per-document edit history + copy/fork of the
    // selected member's code into my own document.
    auto *docActions = new QHBoxLayout;
    mHistoryButton = new QPushButton{tr("Edit history…"), membersTab};
    mHistoryButton->setToolTip(tr("Show who edited this document "
                                  "(the owner and the admin only)"));
    docActions->addWidget(mHistoryButton);
    mCopyToMyFileButton = new QPushButton{tr("Copy to my file"),
                                          membersTab};
    mCopyToMyFileButton->setToolTip(
        tr("Copy the selected member's code into your own document "
           "(their code stays untouched)"));
    docActions->addWidget(mCopyToMyFileButton);
    membersLayout->addLayout(docActions);
    mTabs->addTab(membersTab, tr("Members"));

    // Activity tab (2026-11-04): the room-activity feed — who edited
    // whose file and who got/took admin-code permission, newest first.
    auto *activityTab = new QWidget{mTabs};
    auto *activityLayout = new QVBoxLayout{activityTab};
    activityLayout->setSpacing(4);
    mEventsList = new QListWidget{activityTab};
    mEventsList->setStyleSheet(
        "QListWidget::item { padding: 1px 4px; }");
    mEventsList->setWordWrap(true);
    mEventsList->setSelectionMode(QAbstractItemView::NoSelection);
    activityLayout->addWidget(mEventsList);
    mActivityTabIndex = mTabs->addTab(activityTab, tr("Activity"));

    // Settings tab
    auto *settingsTab = new QWidget{mTabs};
    auto *settingsLayout = new QVBoxLayout{settingsTab};
    settingsLayout->setSpacing(4);
    mSettingsNote = new QLabel{
        tr("Only the admin can change these settings."), settingsTab};
    mSettingsNote->setWordWrap(true);
    mSettingsNote->setStyleSheet("color:#666; font-style:italic;");
    settingsLayout->addWidget(mSettingsNote);
    settingsLayout->addWidget(new QLabel{tr("Project name"), settingsTab});
    mProjectEdit = new QLineEdit{settingsTab};
    mProjectEdit->setPlaceholderText(tr("e.g. Rocket Simulator"));
    mProjectEdit->setMaxLength(60);
    settingsLayout->addWidget(mProjectEdit);
    auto *maxLayout = new QHBoxLayout;
    maxLayout->addWidget(new QLabel{tr("Max members"), settingsTab});
    mMaxMembersSpin = new QSpinBox{settingsTab};
    mMaxMembersSpin->setRange(2, 60);
    mMaxMembersSpin->setValue(8);
    maxLayout->addWidget(mMaxMembersSpin);
    settingsLayout->addLayout(maxLayout);
    mPeerEditCheck = new QCheckBox{
        tr("Members can edit each other's code"), settingsTab};
    settingsLayout->addWidget(mPeerEditCheck);
    mApplySettingsButton = new QPushButton{tr("Apply settings"), settingsTab};
    settingsLayout->addWidget(mApplySettingsButton);
    settingsLayout->addStretch();
    mTabs->addTab(settingsTab, tr("Settings"));

    connect(mCreateButton, &QPushButton::clicked,
            this, &RoomsPanel::onCreateClicked);
    connect(mJoinButton, &QPushButton::clicked,
            this, &RoomsPanel::onJoinClicked);
    connect(mCodeEdit, &QLineEdit::returnPressed,
            this, &RoomsPanel::onJoinClicked);
    connect(mNameEdit, &QLineEdit::returnPressed,
            this, &RoomsPanel::onCreateClicked);
    connect(mLeaveButton, &QPushButton::clicked,
            this, &RoomsPanel::leaveRequested);
    connect(mCopyCodeButton, &QPushButton::clicked,
            this, &RoomsPanel::onCopyCodeClicked);
    connect(mMessagesButton, &QPushButton::clicked,
            this, &RoomsPanel::messagesRequested);
    connect(mApplySettingsButton, &QPushButton::clicked,
            this, &RoomsPanel::onApplySettingsClicked);
    connect(mMembersList, &QListWidget::itemDoubleClicked,
            this, &RoomsPanel::onMemberDoubleClicked);
    connect(mMembersList, &QWidget::customContextMenuRequested,
            this, &RoomsPanel::onMemberContextMenu);
    connect(mMembersList, &QListWidget::itemSelectionChanged,
            this, &RoomsPanel::onMemberSelectionChanged);
    connect(mSaveCodeButton, &QPushButton::clicked,
            this, &RoomsPanel::saveCodeRequested);
    connect(mRestrictButton, &QPushButton::clicked,
            this, &RoomsPanel::onRestrictClicked);
    connect(mGrantButton, &QPushButton::clicked,
            this, &RoomsPanel::onGrantClicked);
    connect(mHistoryButton, &QPushButton::clicked,
            this, &RoomsPanel::onHistoryClicked);
    connect(mCopyToMyFileButton, &QPushButton::clicked,
            this, &RoomsPanel::onCopyToMyFileClicked);
    connect(mTabs, &QTabWidget::currentChanged,
            this, &RoomsPanel::onTabChanged);
    connect(mKickButton, &QPushButton::clicked, this, [this]() {
        QString memberId = selectedMemberId();
        if (!memberId.isEmpty() && memberId != mSelfId)
            emit kickRequested(memberId);
    });
}

QString RoomsPanel::userName() const
{
    return mNameEdit->text().trimmed();
}

void RoomsPanel::setConnecting(const QString &message)
{
    mStatusLabel->setText(message);
    mJoinBox->setEnabled(false);
}

void RoomsPanel::setInRoom(const QString &code, const QString &role,
                           const RoomSettings &settings)
{
    mInRoom = true;
    mIsAdmin = (role == "admin");
    mRoomCode = code;
    mCodeLabel->setText(code);
    mStatusLabel->setText(mIsAdmin
        ? tr("You are the admin of this room.")
        : tr("You are a member of this room."));
    mJoinBox->setVisible(false);
    mTabs->setVisible(true);
    // v1.5.1: in a room, the tabs own the panel's stretch again.
    if (auto *box = qobject_cast<QVBoxLayout *>(layout()))
        box->setStretchFactor(mTabs, 1);
    mMessagesButton->setVisible(true);
    // v1.5.0: ping indicator starts in its grey "waiting" state; the
    // first measured ping replaces it within a few seconds.
    mPingLabel->setVisible(true);
    mPingLabel->setText(tr("Ping: ")
                        + QStringLiteral("<span style=\"color:#9e9e9e\">"
                                         "●</span> —"));
    setUnreadCount(0);
    setReconnecting(false);
    setSettings(settings);
    // 2026-11-04: a fresh room starts with an empty activity feed;
    // the snapshot's recent events follow via setRoomEvents().
    mEvents.clear();
    mActivityUnread = 0;
    refreshEventsList();
    updateActivityTabLabel();
    if (mHistoryDialog)
        mHistoryDialog->hide();
    mHistoryTargetId.clear();
    updateMemberActionButtons();
}

void RoomsPanel::setNotInRoom(const QString &message)
{
    mInRoom = false;
    mIsAdmin = false;
    mSelfId.clear();
    mRoomCode.clear();
    mMembers.clear();
    mMembersList->clear();
    // 2026-11-04: the activity feed and any open history die with
    // the room session, exactly like the chat log.
    mEvents.clear();
    mActivityUnread = 0;
    refreshEventsList();
    updateActivityTabLabel();
    if (mHistoryDialog)
        mHistoryDialog->hide();
    mHistoryTargetId.clear();
    mProjectLabel->setVisible(false);
    mConnLabel->setVisible(false);
    mPingLabel->setVisible(false);
    mMessagesButton->setVisible(false);
    setUnreadCount(0);
    mJoinBox->setVisible(true);
    mJoinBox->setEnabled(true);
    mTabs->setVisible(false);
    // v1.5.1: hand the panel's stretch to the trailing spring while
    // not in a room (see the constructor note).
    if (auto *box = qobject_cast<QVBoxLayout *>(layout()))
        box->setStretchFactor(mTabs, 0);
    mStatusLabel->setText(message.isEmpty() ? tr("Not in a room") : message);
}

void RoomsPanel::setReconnecting(bool reconnecting)
{
    mConnLabel->setVisible(reconnecting && mInRoom);
}

void RoomsPanel::setPingRtt(int rttMs)
{
    if (!mInRoom)
        return;
    QString color;
    QString value;
    if (rttMs < 0) {
        color = QStringLiteral("#e02323"); // red: no reply
        value = tr("no reply");
    } else if (rttMs < 150) {
        color = QStringLiteral("#43a047"); // green: good
        value = tr("%1 ms").arg(rttMs);
    } else if (rttMs < 400) {
        color = QStringLiteral("#f9a825"); // yellow: slow
        value = tr("%1 ms").arg(rttMs);
    } else {
        color = QStringLiteral("#e02323"); // red: very slow
        value = tr("%1 ms").arg(rttMs);
    }
    mPingLabel->setText(tr("Ping: ")
                        + QStringLiteral("<span style=\"color:%1\">"
                                         "●</span> ").arg(color)
                        + value);
}

void RoomsPanel::setUnreadCount(int count)
{
    mMessagesButton->setText(count > 0
        ? tr("Messages (%1)").arg(count)
        : tr("Messages"));
}

void RoomsPanel::selectTab(int index)
{
    if (index >= 0 && index < mTabs->count())
        mTabs->setCurrentIndex(index);
}

void RoomsPanel::focusJoinField()
{
    if (mNameEdit->text().trimmed().isEmpty())
        mNameEdit->setFocus();
    else
        mCodeEdit->setFocus();
}

QString RoomsPanel::languageDisplayName(const QString &language)
{
    if (language == "python")
        return QStringLiteral("Python");
    if (language == "c")
        return QStringLiteral("C");
    return QStringLiteral("C++");
}

void RoomsPanel::setMembers(const QVector<RoomMemberInfo> &members,
                            const QString &selfId)
{
    mSelfId = selfId;
    mMembers = members;
    QString previousSelection = selectedMemberId();
    mMembersList->clear();
    for (const RoomMemberInfo &member : members) {
        QStringList badges;
        if (member.role == "admin")
            badges << tr("Admin");
        if (member.id == selfId)
            badges << tr("You");
        if (member.away)
            badges << tr("Away");
        if (member.restricted)
            badges << tr("Restricted");
        if (member.canEditAdminDoc)
            badges << tr("CAN EDIT ADMIN");
        badges << languageDisplayName(member.doc.language);
        QString text = member.name + " · " + badges.join(" · ");
        auto *item = new QListWidgetItem{text, mMembersList};
        // v1.3.2: fixed compact row height — one neat line per member.
        item->setSizeHint(QSize{0, 23});
        item->setData(Qt::UserRole, member.id);
        item->setIcon(dotIcon(member.away ? QColor{"#b0b0b0"}
                                          : memberColor(member.id)));
        if (member.away)
            item->setForeground(QBrush{QColor{"#888888"}});
        QString tip = member.away
            ? tr("Connection lost — the seat is held for a while")
            : tr("Double-click to view the code");
        if (member.restricted)
            tip += "\n" + tr("Restricted by the admin: cannot edit other "
                             "members' code.");
        if (member.canEditAdminDoc)
            tip += "\n" + tr("The admin gave this member permission to "
                             "edit the admin's code.");
        item->setToolTip(tip);
        if (member.id == previousSelection)
            mMembersList->setCurrentItem(item);
    }
    updateMemberActionButtons();
}

QColor RoomsPanel::memberColor(const QString &memberId)
{
    uint hash = 0;
    for (QChar ch : memberId)
        hash = hash * 31 + ch.unicode();
    return QColor{QString::fromLatin1(
        MEMBER_COLORS[hash % (sizeof(MEMBER_COLORS) / sizeof(MEMBER_COLORS[0]))])};
}

QString RoomsPanel::selectedMemberId() const
{
    return memberIdOf(mMembersList->currentItem());
}

const RoomMemberInfo *RoomsPanel::memberInfo(const QString &memberId) const
{
    for (const RoomMemberInfo &member : mMembers)
        if (member.id == memberId)
            return &member;
    return nullptr;
}

void RoomsPanel::updateMemberActionButtons()
{
    const RoomMemberInfo *selected = memberInfo(selectedMemberId());
    bool canActOn = mInRoom && mIsAdmin && selected
            && selected->id != mSelfId && selected->role != "admin";
    mRestrictButton->setVisible(mIsAdmin);
    mKickButton->setVisible(mIsAdmin);
    mRestrictButton->setEnabled(canActOn);
    mKickButton->setEnabled(canActOn);
    if (selected && selected->restricted)
        mRestrictButton->setText(tr("Allow editing"));
    else
        mRestrictButton->setText(tr("Restrict editing"));
    // 2026-11-04: grant/revoke of admin-code edit permission — the
    // same admin-only targets as restrict/kick; the label mirrors
    // the member's current grant state (web client wording).
    mGrantButton->setVisible(mIsAdmin);
    mGrantButton->setEnabled(canActOn);
    if (selected && selected->canEditAdminDoc) {
        mGrantButton->setText(tr("Revoke my-code edit"));
        mGrantButton->setToolTip(tr("Take back this member's "
                                    "permission to edit YOUR (the "
                                    "admin's) code"));
    } else {
        mGrantButton->setText(tr("Let edit my code"));
        mGrantButton->setToolTip(tr("Give this member permission to "
                                    "edit YOUR (the admin's) code"));
    }
    // Edit history: readable for my own document, or for any
    // document when I am the admin (the server checks again).
    mHistoryButton->setEnabled(mInRoom && selected
                               && (selected->id == mSelfId || mIsAdmin));
    // Copy/fork only makes sense for someone else's document.
    mCopyToMyFileButton->setEnabled(mInRoom && selected
                                    && selected->id != mSelfId);
}

void RoomsPanel::onMemberSelectionChanged()
{
    updateMemberActionButtons();
}

void RoomsPanel::onRestrictClicked()
{
    const RoomMemberInfo *selected = memberInfo(selectedMemberId());
    if (!selected)
        return;
    emit restrictRequested(selected->id, !selected->restricted);
}

void RoomsPanel::onGrantClicked()
{
    const RoomMemberInfo *selected = memberInfo(selectedMemberId());
    if (!selected)
        return;
    emit grantEditRequested(selected->id, !selected->canEditAdminDoc);
}

void RoomsPanel::onHistoryClicked()
{
    const RoomMemberInfo *selected = memberInfo(selectedMemberId());
    if (!selected)
        return;
    openHistoryDialog(selected->id, selected->name);
    emit historyRequested(selected->id);
}

void RoomsPanel::onCopyToMyFileClicked()
{
    const RoomMemberInfo *selected = memberInfo(selectedMemberId());
    if (!selected || selected->id == mSelfId)
        return;
    emit copyToMyFileRequested(selected->id);
}

void RoomsPanel::onTabChanged(int index)
{
    // Reading the Activity tab clears its unread count (the web
    // client's badge behaviour, translated to the tab label).
    if (index == mActivityTabIndex && mActivityUnread > 0) {
        mActivityUnread = 0;
        updateActivityTabLabel();
    }
}

// ---------------------------------------------------------------------------
// 2026-11-04: room activity feed + edit history dialog
// ---------------------------------------------------------------------------

QString RoomsPanel::timeDisplayText(qint64 ts)
{
    if (ts <= 0)
        return QString();
    // Same shape as the web client: "6:50 PM".
    return QDateTime::fromMSecsSinceEpoch(ts).time()
            .toString(QStringLiteral("h:mm AP"));
}

QString RoomsPanel::eventDisplayText(const RoomEvent &event)
{
    // The web client's eventText(), verbatim.
    if (event.kind == "edit")
        return tr("%1 changed %2's file")
                .arg(event.byName, event.targetName);
    if (event.kind == "grant")
        return tr("%1 gave %2 permission to edit the admin's file")
                .arg(event.byName, event.targetName);
    if (event.kind == "revoke")
        return tr("%1 took back %2's permission to edit the admin's "
                  "file").arg(event.byName, event.targetName);
    return QString();
}

void RoomsPanel::refreshEventsList()
{
    if (!mEventsList)
        return;
    mEventsList->clear();
    if (mEvents.isEmpty()) {
        auto *item = new QListWidgetItem{
            tr("No activity yet — edits to other members' files and "
               "permission changes appear here."), mEventsList};
        item->setFlags(Qt::NoItemFlags);
        item->setForeground(QBrush{QColor{"#888888"}});
        return;
    }
    for (const RoomEvent &event : mEvents) {
        QString text = eventDisplayText(event);
        if (text.isEmpty())
            continue;
        QString when = timeDisplayText(event.ts);
        if (!when.isEmpty())
            text += QStringLiteral(" · ") + when;
        new QListWidgetItem{text, mEventsList};
    }
}

void RoomsPanel::updateActivityTabLabel()
{
    if (mActivityTabIndex < 0)
        return;
    mTabs->setTabText(mActivityTabIndex, mActivityUnread > 0
                          ? tr("Activity (%1)").arg(mActivityUnread)
                          : tr("Activity"));
}

void RoomsPanel::setRoomEvents(const QVector<RoomEvent> &events)
{
    // The snapshot sends oldest first; the feed shows newest first.
    mEvents.clear();
    for (int i = events.size() - 1; i >= 0; --i)
        mEvents.append(events.at(i));
    if (mEvents.size() > 50)
        mEvents.resize(50);
    refreshEventsList();
}

void RoomsPanel::addRoomEvent(const RoomEvent &event)
{
    mEvents.prepend(event);
    if (mEvents.size() > 50)
        mEvents.resize(50);
    refreshEventsList();
    if (mTabs->currentIndex() != mActivityTabIndex) {
        ++mActivityUnread;
        updateActivityTabLabel();
    }
}

void RoomsPanel::openHistoryDialog(const QString &memberId,
                                   const QString &memberName)
{
    if (!mHistoryDialog) {
        mHistoryDialog = new QDialog{this};
        mHistoryDialog->setWindowTitle(tr("Edit history"));
        mHistoryDialog->resize(380, 320);
        auto *dialogLayout = new QVBoxLayout{mHistoryDialog};
        mHistoryTitle = new QLabel{mHistoryDialog};
        mHistoryTitle->setStyleSheet("font-weight:bold;");
        dialogLayout->addWidget(mHistoryTitle);
        mHistoryList = new QListWidget{mHistoryDialog};
        mHistoryList->setStyleSheet(
            "QListWidget::item { padding: 1px 4px; }");
        mHistoryList->setSelectionMode(QAbstractItemView::NoSelection);
        dialogLayout->addWidget(mHistoryList);
        auto *closeButton = new QPushButton{tr("Close"), mHistoryDialog};
        dialogLayout->addWidget(closeButton);
        connect(closeButton, &QPushButton::clicked,
                mHistoryDialog, &QDialog::hide);
    }
    mHistoryTargetId = memberId;
    mHistoryDialog->setWindowTitle(tr("Edit history"));
    mHistoryTitle->setText(tr("Edit history — %1").arg(memberName));
    mHistoryList->clear();
    auto *loading = new QListWidgetItem{tr("Loading…"), mHistoryList};
    loading->setFlags(Qt::NoItemFlags);
    loading->setForeground(QBrush{QColor{"#888888"}});
    mHistoryDialog->show();
    mHistoryDialog->raise();
    mHistoryDialog->activateWindow();
}

void RoomsPanel::setDocHistory(const QString &targetId,
                               const QVector<RoomHistoryEntry> &entries,
                               bool denied)
{
    // Answers and pushes for other documents don't touch an open
    // dialog that is showing someone else's history.
    if (!mHistoryDialog || !mHistoryDialog->isVisible()
            || targetId != mHistoryTargetId)
        return;
    mHistoryList->clear();
    if (denied) {
        auto *item = new QListWidgetItem{
            tr("Only the document owner and the admin can see this "
               "history."), mHistoryList};
        item->setFlags(Qt::NoItemFlags);
        return;
    }
    if (entries.isEmpty()) {
        auto *item = new QListWidgetItem{
            tr("No edits recorded yet in this room session."),
            mHistoryList};
        item->setFlags(Qt::NoItemFlags);
        item->setForeground(QBrush{QColor{"#888888"}});
        return;
    }
    for (const RoomHistoryEntry &entry : entries) {
        QString text = tr("%1 edited").arg(entry.byName);
        QString when = timeDisplayText(entry.ts);
        if (!when.isEmpty())
            text += QStringLiteral(" · ") + when;
        new QListWidgetItem{text, mHistoryList};
    }
}

void RoomsPanel::setSettings(const RoomSettings &settings)
{
    mMaxMembersSpin->setValue(settings.maxMembers);
    mPeerEditCheck->setChecked(settings.membersCanEditEachOther);
    if (mProjectEdit->text() != settings.projectName)
        mProjectEdit->setText(settings.projectName);
    // Admins edit; members see the same values read-only.
    mSettingsNote->setVisible(!mIsAdmin);
    mProjectEdit->setReadOnly(!mIsAdmin);
    mMaxMembersSpin->setEnabled(mIsAdmin);
    mPeerEditCheck->setEnabled(mIsAdmin);
    mApplySettingsButton->setVisible(mIsAdmin);
    if (settings.projectName.isEmpty()) {
        mProjectLabel->setVisible(false);
    } else {
        mProjectLabel->setText(tr("Project: %1").arg(settings.projectName));
        mProjectLabel->setVisible(mInRoom);
    }
}

void RoomsPanel::showMessage(const QString &message)
{
    mStatusLabel->setText(message);
}

void RoomsPanel::onCreateClicked()
{
    if (userName().isEmpty()) {
        showMessage(tr("Please enter your name first."));
        mNameEdit->setFocus();
        return;
    }
    emit createRequested(userName());
}

void RoomsPanel::onJoinClicked()
{
    if (userName().isEmpty()) {
        showMessage(tr("Please enter your name first."));
        mNameEdit->setFocus();
        return;
    }
    QString code = mCodeEdit->text().trimmed();
    if (code.length() != 4) {
        showMessage(tr("Please enter the 4-digit room code."));
        mCodeEdit->setFocus();
        return;
    }
    emit joinRequested(userName(), code);
}

QString RoomsPanel::memberIdOf(QListWidgetItem *item) const
{
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void RoomsPanel::onMemberDoubleClicked(QListWidgetItem *item)
{
    QString memberId = memberIdOf(item);
    if (!memberId.isEmpty() && memberId != mSelfId)
        emit openMemberRequested(memberId);
}

void RoomsPanel::onMemberContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = mMembersList->itemAt(pos);
    QString memberId = memberIdOf(item);
    if (memberId.isEmpty() || memberId == mSelfId)
        return;
    const RoomMemberInfo *info = memberInfo(memberId);
    QMenu menu{this};
    // v1.3.2: same compact rows as the Rooms menu.
    menu.setStyleSheet(
        "QMenu { font-size: 10.5pt; }"
        "QMenu::item { padding: 2px 24px 2px 12px; }"
        "QMenu::item:selected { background: #2f6fb2; color: #ffffff; }"
        "QMenu::separator { height: 3px; margin: 1px 8px; }");
    QAction *openAction = menu.addAction(tr("Open code"));
    QAction *restrictAction = nullptr;
    QAction *grantAction = nullptr;
    QAction *kickAction = nullptr;
    if (mInRoom && mIsAdmin && info && info->role != "admin") {
        restrictAction = menu.addAction(info->restricted
            ? tr("Allow editing")
            : tr("Restrict editing"));
        // 2026-11-04: grant/revoke edit permission on MY code.
        grantAction = menu.addAction(info->canEditAdminDoc
            ? tr("Revoke my-code edit")
            : tr("Let edit my code"));
        kickAction = menu.addAction(tr("Kick"));
    }
#ifdef __EMSCRIPTEN__
    // Part 9 (web build): QMenu::exec() wedges the page - a heap
    // menu + popup() with the dispatch in the triggered signal.
    {
        auto *heapMenu = new QMenu(this);
        heapMenu->setStyleSheet(menu.styleSheet());
        QAction *hOpen = heapMenu->addAction(openAction->text());
        QAction *hRestrict = nullptr;
        QAction *hGrant = nullptr;
        QAction *hKick = nullptr;
        if (restrictAction) {
            hRestrict = heapMenu->addAction(restrictAction->text());
            hGrant = heapMenu->addAction(grantAction->text());
            hKick = heapMenu->addAction(kickAction->text());
        }
        const bool wasRestricted = (info && info->restricted);
        const bool wasGranted = (info && info->canEditAdminDoc);
        connect(heapMenu, &QMenu::triggered, this,
                [this, heapMenu, hOpen, hRestrict, hGrant, hKick,
                 memberId, wasRestricted, wasGranted](QAction *chosen) {
            if (chosen == hOpen)
                emit openMemberRequested(memberId);
            else if (hRestrict && chosen == hRestrict)
                emit restrictRequested(memberId, !wasRestricted);
            else if (hGrant && chosen == hGrant)
                emit grantEditRequested(memberId, !wasGranted);
            else if (hKick && chosen == hKick)
                emit kickRequested(memberId);
            heapMenu->deleteLater();
        });
        connect(heapMenu, &QMenu::aboutToHide, heapMenu,
                [heapMenu]() { heapMenu->deleteLater(); });
        heapMenu->popup(mMembersList->mapToGlobal(pos));
    }
    return;
#endif
    QAction *chosen = menu.exec(mMembersList->mapToGlobal(pos));
    if (chosen == openAction)
        emit openMemberRequested(memberId);
    else if (restrictAction && chosen == restrictAction)
        emit restrictRequested(memberId, !(info && info->restricted));
    else if (grantAction && chosen == grantAction)
        emit grantEditRequested(memberId, !(info && info->canEditAdminDoc));
    else if (kickAction && chosen == kickAction)
        emit kickRequested(memberId);
}

void RoomsPanel::onApplySettingsClicked()
{
    RoomSettings settings;
    settings.maxMembers = mMaxMembersSpin->value();
    settings.membersCanEditEachOther = mPeerEditCheck->isChecked();
    settings.projectName = mProjectEdit->text().trimmed();
    emit applySettingsRequested(settings);
}

void RoomsPanel::onCopyCodeClicked()
{
    if (mRoomCode.isEmpty())
        return;
    QApplication::clipboard()->setText(mRoomCode);
    QPushButton *button = mCopyCodeButton;
    button->setText(tr("Copied ✓"));
    // The button pointer is the context object, so the restore is
    // dropped automatically if the button dies first.
    QTimer::singleShot(1500, button, [button]() {
        button->setText(tr("Copy"));
    });
}
