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

#include <QCheckBox>
#include <QDateTime>
#include <QElapsedTimer>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
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
    layout->setContentsMargins(6, 6, 6, 6);

    mStatusLabel = new QLabel{tr("Not in a room"), this};
    mStatusLabel->setWordWrap(true);
    layout->addWidget(mStatusLabel);

    mNameEdit = new QLineEdit{this};
    mNameEdit->setPlaceholderText(tr("Your name"));
    mNameEdit->setMaxLength(24);
    layout->addWidget(mNameEdit);

    mCreateButton = new QPushButton{tr("Create Room"), this};
    layout->addWidget(mCreateButton);

    auto *joinLayout = new QHBoxLayout;
    mCodeEdit = new QLineEdit{this};
    mCodeEdit->setPlaceholderText(tr("4-digit code"));
    mCodeEdit->setMaxLength(4);
    mCodeEdit->setInputMethodHints(Qt::ImhDigitsOnly);
    joinLayout->addWidget(mCodeEdit);
    mJoinButton = new QPushButton{tr("Join Room"), this};
    joinLayout->addWidget(mJoinButton);
    layout->addLayout(joinLayout);

    mCodeLabel = new QLabel{this};
    mCodeLabel->setTextFormat(Qt::RichText);
    mCodeLabel->setVisible(false);
    layout->addWidget(mCodeLabel);

    mProjectLabel = new QLabel{this};
    mProjectLabel->setWordWrap(true);
    mProjectLabel->setStyleSheet(
        "background:#fdecea; border:1px solid #f0b8b2; border-radius:4px;"
        " padding:4px 6px; font-weight:bold; color:#8f1f14;");
    mProjectLabel->setVisible(false);
    layout->addWidget(mProjectLabel);

    auto *membersLabel = new QLabel{tr("Members"), this};
    layout->addWidget(membersLabel);
    mMembersList = new QListWidget{this};
    mMembersList->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(mMembersList);

    mSaveCodeButton = new QPushButton{tr("Save Code…"), this};
    mSaveCodeButton->setToolTip(
        tr("Save the code you are viewing to a file on this computer"));
    mSaveCodeButton->setVisible(false);
    layout->addWidget(mSaveCodeButton);

    // Rooms v2 chat: a simple messaging area under the member list.
    mChatBox = new QGroupBox{tr("Chat"), this};
    auto *chatLayout = new QVBoxLayout{mChatBox};
    mChatList = new QListWidget{mChatBox};
    mChatList->setWordWrap(true);
    mChatList->setSelectionMode(QAbstractItemView::NoSelection);
    chatLayout->addWidget(mChatList);
    mTypingLabel = new QLabel{mChatBox};
    mTypingLabel->setStyleSheet("color:#666; font-style:italic;");
    chatLayout->addWidget(mTypingLabel);
    auto *chatRow = new QHBoxLayout;
    mChatEdit = new QLineEdit{mChatBox};
    mChatEdit->setPlaceholderText(tr("Type a message…"));
    mChatEdit->setMaxLength(500);
    chatRow->addWidget(mChatEdit);
    mChatSendButton = new QPushButton{tr("Send"), mChatBox};
    mChatSendButton->setStyleSheet(
        "background:#e02323; color:white; font-weight:bold; padding:4px 14px;");
    chatRow->addWidget(mChatSendButton);
    chatLayout->addLayout(chatRow);
    mChatBox->setVisible(false);
    layout->addWidget(mChatBox);

    mSettingsBox = new QGroupBox{tr("Admin settings"), this};
    auto *settingsLayout = new QVBoxLayout{mSettingsBox};
    settingsLayout->addWidget(new QLabel{tr("Project name"), mSettingsBox});
    mProjectEdit = new QLineEdit{mSettingsBox};
    mProjectEdit->setPlaceholderText(tr("e.g. Rocket Simulator"));
    mProjectEdit->setMaxLength(60);
    settingsLayout->addWidget(mProjectEdit);
    auto *maxLayout = new QHBoxLayout;
    maxLayout->addWidget(new QLabel{tr("Max members"), mSettingsBox});
    mMaxMembersSpin = new QSpinBox{mSettingsBox};
    mMaxMembersSpin->setRange(2, 50);
    mMaxMembersSpin->setValue(8);
    maxLayout->addWidget(mMaxMembersSpin);
    settingsLayout->addLayout(maxLayout);
    mPeerEditCheck = new QCheckBox{
        tr("Members can edit each other's code"), mSettingsBox};
    settingsLayout->addWidget(mPeerEditCheck);
    auto *applyButton = new QPushButton{tr("Apply settings"), mSettingsBox};
    settingsLayout->addWidget(applyButton);
    mSettingsBox->setVisible(false);
    layout->addWidget(mSettingsBox);

    mLeaveButton = new QPushButton{tr("Leave Room"), this};
    mLeaveButton->setVisible(false);
    layout->addWidget(mLeaveButton);
    layout->addStretch();

    connect(mCreateButton, &QPushButton::clicked,
            this, &RoomsPanel::onCreateClicked);
    connect(mJoinButton, &QPushButton::clicked,
            this, &RoomsPanel::onJoinClicked);
    connect(mCodeEdit, &QLineEdit::returnPressed,
            this, &RoomsPanel::onJoinClicked);
    connect(mLeaveButton, &QPushButton::clicked,
            this, &RoomsPanel::leaveRequested);
    connect(applyButton, &QPushButton::clicked,
            this, &RoomsPanel::onApplySettingsClicked);
    connect(mMembersList, &QListWidget::itemDoubleClicked,
            this, &RoomsPanel::onMemberDoubleClicked);
    connect(mMembersList, &QWidget::customContextMenuRequested,
            this, &RoomsPanel::onMemberContextMenu);
    connect(mSaveCodeButton, &QPushButton::clicked,
            this, &RoomsPanel::saveCodeRequested);
    connect(mChatSendButton, &QPushButton::clicked,
            this, &RoomsPanel::onChatSendClicked);
    connect(mChatEdit, &QLineEdit::returnPressed,
            this, &RoomsPanel::onChatSendClicked);
    connect(mChatEdit, &QLineEdit::textEdited,
            this, &RoomsPanel::onChatTextEdited);

    mTypingClearTimer = new QTimer{this};
    mTypingClearTimer->setSingleShot(true);
    connect(mTypingClearTimer, &QTimer::timeout, this, [this]() {
        mTypingLabel->clear();
    });
}

QString RoomsPanel::userName() const
{
    return mNameEdit->text().trimmed();
}

void RoomsPanel::setConnecting(const QString &message)
{
    mStatusLabel->setText(message);
    mCreateButton->setEnabled(false);
    mJoinButton->setEnabled(false);
}

void RoomsPanel::setInRoom(const QString &code, const QString &role,
                           const RoomSettings &settings)
{
    mInRoom = true;
    mIsAdmin = (role == "admin");
    mCodeLabel->setText(tr("Room code: <b>%1</b>").arg(code));
    mCodeLabel->setVisible(true);
    mStatusLabel->setText(mIsAdmin
        ? tr("You are the admin of this room.")
        : tr("You are a member of this room."));
    mNameEdit->setEnabled(false);
    mCreateButton->setVisible(false);
    mCodeEdit->setVisible(false);
    mJoinButton->setVisible(false);
    mLeaveButton->setVisible(true);
    mSaveCodeButton->setVisible(true);
    mChatBox->setVisible(true);
    mChatList->clear();
    mTypingLabel->clear();
    mSettingsBox->setVisible(mIsAdmin);
    setSettings(settings);
}

void RoomsPanel::setNotInRoom(const QString &message)
{
    mInRoom = false;
    mIsAdmin = false;
    mSelfId.clear();
    mMembersList->clear();
    mChatList->clear();
    mChatEdit->clear();
    mTypingLabel->clear();
    mProjectLabel->setVisible(false);
    mSaveCodeButton->setVisible(false);
    mChatBox->setVisible(false);
    mCodeLabel->setVisible(false);
    mStatusLabel->setText(message.isEmpty() ? tr("Not in a room") : message);
    mNameEdit->setEnabled(true);
    mCreateButton->setVisible(true);
    mCreateButton->setEnabled(true);
    mCodeEdit->setVisible(true);
    mJoinButton->setVisible(true);
    mJoinButton->setEnabled(true);
    mLeaveButton->setVisible(false);
    mSettingsBox->setVisible(false);
}

void RoomsPanel::setMembers(const QVector<RoomMemberInfo> &members,
                            const QString &selfId)
{
    mSelfId = selfId;
    mMembersList->clear();
    for (const RoomMemberInfo &member : members) {
        QString text = member.name;
        if (member.role == "admin")
            text += " ★";
        if (member.id == selfId)
            text += tr(" (you)");
        auto *item = new QListWidgetItem{text, mMembersList};
        item->setData(Qt::UserRole, member.id);
        item->setIcon(dotIcon(memberColor(member.id)));
        item->setToolTip(member.role == "admin"
                             ? tr("Admin — double-click to view the code")
                             : tr("Double-click to view the code"));
    }
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

void RoomsPanel::setChatLog(const QVector<RoomChatMessage> &log,
                            const QString &selfId)
{
    mChatList->clear();
    for (const RoomChatMessage &message : log)
        appendChatMessage(message.fromId, message.fromName, message.text,
                          message.ts);
    Q_UNUSED(selfId);
}

void RoomsPanel::appendChatMessage(const QString &fromId,
                                   const QString &fromName,
                                   const QString &text, qint64 ts)
{
    QString who = (fromId == mSelfId) ? tr("You") : fromName;
    auto *item = new QListWidgetItem{tr("%1: %2").arg(who, text), mChatList};
    item->setForeground(memberColor(fromId));
    if (ts > 0)
        item->setToolTip(QDateTime::fromMSecsSinceEpoch(ts)
                             .toString("h:mm AP"));
    mChatList->scrollToBottom();
    // The sender clearly stopped typing once their message lands.
    if (fromId != mSelfId) {
        mTypingClearTimer->stop();
        mTypingLabel->clear();
    }
}

void RoomsPanel::showPeerTyping(const QString &fromName)
{
    mTypingLabel->setText(tr("%1 is typing…").arg(fromName));
    mTypingClearTimer->start(2800);
}

void RoomsPanel::onChatSendClicked()
{
    QString text = mChatEdit->text().trimmed();
    if (text.isEmpty())
        return;
    mChatEdit->clear();
    emit chatSendRequested(text);
}

void RoomsPanel::onChatTextEdited(const QString &text)
{
    Q_UNUSED(text);
    // At most one typing ping per 2 seconds while typing.
    if (!mTypingSendTimer.isValid() || mTypingSendTimer.elapsed() >= 2000) {
        mTypingSendTimer.start();
        emit chatTyping();
    }
}

void RoomsPanel::setSettings(const RoomSettings &settings)
{
    mMaxMembersSpin->setValue(settings.maxMembers);
    mPeerEditCheck->setChecked(settings.membersCanEditEachOther);
    if (mProjectEdit->text() != settings.projectName)
        mProjectEdit->setText(settings.projectName);
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
    QMenu menu{this};
    QAction *openAction = menu.addAction(tr("Open code"));
    QAction *kickAction = nullptr;
    if (mInRoom && mIsAdmin)
        kickAction = menu.addAction(tr("Kick"));
    QAction *chosen = menu.exec(mMembersList->mapToGlobal(pos));
    if (chosen == openAction)
        emit openMemberRequested(memberId);
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
