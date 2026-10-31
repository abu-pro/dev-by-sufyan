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
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QShowEvent>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QPushButton *makeTile(const QString &title, const QString &subtitle,
                      QWidget *parent)
{
    auto *tile = new QPushButton{parent};
    tile->setText(subtitle.isEmpty() ? title
                                     : title + "\n" + subtitle);
    tile->setMinimumSize(170, 64);
    tile->setCursor(Qt::PointingHandCursor);
    tile->setStyleSheet(
        "QPushButton { background: rgba(255,255,255,0.06);"
        " border: 1px solid rgba(224,35,35,0.55); border-radius: 10px;"
        " padding: 8px 10px; text-align: left; font-size: 10.5pt; }"
        "QPushButton:hover { background: rgba(224,35,35,0.20);"
        " border: 1px solid #e02323; }"
        "QPushButton:pressed { background: rgba(224,35,35,0.35); }");
    return tile;
}

} // namespace

WelcomePage::WelcomePage(QWidget *parent) :
    QWidget{parent}
{
    setAutoFillBackground(true);
    setStyleSheet("WelcomePage { background: #14161c; color: #f2f2f2; }");

    auto *root = new QHBoxLayout{this};
    root->setContentsMargins(28, 24, 28, 24);
    root->setSpacing(24);

    // ---- left column: branding, tiles, hotkeys -------------------------
    auto *left = new QVBoxLayout;
    left->setSpacing(10);

    auto *logoLabel = new QLabel{this};
    QPixmap logo = QIcon{":/icons/images/devcpp.ico"}.pixmap(96, 96);
    if (!logo.isNull())
        logoLabel->setPixmap(logo);
    logoLabel->setFixedSize(96, 96);
    left->addWidget(logoLabel);
    mAnimatables << logoLabel;

    auto *welcomeLabel = new QLabel{tr("WELCOME"), this};
    welcomeLabel->setStyleSheet(
        "font-size: 30pt; font-weight: bold; letter-spacing: 4px;"
        " color: #ffffff;");
    left->addWidget(welcomeLabel);
    mAnimatables << welcomeLabel;

    auto *nameLabel = new QLabel{tr("Dev by Sufyan"), this};
    nameLabel->setStyleSheet("font-size: 15pt; font-weight: bold; color:#ff5347;");
    left->addWidget(nameLabel);
    mAnimatables << nameLabel;

    auto *madeByLabel = new QLabel{tr("Made by Abu Sufyan"), this};
    madeByLabel->setStyleSheet("font-size: 10.5pt; color: #c9ccd4;");
    left->addWidget(madeByLabel);
    mAnimatables << madeByLabel;

    mVersionLabel = new QLabel{this};
    mVersionLabel->setStyleSheet("font-size: 10pt; color: #8f94a3;");
    left->addWidget(mVersionLabel);

    left->addSpacing(8);

    auto *tiles = new QGridLayout;
    tiles->setSpacing(10);
    auto *newDocTile = makeTile(tr("New Document"),
                                tr("Save first, then start coding"), this);
    auto *openDocTile = makeTile(tr("Open Document"),
                                 tr("Pick an existing file"), this);
    auto *newProjectTile = makeTile(tr("New Project"),
                                    tr("Start a whole project"), this);
    auto *roomsTile = makeTile(tr("Rooms"),
                               tr("Code together, live"), this);
    auto *themeTile = makeTile(tr("Change Theme"),
                               tr("Appearance options"), this);
    auto *fontTile = makeTile(tr("Change Font"),
                              tr("Editor font options"), this);
    tiles->addWidget(newDocTile, 0, 0);
    tiles->addWidget(openDocTile, 0, 1);
    tiles->addWidget(newProjectTile, 1, 0);
    tiles->addWidget(roomsTile, 1, 1);
    tiles->addWidget(themeTile, 2, 0);
    tiles->addWidget(fontTile, 2, 1);
    left->addLayout(tiles);
    mAnimatables << newDocTile << openDocTile << newProjectTile
                 << roomsTile << themeTile << fontTile;

    left->addSpacing(8);
    mHotkeysLabel = new QLabel{this};
    mHotkeysLabel->setStyleSheet("font-size: 10pt; color: #aab0bf;");
    mHotkeysLabel->setWordWrap(true);
    left->addWidget(mHotkeysLabel);
    left->addStretch();

    root->addLayout(left, 5);

    // ---- right column: rotating artwork ---------------------------------
    mArtLabel = new QLabel{this};
    mArtLabel->setMinimumSize(320, 240);
    mArtLabel->setAlignment(Qt::AlignCenter);
    mArtLabel->setStyleSheet(
        "QLabel { border: 1px solid rgba(224,35,35,0.45);"
        " border-radius: 12px; background: #0d0f13; }");
    root->addWidget(mArtLabel, 7);
    mAnimatables << mArtLabel;

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

    mRotateTimer = new QTimer{this};
    mRotateTimer->setInterval(12000);
    connect(mRotateTimer, &QTimer::timeout, this, [this]() {
        if (mArtFiles.size() > 1 && !mFading)
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

namespace {

// Focus point (fractions of width/height) used ONLY for cropping the
// blurred backdrop — the artwork itself is always shown whole. The
// points sit on each character's face so even the backdrop stays
// centred on the subject instead of empty scenery.
QPointF artworkAnchor(const QString &path)
{
    const QString name = QFileInfo{path}.baseName();
    if (name == "welcome-art-01") return {0.62, 0.35}; // lightning swordsman
    if (name == "welcome-art-02") return {0.52, 0.40}; // Sukuna
    if (name == "welcome-art-03") return {0.52, 0.35}; // Gojo
    if (name == "welcome-art-04") return {0.85, 0.45}; // Sung Jinwoo (right)
    if (name == "welcome-art-05") return {0.78, 0.30}; // Zenitsu (upper right)
    if (name == "welcome-art-06") return {0.50, 0.25}; // Naruto & Sasuke
    if (name == "welcome-art-07") return {0.50, 0.45}; // Itachi silhouette
    if (name == "welcome-art-08") return {0.78, 0.32}; // Pain (right)
    if (name == "welcome-art-09") return {0.47, 0.28}; // boy facing the sky
    if (name == "welcome-art-10") return {0.56, 0.45}; // Luffy Gear 5
    return {0.5, 0.4};
}

} // namespace

QPixmap WelcomePage::artworkFramed(int index) const
{
    if (mArtFiles.isEmpty())
        return QPixmap{};
    const QString path = mArtFiles.at(index % mArtFiles.size());
    QPixmap pixmap{path};
    if (pixmap.isNull())
        return pixmap;
    QSize target = mArtLabel->size();
    if (target.width() < 10 || target.height() < 10)
        target = QSize{640, 480};
    const QPointF anchor = artworkAnchor(path);

    // Backdrop: a cover-scaled copy of the same artwork, cropped
    // towards the face anchor, heavily blurred (down/up sampling) and
    // darkened. It only fills the space around the real artwork.
    QPixmap cover = pixmap.scaled(target, Qt::KeepAspectRatioByExpanding,
                                  Qt::SmoothTransformation);
    const int cx = qBound(0, int((cover.width() - target.width())
                                 * anchor.x()),
                          cover.width() - target.width());
    const int cy = qBound(0, int((cover.height() - target.height())
                                 * anchor.y()),
                          cover.height() - target.height());
    cover = cover.copy(cx, cy, target.width(), target.height());
    const QSize smallSize{std::max(1, target.width() / 6),
                          std::max(1, target.height() / 6)};
    QPixmap backdrop = cover.scaled(smallSize, Qt::IgnoreAspectRatio,
                                    Qt::SmoothTransformation)
                             .scaled(target, Qt::IgnoreAspectRatio,
                                     Qt::SmoothTransformation);

    // The artwork itself: fitted WHOLE inside the frame — never
    // cropped, so no face can ever be cut off or hidden.
    const QPixmap fitted = pixmap.scaled(target, Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation);

    QPixmap canvas{target};
    canvas.fill(Qt::transparent);
    QPainter painter{&canvas};
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath clip;
    clip.addRoundedRect(QRectF{QPointF{0, 0}, QSizeF{target}}, 12, 12);
    painter.setClipPath(clip);
    painter.drawPixmap(0, 0, backdrop);
    painter.fillRect(QRect{QPoint{0, 0}, target}, QColor{0, 0, 0, 120});
    painter.drawPixmap((target.width() - fitted.width()) / 2,
                       (target.height() - fitted.height()) / 2, fitted);
    painter.setClipping(false);
    painter.setPen(QColor{224, 35, 35, 115});
    painter.drawRoundedRect(QRectF{0.5, 0.5, target.width() - 1.0,
                                   target.height() - 1.0},
                            12, 12);
    return canvas;
}

void WelcomePage::showArtwork(int index, bool fade)
{
    if (mArtFiles.isEmpty())
        return;
    mArtIndex = ((index % mArtFiles.size()) + mArtFiles.size())
            % mArtFiles.size();
    if (!fade || !isVisible()) {
        // Direct swap: never leave a stale fade opacity behind.
        if (auto *fx = qobject_cast<QGraphicsOpacityEffect *>(
                    mArtLabel->graphicsEffect()))
            fx->setOpacity(1.0);
        mFading = false;
        mArtLabel->setPixmap(artworkFramed(mArtIndex));
        return;
    }
    if (mFading)
        return;
    mFading = true;
    auto *effect = qobject_cast<QGraphicsOpacityEffect *>(
                mArtLabel->graphicsEffect());
    if (!effect) {
        effect = new QGraphicsOpacityEffect{mArtLabel};
        mArtLabel->setGraphicsEffect(effect);
    }
    if (!mFadeOut) {
        mFadeOut = new QPropertyAnimation{effect, "opacity", this};
        mFadeOut->setDuration(180);
        mFadeIn = new QPropertyAnimation{effect, "opacity", this};
        mFadeIn->setDuration(320);
        mFadeIn->setStartValue(0.0);
        mFadeIn->setEndValue(1.0);
        connect(mFadeOut, &QPropertyAnimation::finished, this, [this]() {
            mArtLabel->setPixmap(artworkFramed(mArtIndex));
            mFadeIn->start();
        });
        connect(mFadeIn, &QPropertyAnimation::finished, this, [this]() {
            mFading = false;
        });
    }
    mFadeOut->setStartValue(1.0);
    mFadeOut->setEndValue(0.0);
    mFadeOut->start();
}

void WelcomePage::startEntranceAnimation()
{
    // Tiles, logo and headings fade in one after another (~70 ms apart).
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
    if (!mArtFiles.isEmpty() && !mFading)
        mArtLabel->setPixmap(artworkFramed(mArtIndex));
}
