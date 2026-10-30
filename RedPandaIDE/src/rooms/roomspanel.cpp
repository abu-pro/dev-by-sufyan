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

#include <QAbstractItemModel>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEvent>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QStyledItemDelegate>
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

// ---------------------------------------------------------------------
// WhatsApp-style chat bubbles, painted by a delegate (v1.2.0).
// Own messages sit right in a green bubble; other members' messages sit
// left in a white/dark bubble with their name in their member color and
// a small timestamp inside the bubble. Painting (rather than per-item
// widgets) keeps word-wrap sizing exact and cheap for long histories.
// ---------------------------------------------------------------------
enum ChatRoles {
    ChatTextRole = Qt::UserRole + 1,
    ChatNameRole,
    ChatOwnRole,
    ChatColorRole,
    ChatTimeRole,
};

class ChatBubbleDelegate : public QStyledItemDelegate
{
public:
    explicit ChatBubbleDelegate(QListWidget *view) :
        QStyledItemDelegate{view}, mView{view}
    {
        if (mView && mView->viewport())
            mView->viewport()->installEventFilter(this);
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        // Bubble sizes depend on the list width: when it changes, every
        // cached size hint is stale.
        if (mView && watched == mView->viewport()
                && event->type() == QEvent::Resize) {
            if (QAbstractItemModel *model = mView->model()) {
                for (int row = 0; row < model->rowCount(); ++row)
                    emit sizeHintChanged(model->index(row, 0));
            }
        }
        return QStyledItemDelegate::eventFilter(watched, event);
    }

    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override
    {
        int width = mView ? mView->viewport()->width() : option.rect.width();
        if (width <= 0)
            width = 280;
        Geom g = compute(option.font, index, width);
        return QSize{width, g.bubbleH + 2 * kVMargin};
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        int width = mView ? mView->viewport()->width() : option.rect.width();
        Geom g = compute(option.font, index, width);
        bool own = index.data(ChatOwnRole).toBool();
        QRect area = option.rect;
        QRect bubble;
        if (own)
            bubble = QRect{area.right() - kSide - g.bubbleW + 1,
                           area.top() + kVMargin, g.bubbleW, g.bubbleH};
        else
            bubble = QRect{area.left() + kSide, area.top() + kVMargin,
                           g.bubbleW, g.bubbleH};

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(own ? ownBubbleColor() : otherBubbleColor());
        painter->drawRoundedRect(bubble, 10, 10);

        int x = bubble.left() + kPadX;
        int y = bubble.top() + kPadY;
        if (!own) {
            QFont nameFont = option.font;
            nameFont.setBold(true);
            painter->setFont(nameFont);
            painter->setPen(index.data(ChatColorRole).value<QColor>());
            painter->drawText(QRect{x, y, g.textW, g.nameH},
                              Qt::AlignLeft | Qt::AlignTop,
                              index.data(ChatNameRole).toString());
            y += g.nameH;
        }
        painter->setFont(option.font);
        painter->setPen(textColor());
        painter->drawText(QRect{x, y, g.textW, g.textH},
                          Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop,
                          index.data(ChatTextRole).toString());
        y += g.textH + 2;

        QFont timeFont = option.font;
        if (timeFont.pointSize() > 0)
            timeFont.setPointSize(timeFont.pointSize() - 2);
        painter->setFont(timeFont);
        painter->setPen(timeColor());
        painter->drawText(
                    QRect{bubble.right() - kPadX - g.timeW, y, g.timeW, g.timeH},
                    Qt::AlignRight | Qt::AlignTop,
                    index.data(ChatTimeRole).toString());
        painter->restore();
    }

private:
    struct Geom {
        int bubbleW = 0;
        int bubbleH = 0;
        int textW = 0;
        int textH = 0;
        int nameH = 0;
        int timeW = 0;
        int timeH = 0;
    };

    Geom compute(const QFont &font, const QModelIndex &index, int viewW) const
    {
        bool own = index.data(ChatOwnRole).toBool();
        QString text = index.data(ChatTextRole).toString();
        QString name = index.data(ChatNameRole).toString();
        QString time = index.data(ChatTimeRole).toString();

        int bubbleMaxW = qMax(140, int((viewW - 2 * kSide) * 0.78));
        int textMaxW = bubbleMaxW - 2 * kPadX;

        QFontMetrics fm{font};
        QRect textRect = fm.boundingRect(QRect{0, 0, textMaxW, 100000},
                                         Qt::TextWordWrap, text);
        QFont nameFont = font;
        nameFont.setBold(true);
        QFontMetrics nameFm{nameFont};
        int nameW = own ? 0 : nameFm.horizontalAdvance(name);
        QFont timeFont = font;
        if (timeFont.pointSize() > 0)
            timeFont.setPointSize(timeFont.pointSize() - 2);
        QFontMetrics timeFm{timeFont};

        Geom g;
        g.textW = qMax(qMax(textRect.width(), nameW),
                       timeFm.horizontalAdvance(time));
        g.textW = qMin(g.textW, textMaxW);
        g.textH = textRect.height();
        g.nameH = own ? 0 : nameFm.height() + 2;
        g.timeW = timeFm.horizontalAdvance(time);
        g.timeH = timeFm.height();
        g.bubbleW = g.textW + 2 * kPadX;
        g.bubbleH = kPadY + g.nameH + g.textH + 2 + g.timeH + kPadY;
        return g;
    }

    bool darkMode() const
    {
        return mView
                && mView->palette().color(QPalette::Window).lightness() < 110;
    }
    QColor ownBubbleColor() const
    {
        return darkMode() ? QColor{"#005c4b"} : QColor{"#dcf8c6"};
    }
    QColor otherBubbleColor() const
    {
        return darkMode() ? QColor{"#202c33"} : QColor{"#ffffff"};
    }
    QColor textColor() const
    {
        return darkMode() ? QColor{"#e9edef"} : QColor{"#111b21"};
    }
    QColor timeColor() const
    {
        return darkMode() ? QColor{"#8696a0"} : QColor{"#667781"};
    }

    static constexpr int kSide = 8;    // list edge to bubble
    static constexpr int kVMargin = 3; // bubble vertical spacing
    static constexpr int kPadX = 10;   // bubble inner padding
    static constexpr int kPadY = 6;

    QListWidget *mView;
};

} // namespace

RoomsPanel::RoomsPanel(QWidget *parent) :
    QWidget{parent}
{
    auto *layout = new QVBoxLayout{this};
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    // ---- Room section -------------------------------------------------
    auto *roomBox = new QGroupBox{tr("Room"), this};
    auto *roomLayout = new QVBoxLayout{roomBox};
    roomLayout->setSpacing(6);

    mStatusLabel = new QLabel{tr("Not in a room"), roomBox};
    mStatusLabel->setWordWrap(true);
    roomLayout->addWidget(mStatusLabel);

    mConnLabel = new QLabel{
        tr("Connection lost — reconnecting…"), roomBox};
    mConnLabel->setWordWrap(true);
    mConnLabel->setStyleSheet(
        "background:#fff4e5; border:1px solid #f0c36d; border-radius:4px;"
        " color:#7a4d00; padding:4px 6px; font-weight:bold;");
    mConnLabel->setVisible(false);
    roomLayout->addWidget(mConnLabel);

    // create / join (visible while not in a room)
    mJoinBox = new QWidget{roomBox};
    auto *joinLayout = new QVBoxLayout{mJoinBox};
    joinLayout->setContentsMargins(0, 0, 0, 0);
    joinLayout->setSpacing(6);
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
    roomLayout->addWidget(mJoinBox);

    // in-room info (visible while in a room)
    mRoomInfoBox = new QWidget{roomBox};
    auto *infoLayout = new QVBoxLayout{mRoomInfoBox};
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(6);
    auto *codeRow = new QHBoxLayout;
    codeRow->addWidget(new QLabel{tr("Room code:"), mRoomInfoBox});
    mCodeLabel = new QLabel{mRoomInfoBox};
    mCodeLabel->setStyleSheet(
        "font-size:18pt; font-weight:bold; letter-spacing:2px;");
    codeRow->addWidget(mCodeLabel);
    mCopyCodeButton = new QPushButton{tr("Copy"), mRoomInfoBox};
    mCopyCodeButton->setToolTip(tr("Copy the room code"));
    codeRow->addWidget(mCopyCodeButton);
    codeRow->addStretch();
    infoLayout->addLayout(codeRow);
    mProjectLabel = new QLabel{mRoomInfoBox};
    mProjectLabel->setWordWrap(true);
    mProjectLabel->setStyleSheet(
        "background:#fdecea; border:1px solid #f0b8b2; border-radius:4px;"
        " padding:4px 6px; font-weight:bold; color:#8f1f14;");
    mProjectLabel->setVisible(false);
    infoLayout->addWidget(mProjectLabel);
    mLeaveButton = new QPushButton{tr("Leave Room"), mRoomInfoBox};
    infoLayout->addWidget(mLeaveButton);
    mRoomInfoBox->setVisible(false);
    roomLayout->addWidget(mRoomInfoBox);
    layout->addWidget(roomBox);

    // ---- Members section ----------------------------------------------
    mMembersBox = new QGroupBox{tr("Members"), this};
    auto *membersLayout = new QVBoxLayout{mMembersBox};
    membersLayout->setSpacing(6);
    mMembersList = new QListWidget{mMembersBox};
    mMembersList->setContextMenuPolicy(Qt::CustomContextMenu);
    membersLayout->addWidget(mMembersList);
    mSaveCodeButton = new QPushButton{tr("Save Code…"), mMembersBox};
    mSaveCodeButton->setToolTip(
        tr("Save the code you are viewing to a file on this computer"));
    membersLayout->addWidget(mSaveCodeButton);
    mMembersBox->setVisible(false);
    layout->addWidget(mMembersBox);

    // ---- Chat section (WhatsApp-style) ---------------------------------
    mChatBox = new QGroupBox{tr("Chat"), this};
    auto *chatLayout = new QVBoxLayout{mChatBox};
    chatLayout->setSpacing(6);
    mChatList = new QListWidget{mChatBox};
    mChatList->setItemDelegate(new ChatBubbleDelegate{mChatList});
    mChatList->setSelectionMode(QAbstractItemView::NoSelection);
    mChatList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    mChatList->setStyleSheet(palette().color(QPalette::Window).lightness() < 110
        ? "QListWidget { background:#0b141a; border:1px solid #2a3942; }"
        : "QListWidget { background:#ece5dd; border:1px solid #ddd6cc; }");
    chatLayout->addWidget(mChatList);
    mTypingLabel = new QLabel{mChatBox};
    mTypingLabel->setStyleSheet("color:#666; font-style:italic;");
    chatLayout->addWidget(mTypingLabel);
    auto *chatRow = new QHBoxLayout;
    chatRow->setSpacing(6);
    mChatEdit = new QLineEdit{mChatBox};
    mChatEdit->setPlaceholderText(tr("Type a message…"));
    mChatEdit->setMaxLength(500);
    mChatEdit->setStyleSheet(
        "QLineEdit { border:1px solid #b9b9b9; border-radius:13px;"
        " padding:5px 10px; }");
    chatRow->addWidget(mChatEdit);
    mChatSendButton = new QPushButton{tr("Send"), mChatBox};
    mChatSendButton->setStyleSheet(
        "QPushButton { background:#25d366; color:white; border:none;"
        " border-radius:13px; padding:6px 16px; font-weight:bold; }"
        "QPushButton:disabled { background:#a8a8a8; }");
    chatRow->addWidget(mChatSendButton);
    chatLayout->addLayout(chatRow);
    mChatBox->setVisible(false);
    layout->addWidget(mChatBox);

    // ---- Admin settings -------------------------------------------------
    mSettingsBox = new QGroupBox{tr("Admin settings"), this};
    auto *settingsLayout = new QVBoxLayout{mSettingsBox};
    settingsLayout->setSpacing(6);
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
    layout->addStretch();

    connect(mCreateButton, &QPushButton::clicked,
            this, &RoomsPanel::onCreateClicked);
    connect(mJoinButton, &QPushButton::clicked,
            this, &RoomsPanel::onJoinClicked);
    connect(mCodeEdit, &QLineEdit::returnPressed,
            this, &RoomsPanel::onJoinClicked);
    connect(mLeaveButton, &QPushButton::clicked,
            this, &RoomsPanel::leaveRequested);
    connect(mCopyCodeButton, &QPushButton::clicked,
            this, &RoomsPanel::onCopyCodeClicked);
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
    mRoomInfoBox->setVisible(true);
    mMembersBox->setVisible(true);
    mChatBox->setVisible(true);
    mChatList->clear();
    mTypingLabel->clear();
    mSettingsBox->setVisible(mIsAdmin);
    setReconnecting(false);
    setSettings(settings);
}

void RoomsPanel::setNotInRoom(const QString &message)
{
    mInRoom = false;
    mIsAdmin = false;
    mSelfId.clear();
    mRoomCode.clear();
    mMembersList->clear();
    mChatList->clear();
    mChatEdit->clear();
    mTypingLabel->clear();
    mProjectLabel->setVisible(false);
    mConnLabel->setVisible(false);
    mJoinBox->setVisible(true);
    mJoinBox->setEnabled(true);
    mRoomInfoBox->setVisible(false);
    mMembersBox->setVisible(false);
    mChatBox->setVisible(false);
    mSettingsBox->setVisible(false);
    mStatusLabel->setText(message.isEmpty() ? tr("Not in a room") : message);
}

void RoomsPanel::setReconnecting(bool reconnecting)
{
    mConnLabel->setVisible(reconnecting && mInRoom);
    mChatEdit->setEnabled(!reconnecting);
    mChatSendButton->setEnabled(!reconnecting);
}

void RoomsPanel::setMembers(const QVector<RoomMemberInfo> &members,
                            const QString &selfId)
{
    mSelfId = selfId;
    mMembersList->clear();
    for (const RoomMemberInfo &member : members) {
        QStringList badges;
        if (member.role == "admin")
            badges << tr("Admin");
        if (member.id == selfId)
            badges << tr("You");
        if (member.away)
            badges << tr("Away");
        QString text = member.name;
        if (!badges.isEmpty())
            text += " · " + badges.join(" · ");
        auto *item = new QListWidgetItem{text, mMembersList};
        item->setData(Qt::UserRole, member.id);
        item->setIcon(dotIcon(member.away ? QColor{"#b0b0b0"}
                                          : memberColor(member.id)));
        if (member.away)
            item->setForeground(QBrush{QColor{"#888888"}});
        item->setToolTip(member.away
            ? tr("Connection lost — the seat is held for a while")
            : member.role == "admin"
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
    mSelfId = selfId;
    mChatList->clear();
    for (const RoomChatMessage &message : log)
        appendChatMessage(message.fromId, message.fromName, message.text,
                          message.ts);
    mChatList->scrollToBottom();
}

void RoomsPanel::appendChatMessage(const QString &fromId,
                                   const QString &fromName,
                                   const QString &text, qint64 ts)
{
    auto *item = new QListWidgetItem{mChatList};
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    item->setData(ChatTextRole, text);
    item->setData(ChatNameRole, fromName);
    item->setData(ChatOwnRole, fromId == mSelfId);
    item->setData(ChatColorRole, memberColor(fromId));
    item->setData(ChatTimeRole, ts > 0
        ? QDateTime::fromMSecsSinceEpoch(ts).toString("h:mm AP")
        : QString());
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

void RoomsPanel::onCopyCodeClicked()
{
    if (mRoomCode.isEmpty())
        return;
    QApplication::clipboard()->setText(mRoomCode);
    QPushButton *button = mCopyCodeButton;
    button->setText(tr("Copied ✓"));
    // The context object guards the restore if the button dies first.
    QTimer::singleShot(1500, button, [button]() {
        button->setText(tr("Copy"));
    });
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
