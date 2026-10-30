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
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

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

    auto *membersLabel = new QLabel{tr("Members"), this};
    layout->addWidget(membersLabel);
    mMembersList = new QListWidget{this};
    mMembersList->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(mMembersList);

    mSettingsBox = new QGroupBox{tr("Admin settings"), this};
    auto *settingsLayout = new QVBoxLayout{mSettingsBox};
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
    mSettingsBox->setVisible(mIsAdmin);
    setSettings(settings);
}

void RoomsPanel::setNotInRoom(const QString &message)
{
    mInRoom = false;
    mIsAdmin = false;
    mSelfId.clear();
    mMembersList->clear();
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
        item->setToolTip(member.role == "admin"
                             ? tr("Admin — double-click to view the code")
                             : tr("Double-click to view the code"));
    }
}

void RoomsPanel::setSettings(const RoomSettings &settings)
{
    mMaxMembersSpin->setValue(settings.maxMembers);
    mPeerEditCheck->setChecked(settings.membersCanEditEachOther);
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
    emit applySettingsRequested(settings);
}
