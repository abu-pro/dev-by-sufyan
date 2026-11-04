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
class QDialog;
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
    void selectTab(int index); // 0 Room, 1 Members, 2 Settings, 3 Activity
    void focusJoinField();     // focus the code field (Join from menu)

    // v1.5.0: live ping indicator (header line, visible in a room).
    // rttMs >= 0: measured round trip; -1: a ping went unanswered.
    void setPingRtt(int rttMs);

    // 2026-11-04 (grants + history + activity): the Activity tab's
    // feed. setRoomEvents() loads a room snapshot's recent events
    // (oldest first, as sent); addRoomEvent() appends one live event.
    // Events arriving while the Activity tab is not selected raise an
    // unread count on the tab label, like the web client's badge.
    void setRoomEvents(const QVector<RoomEvent> &events);
    void addRoomEvent(const RoomEvent &event);
    // Edit-history dialog for one member's document: opened in its
    // loading state by openHistoryDialog() (which also emits
    // historyRequested), then filled by setDocHistory() when the
    // server answers or pushes a fresher list.
    void openHistoryDialog(const QString &memberId,
                           const QString &memberName);
    void setDocHistory(const QString &targetId,
                       const QVector<RoomHistoryEntry> &entries,
                       bool denied);

    // Stable per-member color (same palette idea as the mobile app).
    static QColor memberColor(const QString &memberId);

signals:
    void createRequested(const QString &name);
    void joinRequested(const QString &name, const QString &code);
    void leaveRequested();
    void openMemberRequested(const QString &memberId);
    void kickRequested(const QString &memberId);
    void restrictRequested(const QString &memberId, bool restricted);
    // 2026-11-04: admin grants/revokes the selected member's
    // permission to edit the admin's own code.
    void grantEditRequested(const QString &memberId, bool granted);
    // 2026-11-04: show the edit history of the given member's doc.
    void historyRequested(const QString &memberId);
    // 2026-11-04: copy the given member's code into my own document.
    void copyToMyFileRequested(const QString &memberId);
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
    void onGrantClicked();
    void onHistoryClicked();
    void onCopyToMyFileClicked();
    void onApplySettingsClicked();
    void onCopyCodeClicked();
    void onTabChanged(int index);

private:
    QString memberIdOf(QListWidgetItem *item) const;
    const RoomMemberInfo *memberInfo(const QString &memberId) const;
    void updateMemberActionButtons();
    void refreshEventsList();
    void updateActivityTabLabel();
    static QString eventDisplayText(const RoomEvent &event);
    static QString timeDisplayText(qint64 ts);
    static QString languageDisplayName(const QString &language);

    // Header
    QLabel *mStatusLabel;
    QLabel *mConnLabel;
    QLabel *mPingLabel;
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
    // 2026-11-04: grants / history / copy actions for the selection.
    QPushButton *mGrantButton;
    QPushButton *mHistoryButton;
    QPushButton *mCopyToMyFileButton;
    // Activity tab (2026-11-04): the room-activity feed.
    QListWidget *mEventsList;
    QVector<RoomEvent> mEvents; // newest first, as displayed
    int mActivityTabIndex = -1;
    int mActivityUnread = 0;
    // Edit-history dialog (2026-11-04), created on first use.
    QDialog *mHistoryDialog = nullptr;
    QLabel *mHistoryTitle = nullptr;
    QListWidget *mHistoryList = nullptr;
    QString mHistoryTargetId;
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
