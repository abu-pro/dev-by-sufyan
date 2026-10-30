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
#ifndef ROOMS_ROOMSPANEL_H
#define ROOMS_ROOMSPANEL_H

#include <QColor>
#include <QWidget>
#include "roomclient.h"

#include <QElapsedTimer>

class QCheckBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QSpinBox;
class QTimer;
class QWidget;

// RoomsPanel is the "Rooms" dock widget content, organized in clear
// sections: Room (create/join, big copyable code, connection state),
// Members (color dot + role badge per member), Chat (WhatsApp-style
// bubbles) and Admin settings. It only emits user intents; all
// networking and editor handling lives in RoomClient and MainWindow.
class RoomsPanel : public QWidget
{
    Q_OBJECT
public:
    explicit RoomsPanel(QWidget *parent = nullptr);

    QString userName() const;

    void setConnecting(const QString &message);
    void setInRoom(const QString &code, const QString &role,
                   const RoomSettings &settings);
    void setNotInRoom(const QString &message = QString());
    // v1.2.0: show/hide the amber "reconnecting" banner and freeze the
    // chat composer while the client resumes the session.
    void setReconnecting(bool reconnecting);
    void setMembers(const QVector<RoomMemberInfo> &members,
                    const QString &selfId);
    void setSettings(const RoomSettings &settings);
    void showMessage(const QString &message);
    // Rooms v2 chat (WhatsApp-style bubbles since v1.2.0)
    void setChatLog(const QVector<RoomChatMessage> &log,
                    const QString &selfId);
    void appendChatMessage(const QString &fromId, const QString &fromName,
                           const QString &text, qint64 ts);
    void showPeerTyping(const QString &fromName);
    QString selectedMemberId() const;

    // Stable per-member color (same palette idea as the mobile app).
    static QColor memberColor(const QString &memberId);

signals:
    void createRequested(const QString &name);
    void joinRequested(const QString &name, const QString &code);
    void leaveRequested();
    void openMemberRequested(const QString &memberId);
    void kickRequested(const QString &memberId);
    void applySettingsRequested(const RoomSettings &settings);
    void chatSendRequested(const QString &text);
    void chatTyping();
    void saveCodeRequested();

private slots:
    void onCreateClicked();
    void onJoinClicked();
    void onMemberDoubleClicked(QListWidgetItem *item);
    void onMemberContextMenu(const QPoint &pos);
    void onApplySettingsClicked();
    void onChatSendClicked();
    void onChatTextEdited(const QString &text);
    void onCopyCodeClicked();

private:
    QString memberIdOf(QListWidgetItem *item) const;

    // "Room" section: join widgets vs in-room widgets are two containers.
    QWidget *mJoinBox;
    QLineEdit *mNameEdit;
    QPushButton *mCreateButton;
    QLineEdit *mCodeEdit;
    QPushButton *mJoinButton;
    QWidget *mRoomInfoBox;
    QLabel *mCodeLabel;
    QPushButton *mCopyCodeButton;
    QLabel *mProjectLabel;
    QPushButton *mLeaveButton;
    QLabel *mStatusLabel;
    QLabel *mConnLabel;
    // "Members" section
    QGroupBox *mMembersBox;
    QListWidget *mMembersList;
    QPushButton *mSaveCodeButton;
    // "Chat" section
    QGroupBox *mChatBox;
    QListWidget *mChatList;
    QLabel *mTypingLabel;
    QLineEdit *mChatEdit;
    QPushButton *mChatSendButton;
    // "Admin settings" section
    QGroupBox *mSettingsBox;
    QLineEdit *mProjectEdit;
    QSpinBox *mMaxMembersSpin;
    QCheckBox *mPeerEditCheck;
    QTimer *mTypingClearTimer;
    QElapsedTimer mTypingSendTimer;

    bool mInRoom = false;
    bool mIsAdmin = false;
    QString mSelfId;
    QString mRoomCode;
};

#endif // ROOMS_ROOMSPANEL_H
