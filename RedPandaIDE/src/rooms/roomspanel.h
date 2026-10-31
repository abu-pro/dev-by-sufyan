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

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QSpinBox;
class QTabWidget;

// RoomsPanel is the "Rooms" dock widget content. v1.3.0 layout: a slim
// header (status, reconnect banner, Messages button with unread count),
// the create/join box while outside a room, and — once in a room — a
// tab widget: "Room" (big copyable code, project name, leave),
// "Members" (color dot, role / away / restricted badges, per-member
// admin actions: restrict editing, kick) and "Settings" (admin
// controls; read-only for members). Chat lives in RoomChatWindow, not
// here. The panel only emits user intents; all networking and editor
// handling lives in RoomClient and MainWindow.
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
    // v1.2.0: show/hide the amber "reconnecting" banner.
    void setReconnecting(bool reconnecting);
    void setMembers(const QVector<RoomMemberInfo> &members,
                    const QString &selfId);
    void setSettings(const RoomSettings &settings);
    void showMessage(const QString &message);
    QString selectedMemberId() const;

    // v1.3.0 additions
    void setUnreadCount(int count);
    void selectTab(int index); // 0 Room, 1 Members, 2 Settings
    void focusJoinField();     // focus the code field (Join from menu)

    // Stable per-member color (same palette idea as the mobile app).
    static QColor memberColor(const QString &memberId);

signals:
    void createRequested(const QString &name);
    void joinRequested(const QString &name, const QString &code);
    void leaveRequested();
    void openMemberRequested(const QString &memberId);
    void kickRequested(const QString &memberId);
    void restrictRequested(const QString &memberId, bool restricted);
    void applySettingsRequested(const RoomSettings &settings);
    void messagesRequested();
    void saveCodeRequested();

private slots:
    void onCreateClicked();
    void onJoinClicked();
    void onMemberDoubleClicked(QListWidgetItem *item);
    void onMemberContextMenu(const QPoint &pos);
    void onMemberSelectionChanged();
    void onRestrictClicked();
    void onApplySettingsClicked();
    void onCopyCodeClicked();

private:
    QString memberIdOf(QListWidgetItem *item) const;
    const RoomMemberInfo *memberInfo(const QString &memberId) const;
    void updateMemberActionButtons();
    static QString languageDisplayName(const QString &language);

    // Header
    QLabel *mStatusLabel;
    QLabel *mConnLabel;
    QPushButton *mMessagesButton;
    // Join box (not in a room)
    QWidget *mJoinBox;
    QLineEdit *mNameEdit;
    QPushButton *mCreateButton;
    QLineEdit *mCodeEdit;
    QPushButton *mJoinButton;
    // Tabs (in a room)
    QTabWidget *mTabs;
    // Room tab
    QLabel *mCodeLabel;
    QPushButton *mCopyCodeButton;
    QLabel *mProjectLabel;
    QPushButton *mLeaveButton;
    // Members tab
    QListWidget *mMembersList;
    QPushButton *mSaveCodeButton;
    QPushButton *mRestrictButton;
    QPushButton *mKickButton;
    // Settings tab
    QLineEdit *mProjectEdit;
    QSpinBox *mMaxMembersSpin;
    QCheckBox *mPeerEditCheck;
    QPushButton *mApplySettingsButton;
    QLabel *mSettingsNote;

    bool mInRoom = false;
    bool mIsAdmin = false;
    QString mSelfId;
    QString mRoomCode;
    QVector<RoomMemberInfo> mMembers;
};

#endif // ROOMS_ROOMSPANEL_H
