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

#include <QPixmap>
#include <QStringList>
#include <QWidget>

class QLabel;
class QPropertyAnimation;
class QTimer;

// WelcomePage is the Dev by Sufyan start page (v1.3.0), modeled on the
// classic Dev-C++ 6.3 welcome screen: logo, WELCOME heading, version,
// quick-action tiles and a hotkey list — plus a rotating anime artwork
// panel. It is shown over the editor area while no editor is open.
//
// Artwork rotation: the built-in artworks ship in the Qt resources
// (welcome-art-01.jpg .. welcome-art-NN.jpg: the approved generated
// artwork plus the user's own anime wallpapers). Additional artworks
// can be dropped (no rebuild) into a "welcome-art" folder next to the
// executable or in the app data folder; they join the rotation
// automatically. The panel advances to the next artwork on every
// launch and crossfades every ~12 s while visible.
//
// Faces are never cropped or covered: the artwork is always fitted
// WHOLE inside the panel (see artworkFramed); only a blurred,
// darkened copy of the same image fills the leftover space.
class WelcomePage : public QWidget
{
    Q_OBJECT
public:
    explicit WelcomePage(QWidget *parent = nullptr);

    void setVersionText(const QString &text);
    void setHotkeysText(const QString &text);

signals:
    void newDocumentRequested();
    void openDocumentRequested();
    void newProjectRequested();
    void roomsRequested();
    void themeRequested();
    void fontRequested();

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void loadArtworks();
    void showArtwork(int index, bool fade);
    QPixmap artworkFramed(int index) const;
    void startEntranceAnimation();

    QLabel *mArtLabel = nullptr;
    QLabel *mVersionLabel = nullptr;
    QLabel *mHotkeysLabel = nullptr;
    QStringList mArtFiles; // resource paths (":/...") or disk paths
    int mArtIndex = 0;
    QTimer *mRotateTimer = nullptr;
    QPropertyAnimation *mFadeOut = nullptr;
    QPropertyAnimation *mFadeIn = nullptr;
    bool mFading = false;
    QWidgetList mAnimatables;
};

#endif // ROOMS_WELCOMEPAGE_H
