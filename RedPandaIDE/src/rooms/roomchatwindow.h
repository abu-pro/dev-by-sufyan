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
#ifndef ROOMS_ROOMCHATWINDOW_H
#define ROOMS_ROOMCHATWINDOW_H

#include <QWidget>
#include "roomclient.h"

#include <QElapsedTimer>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTimer;

// RoomChatWindow is the room chat as a standalone popup window (v1.3.0):
// WhatsApp-style bubbles, a composer and a typing line, moved out of the
// Rooms dock. MainWindow owns it: it opens from the Messages buttons and
// opens by itself when another member writes. It only displays and emits
// intents; RoomClient does the networking.
class RoomChatWindow : public QWidget
{
    Q_OBJECT
public:
    explicit RoomChatWindow(QWidget *parent = nullptr);

    void setSelfId(const QString &selfId) { mSelfId = selfId; }
    void setChatLog(const QVector<RoomChatMessage> &log);
    void appendChatMessage(const QString &fromId, const QString &fromName,
                           const QString &text, qint64 ts);
    void showPeerTyping(const QString &fromName);
    void setComposerEnabled(bool enabled);
    void clearAll();

signals:
    void sendRequested(const QString &text);
    void typing();
    // Emitted every time the window becomes visible, so the owner can
    // clear the unread badge.
    void becameVisible();

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void onSendClicked();
    void onTextEdited(const QString &text);

private:
    QListWidget *mChatList;
    QLabel *mTypingLabel;
    QLineEdit *mChatEdit;
    QPushButton *mSendButton;
    QTimer *mTypingClearTimer;
    QElapsedTimer mTypingSendTimer;
    QString mSelfId;
};

#endif // ROOMS_ROOMCHATWINDOW_H
