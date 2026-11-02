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
                    const QString &themeName);
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

class DbsEffectsOverlay : public QWidget
{
    Q_OBJECT
    // Part 7 transition veil (0 = clear, 1 = fully veiled).
    Q_PROPERTY(qreal veilAlpha READ veilAlpha WRITE setVeilAlpha)
public:
    explicit DbsEffectsOverlay(QWidget *parent);
    void setFxKind(DbsFxKind kind);
    void setFxEnabled(bool on);
    // Signature effects are anchored: the Rooms dock's rectangle
    // and the toolbar strip's rectangle, in overlay coordinates.
    // Providers are polled; an empty rect disables that anchor.
    void setRoomsRectProvider(std::function<QRect()> provider)
    {
        mRoomsProvider = std::move(provider);
    }
    void setToolbarRectProvider(std::function<QRect()> provider)
    {
        mToolbarProvider = std::move(provider);
    }
    // Face avoidance (user's hard rule): while set (fullscreen
    // Home), every streak zone keeps out of this rectangle —
    // the current artwork's head/face zone in overlay coords.
    void setAvoidRect(const QRect &r) { mAvoidRect = r; }
    // Fires the toolbar + Rooms signature streaks immediately
    // (theme-apply feedback; also used by screenshot verification).
    void triggerStreaksNow();
    // Part 7: smooth transition — veil fades in (130 ms), the
    // midpoint callback runs under the veil (layout/theme switch),
    // then the veil fades back out (210 ms). Runs the callback
    // immediately when animations are off or a veil is in flight.
    void fadeThrough(std::function<void()> midpoint);
    qreal veilAlpha() const { return mVeil; }
    void setVeilAlpha(qreal value);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    struct Particle {
        QPointF pos;
        QPointF vel;
        qreal life = 0;
        qreal ttl = 1;
        qreal size = 3;
        QColor color;
    };
    // --- Signature-technique streaks (Part 6, final design — the
    // user drew it himself on a screenshot): a PERSISTENT glowing
    // lightning line lives along the TOP EDGE of the window and
    // keeps blinking/flickering (continuous electric shimmer), and
    // a second living streak lives on the Rooms box (Snake
    // Breathing serpent for Demon Slayer, a ki-flame line for
    // Dragon Ball, Full-Cowling wisps for MHA). Occasional bigger
    // surge bolts cross the toolbar strip on top of the ambient
    // line. Particles remain only as faint background seasoning.
    struct Bolt {
        QPointF from;
        QPointF to;
        qint64 born = 0;
        qint64 life = 0;
        QColor core;   // near-white centre
        QColor glow;   // coloured halo
        qreal width = 2.2;
        qreal jag = 14;
        bool branches = true;
    };

    void tick();
    void spawnParticles(qreal dt);
    bool fxRunning() const;
    void fireToolbarStreak(qint64 now);
    qreal envelope(qint64 born, qint64 life, qint64 now) const;
    // Continuous shimmer intensity (0.3..1.0): layered sines plus
    // an occasional bright surge — the line never fully dies.
    qreal flicker(qint64 now, qreal phase, qreal speed) const;
    QVector<QPointF> boltPoints(const QPointF &a, const QPointF &b,
                                qreal jag) const;
    // A rendered lightning tree: main channel + recursive
    // branches + a hot point for the glow halo.
    struct BoltTree {
        QVector<QVector<QPointF>> lines;  // [0] = main channel
        QPointF hot;
    };
    BoltTree makeBoltTree(const QPointF &a, const QPointF &b,
                          qreal jag, qreal maxBranch = 170,
                          qreal dive = 0.38) const;
    void strokeTree(QPainter &p, const BoltTree &tree,
                    const QColor &core, const QColor &glow,
                    qreal width, qreal alphaScale) const;
    void drawBolt(QPainter &p, const Bolt &bolt, qint64 now);
    bool zoneBlocked(const QRectF &zone) const;
    void drawTopLine(QPainter &p, qint64 now);
    void drawRoomsLine(QPainter &p, qint64 now);
    // Zone painters (Part 6 expansion: one signature effect per
    // main character, each living in its anchor zone).
    void drawBottomZone(QPainter &p, qint64 now);
    void drawSideEdges(QPainter &p, qint64 now);
    void drawCorners(QPainter &p, qint64 now);
    // Rotation helpers for shared zones: period ~25 s with a
    // 1.2 s crossfade (DBS_FX_ROT_MS overrides for testing).
    qint64 rotationMs() const;
    qreal slotAlpha(qint64 now, int slot, int count) const;

    DbsFxKind mKind = DbsFxKind::None;
    bool mEnabled = false;
    QTimer *mTimer = nullptr;
    // Part 7 veil state.
    qreal mVeil = 0;
    bool mVeilBusy = false;
    std::function<void()> mVeilMid;
    class QPropertyAnimation *mVeilAnim = nullptr;
    QElapsedTimer mClock;
    qint64 mLastTickMs = 0;
    std::function<QRect()> mRoomsProvider;
    std::function<QRect()> mToolbarProvider;
    QRect mRoomsRect;
    QRect mToolbarRect;
    qint64 mNextRectPollAt = 0;

    QRect mAvoidRect;
    // Top-line bolt trees, regenerated at crackle cadence
    // (~110 ms) instead of every frame — a rendered, held
    // lightning shape reads far better than per-frame noise.
    struct TopLine {
        BoltTree tree;
        qreal scale = 1;
    };
    QVector<TopLine> mTopLines;
    qint64 mTopLinesAt = -100000;
    int mTopLinesW = 0;
    DbsFxKind mTopLinesKind = DbsFxKind::None;
    QRect mTopLinesAvoid;

    QVector<Particle> mParts;  // faint seasoning only
    QVector<Bolt> mToolbarBolts;  // occasional surges
    qint64 mNextToolbarAt = 0;
    // Dragon Ball aura pulse phase (edge seasoning).
    qreal mAuraPhase = 0;
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
