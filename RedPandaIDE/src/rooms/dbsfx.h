// dbsfx.h — Dev by Sufyan v1.6.0: the GTA-style loading screen and
// the ambient per-theme special effects overlay.
//
// Splash: a borderless, full-bleed artwork screen shown while the
// main window is being built. The artwork follows the active theme
// pack (or the classic anime scheme), with the "Dev by Sufyan"
// wordmark, a progress bar, rotating tips and "Made by Abu Sufyan"
// kept in a bottom band so the artwork's faces stay uncovered.
//
// Effects: a mouse-transparent overlay painted over the main window.
// One family per theme pack — Demon Slayer lightning + embers + the
// Rooms snake, Dragon Ball aura + rising sparks + ki flashes, My
// Hero Academia spark bursts + speed lines + colour pops. Strictly
// lightweight (capped particles, one 50 ms timer, pauses while the
// window is minimized or hidden) because the app also runs inside
// Winlator on a 4 GB phone. Activation state is not used for
// pausing: it is unreliable under Winlator and would silently
// disable the effects there.
#ifndef DBSFX_H
#define DBSFX_H

#include "dbstheme.h"

#include <QElapsedTimer>
#include <QPixmap>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>

class QPaintEvent;
class QPainter;
class QTimer;

class DbsSplashScreen : public QWidget
{
    Q_OBJECT
public:
    DbsSplashScreen(const QPixmap &art, const QColor &accent,
                    const QString &themeName, bool brandMode = false);
    // Monotonic progress with a plain stage line ("Loading...").
    void setProgress(int percent, const QString &stageText);
    // Brings the bar to 100 %, fades out and closes. The total show
    // time is padded to feel deliberate but capped snappy (~2.5 s).
    void finishAndClose();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void startOutro();

    QPixmap mArt;
    bool mBrandMode = false;
    QTimer *mBrandTimer = nullptr;
    void paintBrandScene(QPainter &p, const QRectF &full);
    QColor mAccent;
    QString mThemeName;
    QString mStage;
    QStringList mTips;
    int mProgress = 0;
    int mTipIndex = 0;
    bool mOutroStarted = false;
    qreal mOpacity = 1.0;
    QElapsedTimer mClock;
    QTimer *mTipTimer = nullptr;
    QTimer *mOutroTimer = nullptr;
};

// Builds the splash for the given editor colour scheme (used when no
// theme pack is active). Returns nullptr when no art is available.
DbsSplashScreen *dbsCreateSplashScreen(const QString &schemeName);

// Part 7 helper: fades a freshly shown window in over ~160 ms
// (window-opacity only — compositor-cheap, no repaints). No-op when
// animations are disabled in the settings.
void dbsFadeInWindow(QWidget *window, int durationMs = 220);

// Part 7 transition veil: a mouse-transparent layer whose ONLY
// job is the fadeThrough transition (fade to a dark veil, switch
// under cover, fade back). v1.6.1: the Part 6 character effects
// that used to share this widget (signature streaks, particles,
// auras, glows on every surface) were REMOVED ENTIRELY on the
// user's order - nothing of them remains anywhere in the app.
class DbsVeil : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal veilAlpha READ veilAlpha WRITE setVeilAlpha)
public:
    explicit DbsVeil(QWidget *parent);
    qreal veilAlpha() const { return mVeil; }
    void setVeilAlpha(qreal value);
    // Fade in (230 ms), run `midpoint` under the veil, fade back
    // out. Runs the callback at once when animations are off.
    void fadeThrough(std::function<void()> midpoint);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    qreal mVeil = 0;
    bool mVeilBusy = false;
    std::function<void()> mVeilMid;
    class QPropertyAnimation *mVeilAnim = nullptr;
};


// GTA-style wordmark (v1.6.0, Parts 4+8): heavy lettering filled
// with a vertical sunset gradient built from the ACTIVE theme's
// accent (pale -> accent -> deep), a dark outline and a soft drop
// shadow, so it pops over any artwork and re-colours with the
// theme. Paints `text` inside `box`, left-aligned, auto-shrinking
// the font to fit the box width.
void dbsPaintWordmark(QPainter &painter, const QRectF &box,
                      const QString &text, const QColor &accent);

#endif // DBSFX_H
