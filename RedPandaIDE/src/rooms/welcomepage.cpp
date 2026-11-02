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

#include <cmath>

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
#include <QListWidget>
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

#include "dbstheme.h"
#include "dbsfx.h"

#include <QEvent>
#include <QVariantAnimation>

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
    // The head/face zone in SOURCE fractions (hair included). The
    // effects overlay keeps every streak out of this box on Home —
    // the user's hard rule: no UI element over a face, ever.
    QRectF faceRect{};
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
    // v1.6.0 theme-pack wallpapers (the user's own folders). Every
    // pick keeps the main face large, sharp and on the side opposite
    // the tile column; the anchor centres the cover-crop on the face
    // (a small anchor pulls a centre face right of the tiles).
    if (name == "theme-ds-01") return {{0.45, 0.30}, 0.80, QColor{0xFF, 0xC2, 0x1C}, QRectF{0.395, 0.03, 0.36, 0.50}}; // Zenitsu portrait — face right
    if (name == "theme-ds-02") return {{0.03, 0.35}, 0.13, QColor{0xFF, 0x7A, 0x3D}, QRectF{0.02, 0.02, 0.34, 0.48}}; // Tanjiro in flames — face left
    if (name == "theme-ds-03") return {{0.85, 0.35}, 0.74, QColor{0xB9, 0xCC, 0xE0}, QRectF{0.30, 0.05, 0.42, 0.55}}; // Tanjiro B/W — face right
    if (name == "theme-ds-04") return {{0.06, 0.30}, 0.20, QColor{0xFF, 0xD2, 0x3F}, QRectF{0.05, 0.05, 0.35, 0.50}}; // Zenitsu lightning — face left
    if (name == "theme-db-01") return {{0.30, 0.30}, 0.67, QColor{0x4D, 0xD8, 0xFF}, QRectF{0.495, 0.045, 0.335, 0.37}}; // SSB Goku & Vegeta — faces right, empty sky left
    if (name == "theme-db-02") return {{0.35, 0.30}, 0.72, QColor{0xB3, 0x88, 0xFF}, QRectF{0.595, 0.0, 0.28, 0.58}}; // Beerus — face right, starfield left
                if (name == "theme-mha-01") return {{0.0, 0.40}, 0.71, QColor{0x35, 0xD0, 0x7F}, QRectF{0.36, 0.10, 0.64, 0.64}}; // Tsuyu pond (leaf included) — face centre-right, open sky/water left
    if (name == "theme-mha-02") return {{0.10, 0.26}, 0.53, QColor{0xFF, 0xC2, 0x4D}}; // All Might poster — face centre, crop pulls it right
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
    mWordmarkBand = new QWidget{mContent};
    mWordmarkBand->setFixedHeight(44);
    mWordmarkBand->setMinimumWidth(350);
    mWordmarkBand->setAttribute(Qt::WA_TranslucentBackground);
    titleCol->addWidget(mWordmarkBand);
    auto *madeByLabel = new QLabel{tr("Made by Abu Sufyan"), mContent};
    madeByLabel->setStyleSheet("font-size: 11pt; color: #dfe3ec;");
    madeByLabel->setAttribute(Qt::WA_TranslucentBackground);
    titleCol->addWidget(madeByLabel);
    mVersionLabel = new QLabel{mContent};
    mVersionLabel->setStyleSheet("font-size: 10pt; color: #aeb4c2;");
    mVersionLabel->setAttribute(Qt::WA_TranslucentBackground);
    titleCol->addWidget(mVersionLabel);
    // v1.4.0: update notice line (hidden until an update is found).
    mUpdateLabel = new QLabel{mContent};
    mUpdateLabel->setStyleSheet("font-size: 10pt; color: #ffd23f;");
    mUpdateLabel->setAttribute(Qt::WA_TranslucentBackground);
    mUpdateLabel->setOpenExternalLinks(true);
    mUpdateLabel->hide();
    titleCol->addWidget(mUpdateLabel);
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
        tile->installEventFilter(this);  // Part 7 hover glow
        mTileGlow.insert(tile, 0.0);
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
    // v1.5.1: program templates — same glass tile as the others, one
    // more row in the same column (the artwork's face side is never
    // covered; the layout just grows downward like it did for tiles).
    auto *templatesTile = makeTile(tr("Program Templates"),
                                   tr("Ready-made programs to start from"));
    auto *continueTile = makeTile(tr("Continue Coding"),
                                  tr("Go back to your open files"));
    mContinueTile = continueTile;
    // v1.6.0: Customize Home — pick which wallpaper of the active
    // theme set Home shows (the Themes menu has the same option).
    auto *customizeTile = makeTile(tr("Customize Home"),
                                   tr("Pick the Home wallpaper"));
    // v1.6.1: stable ids so theme packs can dress the tiles with
    // their App Icons (see setTileIconPack).
    newDocTile->setProperty("dbsTileId", QStringLiteral("new"));
    openDocTile->setProperty("dbsTileId", QStringLiteral("open"));
    newProjectTile->setProperty("dbsTileId", QStringLiteral("project"));
    roomsTile->setProperty("dbsTileId", QStringLiteral("rooms"));
    themeTile->setProperty("dbsTileId", QStringLiteral("theme"));
    fontTile->setProperty("dbsTileId", QStringLiteral("font"));
    templatesTile->setProperty("dbsTileId", QStringLiteral("templates"));
    continueTile->setProperty("dbsTileId", QStringLiteral("continue"));
    customizeTile->setProperty("dbsTileId", QStringLiteral("customize"));
    tiles->addWidget(newDocTile, 0, 0);
    tiles->addWidget(openDocTile, 0, 1);
    tiles->addWidget(newProjectTile, 1, 0);
    tiles->addWidget(roomsTile, 1, 1);
    tiles->addWidget(themeTile, 2, 0);
    tiles->addWidget(fontTile, 2, 1);
    tiles->addWidget(templatesTile, 3, 0, 1, 2);
    tiles->addWidget(continueTile, 4, 0, 1, 2);
    tiles->addWidget(customizeTile, 5, 0, 1, 2);
    col->addLayout(tiles);

    // ---- Recent files (v1.3.2): filled from the visit history ---------
    col->addSpacing(4);
    mRecentTitle = new QLabel{tr("Recent Files"), mContent};
    mRecentTitle->setAttribute(Qt::WA_TranslucentBackground);
    col->addWidget(mRecentTitle);
    mRecentList = new QListWidget{mContent};
    mRecentList->setMaximumHeight(106);
    mRecentList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    mRecentList->setSelectionMode(QAbstractItemView::SingleSelection);
    mRecentList->setCursor(Qt::PointingHandCursor);
    col->addWidget(mRecentList);
    mRecentTitle->setVisible(false);
    mRecentList->setVisible(false);
    mAnimatables << mRecentTitle << mRecentList;
    connect(mRecentList, &QListWidget::itemClicked,
            this, [this](QListWidgetItem *item) {
        if (item)
            emit recentFileRequested(
                        item->data(Qt::UserRole).toString());
    });

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
    connect(templatesTile, &QPushButton::clicked,
            this, &WelcomePage::templatesRequested);
    connect(customizeTile, &QPushButton::clicked,
            this, &WelcomePage::customizeHomeRequested);

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

void WelcomePage::setUpdateNotice(const QString &html)
{
    if (!mUpdateLabel)
        return;
    mUpdateLabel->setText(html);
    mUpdateLabel->setVisible(!html.isEmpty());
}

void WelcomePage::setHotkeysText(const QString &text)
{
    mHotkeysLabel->setText(text);
}

void WelcomePage::setRecentFiles(const QStringList &files)
{
    mRecentList->clear();
    for (const QString &file : files) {
        auto *item = new QListWidgetItem{QFileInfo{file}.fileName(),
                                          mRecentList};
        item->setData(Qt::UserRole, file);
        item->setToolTip(file);
    }
    const bool hasFiles = !files.isEmpty();
    mRecentTitle->setVisible(hasFiles);
    mRecentList->setVisible(hasFiles);
}

void WelcomePage::setContinueVisible(bool visible)
{
    if (mContinueTile)
        mContinueTile->setVisible(visible);
}

void WelcomePage::setThemeArtworks(const QStringList &paths,
                                   const QString &pinnedPath)
{
    if (paths.isEmpty()) {
        clearThemeArtworks();
        return;
    }
    mThemeMode = true;
    if (!pinnedPath.isEmpty() && paths.contains(pinnedPath))
        mArtFiles = QStringList{pinnedPath};
    else
        mArtFiles = paths;
    mArtIndex = 0;
    refreshThemeArt();
}

void WelcomePage::clearThemeArtworks()
{
    if (!mThemeMode)
        return;
    mThemeMode = false;
    loadArtworks();
    mArtIndex = 0;
    if (isVisible() && !mArtFiles.isEmpty())
        showArtwork(0, false);
}

void WelcomePage::refreshThemeArt()
{
    // When the page is hidden, showEvent applies the set; when it is
    // on screen (a theme switch), swap right away.
    if (isVisible() && !mArtFiles.isEmpty())
        showArtwork(0, false);
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
    for (QPushButton *tile : mTiles)
        tile->setStyleSheet(
            tileStyleSheet(mTileGlow.value(tile, 0.0)));
    // Recent-files list borrows the same accent, so it recolours with
    // every artwork exactly like the tiles do.
    if (mRecentList) {
        mRecentList->setStyleSheet(
                "QListWidget { background: rgba(8,10,16,0.45);"
                " border: 1.5px solid " + accent + "; border-radius: 10px;"
                " color: #eef1f7; font-size: 10.5pt; outline: 0; }"
                "QListWidget::item { padding: 4px 10px;"
                " border-radius: 6px; }"
                "QListWidget::item:hover { background: "
                + rgba(mAccent, 64) + "; }"
                "QListWidget::item:selected { background: "
                + rgba(mAccent, 90) + "; }");
    }
    if (mRecentTitle)
        mRecentTitle->setStyleSheet(
                "font-size: 11pt; font-weight: bold; color: " + accent
                + ";");
    // The "Dev by Sufyan" wordmark itself is painted GTA-style
    // (gradient from the live accent) over mWordmarkBand in
    // paintEvent — rich text cannot do gradient lettering.
    mTitleLabel->setText(
                "<span style=\"font-size:15pt; color:#ffffff;\">"
                + tr("Welcome to") + "</span>");
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
    mCurrentFaceRect = info.faceRect;

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
        mFadeAnim->setDuration(900);
        mFadeAnim->setEasingCurve(QEasingCurve::InOutQuad);
    }
    if (!mAccentAnim) {
        mAccentAnim = new QPropertyAnimation{this, "accentColor", this};
        mAccentAnim->setDuration(900);
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
    if (mBrandMode) {
        // Normal theme: the animated brand scene is the whole
        // background; only the wordmark band below is shared.
        paintBrandBackground(painter);
        if (mWordmarkBand) {
            const QRect band{mWordmarkBand->mapTo(this, QPoint{0, 0}),
                             mWordmarkBand->size()};
            dbsPaintWordmark(painter, QRectF{band},
                             QStringLiteral("Dev by Sufyan"), mAccent);
        }
        return;
    }
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

    // GTA-style wordmark over its layout band, in the live accent.
    if (mWordmarkBand) {
        const QRect band{mWordmarkBand->mapTo(this, QPoint{0, 0}),
                         mWordmarkBand->size()};
        dbsPaintWordmark(painter, QRectF{band},
                         QStringLiteral("Dev by Sufyan"), mAccent);
    }
}

void WelcomePage::setTileIconPack(const QHash<QString, QString> &icons)
{
    for (QPushButton *tile : mTiles) {
        const QString path =
            icons.value(tile->property("dbsTileId").toString());
        if (!path.isEmpty()) {
            tile->setIcon(QIcon(path));
            tile->setIconSize(QSize(30, 30));
        } else {
            tile->setIcon(QIcon());
        }
    }
}

void WelcomePage::setBrandMode(bool on)
{
    if (mBrandMode == on) {
        if (on)
            update();
        return;
    }
    mBrandMode = on;
    if (on) {
        // The brand scene has no artwork to take an accent from:
        // use the Normal pack's red directly.
        if (const DbsThemePack *normal =
                dbsThemePackById(QStringLiteral("normal")))
            setAccentColor(normal->accent);
        mCurrentCover = QPixmap();
        mPreviousCover = QPixmap();
        mBrandClock.start();
        if (!mBrandTimer) {
            mBrandTimer = new QTimer(this);
            mBrandTimer->setInterval(50);
            connect(mBrandTimer, &QTimer::timeout, this, [this]() {
                if (mBrandMode && isVisible() && dbsAnimationsEnabled())
                    update();
            });
        }
        mBrandTimer->start();
    } else if (mBrandTimer) {
        mBrandTimer->stop();
    }
    update();
}

void WelcomePage::paintBrandBackground(QPainter &painter)
{
    const QRectF full(rect());
    const qreal t = mBrandClock.isValid()
                        ? mBrandClock.elapsed() / 1000.0
                        : 0.0;
    // Living slate gradient with a slow red sheen drifting across.
    QLinearGradient grad(0, 0, full.width(), full.height());
    grad.setColorAt(0.0, QColor(QStringLiteral("#262B36")));
    grad.setColorAt(0.55, QColor(QStringLiteral("#161A21")));
    grad.setColorAt(1.0, QColor(QStringLiteral("#1F242E")));
    painter.fillRect(full, grad);
    const qreal drift = 0.5 + 0.5 * std::sin(t * 0.35);
    const QPointF glowC(full.width() * (0.60 + 0.12 * drift),
                        full.height() * 0.28);
    QRadialGradient glow(glowC, full.width() * 0.45);
    glow.setColorAt(0.0, QColor(mAccent.red(), mAccent.green(),
                                mAccent.blue(), 36));
    glow.setColorAt(1.0, QColor(mAccent.red(), mAccent.green(),
                                mAccent.blue(), 0));
    painter.fillRect(full, glow);
    // Giant, barely-there </> watermark breathing on the empty side.
    {
        QFont f(QStringLiteral("Inter"));
        f.setWeight(QFont::Black);
        f.setPointSizeF(full.height() * 0.52);
        painter.setFont(f);
        const int a = int(8 + 4 * (0.5 + 0.5 * std::sin(t * 0.5)));
        painter.setPen(QColor(255, 255, 255, a));
        painter.drawText(QRectF(full.width() * 0.44, full.height() * 0.16,
                                full.width() * 0.54, full.height() * 0.68),
                         Qt::AlignCenter, QStringLiteral("</>"));
    }
    // The </> mark above the wordmark, pulsing gently in the accent.
    if (mWordmarkBand) {
        const QRect band{mWordmarkBand->mapTo(this, QPoint{0, 0}),
                         mWordmarkBand->size()};
        QFont f(QStringLiteral("Inter"));
        f.setWeight(QFont::Black);
        f.setPointSizeF(44);
        painter.setFont(f);
        const int a = int(165 + 75 * (0.5 + 0.5 * std::sin(t * 1.9)));
        painter.setPen(QColor(mAccent.red(), mAccent.green(),
                              mAccent.blue(), a));
        painter.drawText(QRectF(band.left(), band.top() - 86, 170, 64),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("</>"));
    }
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
        if (mThemeMode) {
            // Theme-pack set: start at the hero (or the pinned
            // wallpaper) and rotate inside the set only; the
            // persisted per-launch index belongs to the classic set.
            showArtwork(mArtIndex, false);
        } else {
            // Each launch shows the NEXT artwork: read the persisted
            // index, display it, and persist the following one for
            // the next launch.
            QSettings settings;
            int stored = settings.value("welcome/artIndex", 0).toInt();
            showArtwork(stored, false);
            settings.setValue("welcome/artIndex",
                              (mArtIndex + 1) % mArtFiles.size());
        }
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

// ---------------------------------------------------------------------------
// Part 7: tile hover/press micro-animations. The glow (0..1) drives
// the tile's background depth and border brightness; a hover fades
// in over ~140 ms instead of the old instant :hover swap. Only the
// hovered tile's (small) stylesheet is rebuilt per frame.
// ---------------------------------------------------------------------------

QString WelcomePage::tileStyleSheet(qreal glow) const
{
    const int bgAlpha = int(140 + 80 * glow);  // 140..220 of 255
    const QColor border = mAccent.lighter(int(100 + 60 * glow));
    return QStringLiteral(
               "QPushButton { background: %1;"
               " border: 1.5px solid %2; border-radius: 12px;"
               " color: #ffffff; padding: 9px 12px;"
               " text-align: left; font-size: 11pt; }"
               "QPushButton:pressed { background: %3; }")
        .arg(QStringLiteral("rgba(8,10,16,%1)").arg(bgAlpha),
             border.name(), rgba(mAccent, 120));
}

void WelcomePage::setTileGlow(QPushButton *tile, qreal glow)
{
    mTileGlow.insert(tile, glow);
    tile->setStyleSheet(tileStyleSheet(glow));
}

void WelcomePage::animateTileGlow(QPushButton *tile, qreal target,
                                  int durationMs)
{
    if (!dbsAnimationsEnabled()) {
        setTileGlow(tile, target);
        return;
    }
    if (QVariantAnimation *old = mTileAnims.value(tile))
        old->stop();
    auto *anim = new QVariantAnimation(tile);
    anim->setDuration(durationMs);
    anim->setStartValue(mTileGlow.value(tile, 0.0));
    anim->setEndValue(target);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(anim, &QVariantAnimation::valueChanged, this,
            [this, tile](const QVariant &value) {
        setTileGlow(tile, value.toReal());
    });
    mTileAnims.insert(tile, anim);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

bool WelcomePage::eventFilter(QObject *watched, QEvent *event)
{
    auto *tile = qobject_cast<QPushButton *>(watched);
    if (tile && mTiles.contains(tile)) {
        switch (event->type()) {
        case QEvent::Enter:
            animateTileGlow(tile, 1.0, 200);
            break;
        case QEvent::Leave:
            animateTileGlow(tile, 0.0, 280);
            break;
        case QEvent::MouseButtonPress:
            animateTileGlow(tile, 1.0, 80);
            break;
        case QEvent::MouseButtonRelease:
            animateTileGlow(tile, tile->underMouse() ? 1.0 : 0.0,
                            160);
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}
