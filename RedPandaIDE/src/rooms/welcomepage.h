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
#ifndef ROOMS_WELCOMEPAGE_H
#define ROOMS_WELCOMEPAGE_H

#include <QColor>
#include <QHash>
#include <QPixmap>
#include <QRectF>
#include <QPointer>
#include <QStringList>
#include <QWidget>

class QLabel;
class QListWidget;
class QPropertyAnimation;
class QVariantAnimation;
class QPushButton;
class QTimer;

// WelcomePage is the Dev by Sufyan start page (v1.3.0, redesigned in
// v1.3.1): the rotating anime artwork fills the WHOLE page as a
// full-bleed background — cover-scaled but anchored on each artwork's
// face focal point so the character stays in frame — and the title,
// glassy action tiles and hotkey line float over it on the side
// opposite the face. A soft dark gradient under the text zone keeps
// the words readable without hiding the character.
//
// Every artwork carries its own accent colour (Sukuna red, Gojo icy
// blue, Zenitsu lightning yellow, ...): the tile borders, the title
// highlight and the hover glows morph to the accent of the artwork
// currently on screen, animated together with the art crossfade.
//
// Artwork rotation: the built-in artworks ship in the Qt resources
// (welcome-art-01.jpg .. welcome-art-NN.jpg: the approved generated
// artwork plus the user's own anime wallpapers). Additional artworks
// can be dropped (no rebuild) into a "welcome-art" folder next to the
// executable or in the app data folder; they join the rotation
// automatically. The panel advances to the next artwork on every
// launch and crossfades every ~12 s while visible.
class WelcomePage : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal fadeProgress READ fadeProgress WRITE setFadeProgress)
    Q_PROPERTY(QColor accentColor READ accentColor WRITE setAccentColor)
public:
    explicit WelcomePage(QWidget *parent = nullptr);

    void setVersionText(const QString &text);
    // v1.4.0: a one-line "update available" notice under the version
    // text (empty text hides it). The text may contain an HTML link.
    void setUpdateNotice(const QString &html);
    void setHotkeysText(const QString &text);
    // v1.3.2: the Home tab carries a Recent Files list (hidden while
    // empty) and the Continue Coding tile only exists when editors do.
    void setRecentFiles(const QStringList &files);
    void setContinueVisible(bool visible);
    // v1.6.0 theme packs: show a pack's curated wallpapers instead
    // of the classic set. A non-empty pinned path locks Home to that
    // one wallpaper; clearThemeArtworks() restores the classic set.
    void setThemeArtworks(const QStringList &paths,
                          const QString &pinnedPath);
    void clearThemeArtworks();
    bool themeArtworksActive() const { return mThemeMode; }

    qreal fadeProgress() const { return mFadeProgress; }
    void setFadeProgress(qreal value);
    QColor accentColor() const { return mAccent; }
    void setAccentColor(const QColor &color);

    // Face/head zone of the current artwork, in widget coords
    // (empty when the artwork has no recorded face zone).
    QRectF currentFaceRect() const;
signals:
    void continueRequested();
    void newDocumentRequested();
    void templatesRequested();
    void openDocumentRequested();
    void newProjectRequested();
    void roomsRequested();
    void themeRequested();
    void fontRequested();
    // v1.6.0: the Customize Home tile was clicked.
    void customizeHomeRequested();
    void recentFileRequested(const QString &filename);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    // v1.6.0 Part 7: tile hover/press micro-animations.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void loadArtworks();
    // v1.6.0: (re)applies the current theme-artwork set right away.
    void refreshThemeArt();
    // Part 7: per-tile hover glow (0 = rest, 1 = fully lit). The
    // glow replaces the old instant :hover swap with a 140 ms fade
    // of the tile's border and background depth.
    QString tileStyleSheet(qreal glow) const;
    void setTileGlow(QPushButton *tile, qreal glow);
    void animateTileGlow(QPushButton *tile, qreal target,
                         int durationMs);
    void showArtwork(int index, bool fade);
    void rebuildCoverCache();
    QPixmap coverFor(const QPixmap &source, const QPointF &anchor) const;
    void applyAccentStyles();
    void startEntranceAnimation();

    QLabel *mLogoLabel = nullptr;
    QLabel *mTitleLabel = nullptr;
    // GTA-style wordmark is painted over this empty band.
    QWidget *mWordmarkBand = nullptr;
    QLabel *mVersionLabel = nullptr;
    QLabel *mUpdateLabel = nullptr;
    QLabel *mHotkeysLabel = nullptr;
    QLabel *mRecentTitle = nullptr;
    QListWidget *mRecentList = nullptr;
    QPushButton *mContinueTile = nullptr;
    QWidget *mContent = nullptr;
    QList<QPushButton *> mTiles;
    // Part 7: current hover glow per tile + running glow animations.
    QHash<QPushButton *, qreal> mTileGlow;
    QHash<QPushButton *, QPointer<QVariantAnimation>> mTileAnims;
    QWidgetList mAnimatables;

    QStringList mArtFiles; // resource paths (":/...") or disk paths
    // v1.6.0: true while a theme pack's artworks replace the
    // classic set (the classic per-launch index is skipped then).
    bool mThemeMode = false;
    int mArtIndex = 0;
    QPixmap mCurrentSource;
    QPixmap mPreviousSource;
    QPixmap mCurrentCover;
    QPixmap mPreviousCover;
    QPointF mCurrentAnchor{0.5, 0.4};
    QRectF mCurrentFaceRect;
    QPointF mPreviousAnchor{0.5, 0.4};
    bool mTextRight = false;
    qreal mFadeProgress = 1.0;
    QColor mAccent{224, 35, 35};
    QTimer *mRotateTimer = nullptr;
    QPropertyAnimation *mFadeAnim = nullptr;
    QPropertyAnimation *mAccentAnim = nullptr;
};

#endif // ROOMS_WELCOMEPAGE_H
