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
#include "welcomepage.h"

#include <QCoreApplication>
#include <QDir>
#include <QEasingCurve>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QShowEvent>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

namespace {

// Per-artwork presentation data:
// - anchor: the focal point the cover-crop centres on. For the
//   centre-face artworks it is deliberately shifted left of the face
//   so the crop window moves the face RIGHT of the tile column.
// - faceX: where the face actually sits in the source (fraction of
//   width); decides which side the text zone goes to.
// - accent: the colour the whole page borrows while the art shows.
struct ArtInfo {
    QPointF anchor;
    qreal faceX;
    QColor accent;
};

ArtInfo artInfoFor(const QString &path)
{
    const QString name = QFileInfo{path}.baseName();
    if (name == "welcome-art-01") return {{0.62, 0.35}, 0.62, QColor{0xE0, 0x23, 0x23}}; // generated lightning — brand red
    if (name == "welcome-art-02") return {{0.02, 0.40}, 0.52, QColor{0xFF, 0x2A, 0x2A}}; // Sukuna — blood red
    if (name == "welcome-art-03") return {{0.02, 0.35}, 0.52, QColor{0x57, 0xC7, 0xFF}}; // Gojo — icy blue
    if (name == "welcome-art-04") return {{0.85, 0.45}, 0.85, QColor{0x4D, 0xA3, 0xFF}}; // Sung Jinwoo — hunter blue
    if (name == "welcome-art-05") return {{0.78, 0.30}, 0.78, QColor{0xFF, 0xD2, 0x3F}}; // Zenitsu — lightning yellow
    if (name == "welcome-art-06") return {{0.50, 0.25}, 0.50, QColor{0xFF, 0x7A, 0x18}}; // Naruto x Sasuke — flame orange
    if (name == "welcome-art-07") return {{0.50, 0.45}, 0.50, QColor{0xB9, 0xCC, 0xE0}}; // Itachi moon — pale silver-blue
    if (name == "welcome-art-08") return {{0.78, 0.32}, 0.78, QColor{0xD9, 0x7B, 0x36}}; // Pain — burnt orange
    if (name == "welcome-art-09") return {{0.47, 0.28}, 0.47, QColor{0x6E, 0xC1, 0xFF}}; // boy facing the sky — sky blue
    if (name == "welcome-art-10") return {{0.02, 0.45}, 0.56, QColor{0x35, 0xA7, 0xFF}}; // Luffy Gear 5 — sky blue
    return {{0.5, 0.4}, 0.5, QColor{0xE0, 0x23, 0x23}};
}

QString rgba(const QColor &c, int alpha)
{
    return QString{"rgba(%1,%2,%3,%4)"}
            .arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha);
}

} // namespace

WelcomePage::WelcomePage(QWidget *parent) :
    QWidget{parent}
{
    setAutoFillBackground(false);

    // ---- content column (floats over the artwork) ----------------------
    mContent = new QWidget{this};
    mContent->setAttribute(Qt::WA_TranslucentBackground);
    auto *col = new QVBoxLayout{mContent};
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(10);

    auto *titleRow = new QHBoxLayout;
    titleRow->setSpacing(16);
    mLogoLabel = new QLabel{mContent};
    QPixmap logo = QIcon{":/icons/images/devcpp.ico"}.pixmap(84, 84);
    if (!logo.isNull())
        mLogoLabel->setPixmap(logo);
    mLogoLabel->setFixedSize(84, 84);
    mLogoLabel->setAttribute(Qt::WA_TranslucentBackground);
    titleRow->addWidget(mLogoLabel);

    auto *titleCol = new QVBoxLayout;
    titleCol->setSpacing(2);
    mTitleLabel = new QLabel{mContent};
    mTitleLabel->setAttribute(Qt::WA_TranslucentBackground);
    titleCol->addWidget(mTitleLabel);
    auto *madeByLabel = new QLabel{tr("Made by Abu Sufyan"), mContent};
    madeByLabel->setStyleSheet("font-size: 11pt; color: #dfe3ec;");
    madeByLabel->setAttribute(Qt::WA_TranslucentBackground);
    titleCol->addWidget(madeByLabel);
    mVersionLabel = new QLabel{mContent};
    mVersionLabel->setStyleSheet("font-size: 10pt; color: #aeb4c2;");
    mVersionLabel->setAttribute(Qt::WA_TranslucentBackground);
    titleCol->addWidget(mVersionLabel);
    titleRow->addLayout(titleCol);
    titleRow->addStretch();
    col->addLayout(titleRow);
    mAnimatables << mLogoLabel << mTitleLabel << madeByLabel;

    col->addSpacing(14);

    auto *tiles = new QGridLayout;
    tiles->setSpacing(10);
    auto makeTile = [this](const QString &title, const QString &subtitle) {
        auto *tile = new QPushButton{mContent};
        tile->setText(subtitle.isEmpty() ? title
                                         : title + "\n" + subtitle);
        tile->setMinimumSize(190, 62);
        tile->setCursor(Qt::PointingHandCursor);
        mTiles << tile;
        mAnimatables << tile;
        return tile;
    };
    auto *newDocTile = makeTile(tr("New Document"),
                                tr("Save first, then start coding"));
    auto *openDocTile = makeTile(tr("Open Document"),
                                 tr("Pick an existing file"));
    auto *newProjectTile = makeTile(tr("New Project"),
                                    tr("Start a whole project"));
    auto *roomsTile = makeTile(tr("Rooms"),
                               tr("Code together, live"));
    auto *themeTile = makeTile(tr("Change Theme"),
                               tr("Appearance options"));
    auto *fontTile = makeTile(tr("Change Font"),
                              tr("Editor font options"));
    auto *continueTile = makeTile(tr("Continue Coding"),
                                  tr("Go back to your open files"));
    tiles->addWidget(newDocTile, 0, 0);
    tiles->addWidget(openDocTile, 0, 1);
    tiles->addWidget(newProjectTile, 1, 0);
    tiles->addWidget(roomsTile, 1, 1);
    tiles->addWidget(themeTile, 2, 0);
    tiles->addWidget(fontTile, 2, 1);
    tiles->addWidget(continueTile, 3, 0, 1, 2);
    col->addLayout(tiles);

    col->addSpacing(10);
    mHotkeysLabel = new QLabel{mContent};
    mHotkeysLabel->setStyleSheet("font-size: 10pt; color: #c6cbd7;");
    mHotkeysLabel->setAttribute(Qt::WA_TranslucentBackground);
    mHotkeysLabel->setWordWrap(true);
    col->addWidget(mHotkeysLabel);
    mAnimatables << mHotkeysLabel;
    col->addStretch();

    auto *root = new QHBoxLayout{this};
    root->setContentsMargins(36, 30, 36, 30);
    root->addWidget(mContent, 0);
    root->addStretch(1);
    mContent->setMaximumWidth(560);

    connect(continueTile, &QPushButton::clicked,
            this, &WelcomePage::continueRequested);
    connect(newDocTile, &QPushButton::clicked,
            this, &WelcomePage::newDocumentRequested);
    connect(openDocTile, &QPushButton::clicked,
            this, &WelcomePage::openDocumentRequested);
    connect(newProjectTile, &QPushButton::clicked,
            this, &WelcomePage::newProjectRequested);
    connect(roomsTile, &QPushButton::clicked,
            this, &WelcomePage::roomsRequested);
    connect(themeTile, &QPushButton::clicked,
            this, &WelcomePage::themeRequested);
    connect(fontTile, &QPushButton::clicked,
            this, &WelcomePage::fontRequested);

    loadArtworks();
    applyAccentStyles();

    mRotateTimer = new QTimer{this};
    mRotateTimer->setInterval(12000);
    connect(mRotateTimer, &QTimer::timeout, this, [this]() {
        if (mArtFiles.size() > 1)
            showArtwork(mArtIndex + 1, true);
    });
}

void WelcomePage::setVersionText(const QString &text)
{
    mVersionLabel->setText(text);
}

void WelcomePage::setHotkeysText(const QString &text)
{
    mHotkeysLabel->setText(text);
}

void WelcomePage::setFadeProgress(qreal value)
{
    mFadeProgress = value;
    update();
}

void WelcomePage::setAccentColor(const QColor &color)
{
    mAccent = color;
    applyAccentStyles();
}

void WelcomePage::applyAccentStyles()
{
    const QString accent = mAccent.name();
    const QString light = mAccent.lighter(135).name();
    const QString tileStyle =
            "QPushButton { background: rgba(8,10,16,0.55);"
            " border: 1.5px solid " + accent + "; border-radius: 12px;"
            " color: #ffffff; padding: 9px 12px; text-align: left;"
            " font-size: 11pt; }"
            "QPushButton:hover { background: " + rgba(mAccent, 64) + ";"
            " border: 1.5px solid " + light + "; }"
            "QPushButton:pressed { background: " + rgba(mAccent, 110) + "; }";
    for (QPushButton *tile : mTiles)
        tile->setStyleSheet(tileStyle);
    mTitleLabel->setText(
                "<span style=\"font-size:15pt; color:#ffffff;\">"
                + tr("Welcome to") + "</span><br/>"
                "<span style=\"font-size:29pt; font-weight:bold; color:"
                + accent + ";\">Dev by Sufyan</span>");
}

void WelcomePage::loadArtworks()
{
    mArtFiles.clear();
    // Built-in artworks: welcome-art-01.jpg is the approved generated
    // lightning-swordsman artwork; 02.. are the user's own wallpapers
    // (Sukuna, Gojo, Sung Jinwoo, Zenitsu, Naruto & Sasuke, Itachi,
    // Pain, sky boy, Luffy Gear 5), embedded as resources.
    for (int i = 1; i <= 30; ++i) {
        const QString builtin = QString{":/icons/images/welcome-art-%1.jpg"}
                .arg(i, 2, 10, QLatin1Char{'0'});
        if (QFile::exists(builtin))
            mArtFiles << builtin;
    }
    // Drop-in folders: artworks the user adds later join automatically.
    const QStringList dirs = {
        QCoreApplication::applicationDirPath() + "/welcome-art",
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + "/welcome-art",
    };
    const QStringList filters = {"*.jpg", "*.jpeg", "*.png"};
    for (const QString &dir : dirs) {
        QDir d{dir};
        if (!d.exists())
            continue;
        const QStringList files = d.entryList(filters, QDir::Files, QDir::Name);
        for (const QString &file : files)
            mArtFiles << d.absoluteFilePath(file);
    }
}

QPixmap WelcomePage::coverFor(const QPixmap &source,
                              const QPointF &anchor) const
{
    if (source.isNull())
        return QPixmap{};
    const QSize target = size();
    if (target.width() < 10 || target.height() < 10)
        return QPixmap{};
    QPixmap cover = source.scaled(target, Qt::KeepAspectRatioByExpanding,
                                  Qt::SmoothTransformation);
    const int cx = qBound(0, int((cover.width() - target.width())
                                 * anchor.x()),
                          cover.width() - target.width());
    const int cy = qBound(0, int((cover.height() - target.height())
                                 * anchor.y()),
                          cover.height() - target.height());
    return cover.copy(cx, cy, target.width(), target.height());
}

void WelcomePage::rebuildCoverCache()
{
    mCurrentCover = coverFor(mCurrentSource, mCurrentAnchor);
    if (mFadeProgress < 1.0)
        mPreviousCover = coverFor(mPreviousSource, mPreviousAnchor);
    else
        mPreviousCover = QPixmap{};
}

void WelcomePage::showArtwork(int index, bool fade)
{
    if (mArtFiles.isEmpty())
        return;
    mArtIndex = ((index % mArtFiles.size()) + mArtFiles.size())
            % mArtFiles.size();
    const QString path = mArtFiles.at(mArtIndex);
    const ArtInfo info = artInfoFor(path);

    mPreviousSource = mCurrentSource;
    mPreviousAnchor = mCurrentAnchor;
    mCurrentSource = QPixmap{path};
    mCurrentAnchor = info.anchor;

    // The text zone sits opposite the face: face on the left half
    // pushes the content to the right, and vice versa.
    mTextRight = info.faceX <= 0.35;
    if (auto *root = qobject_cast<QHBoxLayout *>(layout()))
        root->setDirection(mTextRight ? QBoxLayout::RightToLeft
                                      : QBoxLayout::LeftToRight);

    const bool animate = fade && isVisible() && !mPreviousSource.isNull();
    rebuildCoverCache();
    if (!mFadeAnim) {
        mFadeAnim = new QPropertyAnimation{this, "fadeProgress", this};
        mFadeAnim->setDuration(650);
        mFadeAnim->setEasingCurve(QEasingCurve::InOutQuad);
    }
    if (!mAccentAnim) {
        mAccentAnim = new QPropertyAnimation{this, "accentColor", this};
        mAccentAnim->setDuration(650);
        mAccentAnim->setEasingCurve(QEasingCurve::InOutQuad);
    }
    mFadeAnim->stop();
    mAccentAnim->stop();
    if (animate) {
        mFadeProgress = 0.0;
        rebuildCoverCache();
        mFadeAnim->setStartValue(0.0);
        mFadeAnim->setEndValue(1.0);
        mFadeAnim->start();
        mAccentAnim->setStartValue(mAccent);
        mAccentAnim->setEndValue(info.accent);
        mAccentAnim->start();
    } else {
        mFadeProgress = 1.0;
        mPreviousCover = QPixmap{};
        setAccentColor(info.accent);
        update();
    }
}

void WelcomePage::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter{this};
    painter.fillRect(rect(), QColor{0x0d, 0x0f, 0x13});
    if (!mPreviousCover.isNull() && mFadeProgress < 1.0) {
        painter.drawPixmap(0, 0, mPreviousCover);
        painter.setOpacity(mFadeProgress);
    }
    if (!mCurrentCover.isNull())
        painter.drawPixmap(0, 0, mCurrentCover);
    painter.setOpacity(1.0);

    // Soft dark gradient under the text zone only — the character on
    // the other side stays fully lit.
    const bool textRight = mTextRight;
    QLinearGradient gradient;
    if (textRight) {
        gradient.setStart(QPointF{qreal(width()), 0.0});
        gradient.setFinalStop(QPointF{width() * 0.42, 0.0});
    } else {
        gradient.setStart(QPointF{0.0, 0.0});
        gradient.setFinalStop(QPointF{width() * 0.58, 0.0});
    }
    gradient.setColorAt(0.0, QColor{0, 0, 0, 150});
    gradient.setColorAt(1.0, QColor{0, 0, 0, 0});
    painter.fillRect(rect(), gradient);

    // A gentle floor under the hotkey line (bottom of the text zone)
    // so it stays readable over bright artworks like Luffy's sky.
    QLinearGradient floorGrad;
    floorGrad.setStart(QPointF{0.0, height() * 0.60});
    floorGrad.setFinalStop(QPointF{0.0, qreal(height())});
    floorGrad.setColorAt(0.0, QColor{0, 0, 0, 0});
    floorGrad.setColorAt(1.0, QColor{0, 0, 0, 110});
    const QRect floorRect = textRight
            ? QRect{int(width() * 0.42), int(height() * 0.60),
                    int(width() * 0.58), int(height() * 0.40)}
            : QRect{0, int(height() * 0.60),
                    int(width() * 0.58), int(height() * 0.40)};
    painter.fillRect(floorRect, floorGrad);
}

void WelcomePage::startEntranceAnimation()
{
    // Content fades in piece by piece (~70 ms apart).
    int delay = 0;
    for (QWidget *widget : mAnimatables) {
        if (!widget)
            continue;
        auto *effect = new QGraphicsOpacityEffect{widget};
        effect->setOpacity(0.0);
        widget->setGraphicsEffect(effect);
        auto *anim = new QPropertyAnimation{effect, "opacity", widget};
        anim->setDuration(380);
        anim->setStartValue(0.0);
        anim->setEndValue(1.0);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        QTimer::singleShot(delay, anim, [anim]() { anim->start(); });
        connect(anim, &QPropertyAnimation::finished, widget, [widget]() {
            // Hand the widget back to normal rendering once faded in.
            widget->setGraphicsEffect(nullptr);
        });
        delay += 70;
    }
}

void WelcomePage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!mArtFiles.isEmpty()) {
        // Each launch shows the NEXT artwork: read the persisted index,
        // display it, and persist the following one for the next launch.
        QSettings settings;
        int stored = settings.value("welcome/artIndex", 0).toInt();
        showArtwork(stored, false);
        settings.setValue("welcome/artIndex",
                          (mArtIndex + 1) % mArtFiles.size());
    }
    startEntranceAnimation();
    mRotateTimer->start();
}

void WelcomePage::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    mRotateTimer->stop();
}

void WelcomePage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    rebuildCoverCache();
}
