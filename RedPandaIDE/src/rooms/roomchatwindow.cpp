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
#include "roomchatwindow.h"
#include "roomspanel.h" // RoomsPanel::memberColor (stable per-member color)

#include <QAbstractItemModel>
#include <QDateTime>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QShowEvent>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QVBoxLayout>

namespace {

// ---------------------------------------------------------------------
// WhatsApp-style chat bubbles, painted by a delegate (since v1.2.0,
// moved here from the Rooms panel in v1.3.0). Own messages sit right in
// a green bubble; other members' messages sit left in a white/dark
// bubble with their name in their member color and a small timestamp
// inside the bubble. Painting (rather than per-item widgets) keeps
// word-wrap sizing exact and cheap for long histories.
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

RoomChatWindow::RoomChatWindow(QWidget *parent) :
    QWidget{parent, Qt::Window}
{
    setWindowTitle(tr("Room Messages — Dev by Sufyan"));
    resize(400, 540);

    auto *layout = new QVBoxLayout{this};
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    mChatList = new QListWidget{this};
    mChatList->setItemDelegate(new ChatBubbleDelegate{mChatList});
    mChatList->setSelectionMode(QAbstractItemView::NoSelection);
    mChatList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    mChatList->setStyleSheet(palette().color(QPalette::Window).lightness() < 110
        ? "QListWidget { background:#0b141a; border:1px solid #2a3942; }"
        : "QListWidget { background:#ece5dd; border:1px solid #ddd6cc; }");
    layout->addWidget(mChatList);

    mTypingLabel = new QLabel{this};
    mTypingLabel->setStyleSheet("color:#666; font-style:italic;");
    layout->addWidget(mTypingLabel);

    auto *row = new QHBoxLayout;
    row->setSpacing(6);
    mChatEdit = new QLineEdit{this};
    mChatEdit->setPlaceholderText(tr("Type a message…"));
    mChatEdit->setMaxLength(500);
    mChatEdit->setStyleSheet(
        "QLineEdit { border:1px solid #b9b9b9; border-radius:13px;"
        " padding:5px 10px; }");
    row->addWidget(mChatEdit);
    mSendButton = new QPushButton{tr("Send"), this};
    mSendButton->setStyleSheet(
        "QPushButton { background:#25d366; color:white; border:none;"
        " border-radius:13px; padding:6px 16px; font-weight:bold; }"
        "QPushButton:disabled { background:#a8a8a8; }");
    row->addWidget(mSendButton);
    layout->addLayout(row);

    connect(mSendButton, &QPushButton::clicked,
            this, &RoomChatWindow::onSendClicked);
    connect(mChatEdit, &QLineEdit::returnPressed,
            this, &RoomChatWindow::onSendClicked);
    connect(mChatEdit, &QLineEdit::textEdited,
            this, &RoomChatWindow::onTextEdited);

    mTypingClearTimer = new QTimer{this};
    mTypingClearTimer->setSingleShot(true);
    connect(mTypingClearTimer, &QTimer::timeout, this, [this]() {
        mTypingLabel->clear();
    });
}

void RoomChatWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    emit becameVisible();
    mChatList->scrollToBottom();
}

void RoomChatWindow::setChatLog(const QVector<RoomChatMessage> &log)
{
    mChatList->clear();
    for (const RoomChatMessage &message : log)
        appendChatMessage(message.fromId, message.fromName, message.text,
                          message.ts);
    mChatList->scrollToBottom();
}

void RoomChatWindow::appendChatMessage(const QString &fromId,
                                       const QString &fromName,
                                       const QString &text, qint64 ts)
{
    auto *item = new QListWidgetItem{mChatList};
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    item->setData(ChatTextRole, text);
    item->setData(ChatNameRole, fromName);
    item->setData(ChatOwnRole, fromId == mSelfId);
    item->setData(ChatColorRole, RoomsPanel::memberColor(fromId));
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

void RoomChatWindow::showPeerTyping(const QString &fromName)
{
    mTypingLabel->setText(tr("%1 is typing…").arg(fromName));
    mTypingClearTimer->start(2800);
}

void RoomChatWindow::setComposerEnabled(bool enabled)
{
    mChatEdit->setEnabled(enabled);
    mSendButton->setEnabled(enabled);
}

void RoomChatWindow::clearAll()
{
    mChatList->clear();
    mChatEdit->clear();
    mTypingLabel->clear();
}

void RoomChatWindow::onSendClicked()
{
    QString text = mChatEdit->text().trimmed();
    if (text.isEmpty())
        return;
    mChatEdit->clear();
    emit sendRequested(text);
}

void RoomChatWindow::onTextEdited(const QString &text)
{
    Q_UNUSED(text);
    // At most one typing ping per 2 seconds while typing.
    if (!mTypingSendTimer.isValid() || mTypingSendTimer.elapsed() >= 2000) {
        mTypingSendTimer.start();
        emit typing();
    }
}
