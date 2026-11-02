// dbsfx.cpp — see dbsfx.h for the design notes.
#include "dbsfx.h"

#include <QApplication>
#include <QGuiApplication>
#include <QPainter>
#include <QPaintEvent>
#include <QPolygonF>
#include <QPropertyAnimation>
#include <QFontMetricsF>
#include <QPainterPath>
#include <QRadialGradient>
#include <QRandomGenerator>
#include <QScreen>
#include <QTimer>

#include <cmath>

namespace {
qreal rnd(qreal lo, qreal hi)
{
    return lo + QRandomGenerator::global()->generateDouble() * (hi - lo);
}
int rndInt(int lo, int hi)
{
    return QRandomGenerator::global()->bounded(lo, hi + 1);
}
} // namespace

// ---------------------------------------------------------------------------
// Splash screen
// ---------------------------------------------------------------------------

DbsSplashScreen::DbsSplashScreen(const QPixmap &art, const QColor &accent,
                                 const QString &themeName)
    : QWidget(nullptr, Qt::SplashScreen | Qt::FramelessWindowHint),
      mArt(art),
      mAccent(accent),
      mThemeName(themeName),
      mStage(tr("Starting Dev by Sufyan")),
      mTips({tr("Tip: press F9 to compile and run your code."),
             tr("Tip: Rooms lets the whole class code together, live."),
             tr("Tip: Fix Lagao explains compiler errors in easy words."),
             tr("Tip: Home, Program Templates, starts common programs "
                "in one click."),
             tr("Tip: the Themes menu holds full theme packs with "
                "wallpapers."),
             tr("Tip: Customize Home picks which wallpaper Home "
                "shows.")}),
      mProgress(8)
{
    setAttribute(Qt::WA_DeleteOnClose, false);
    QScreen *screen = QGuiApplication::primaryScreen();
    QRect avail = screen ? screen->availableGeometry() : QRect(0, 0, 1280, 800);
    int w = qMin(1120, int(avail.width() * 0.86));
    int h = qMin(int(w * 9 / 16), int(avail.height() * 0.86));
    setFixedSize(w, h);
    move(avail.center() - rect().center());
    mClock.start();
    mTipTimer = new QTimer(this);
    mTipTimer->setInterval(1150);
    connect(mTipTimer, &QTimer::timeout, this, [this]() {
        mTipIndex = (mTipIndex + 1) % mTips.size();
        update();
    });
    mTipTimer->start();
}

void DbsSplashScreen::setProgress(int percent, const QString &stageText)
{
    if (percent > mProgress)
        mProgress = qMin(percent, 100);
    if (!stageText.isEmpty())
        mStage = stageText;
    update();
}

void DbsSplashScreen::finishAndClose()
{
    if (mOutroStarted)
        return;
    mOutroStarted = true;
    // Impressive but snappy: pad very fast starts to ~1.5 s, never
    // hold the user longer than ~2.6 s in total.
    qint64 elapsed = mClock.elapsed();
    qint64 wait = 1500 - elapsed;
    if (wait <= 0) {
        startOutro();
    } else {
        QTimer::singleShot(int(wait), this,
                           &DbsSplashScreen::startOutro);
    }
}

void DbsSplashScreen::startOutro()
{
    // Part 7: with animations off the splash closes at once.
    if (!dbsAnimationsEnabled()) {
        close();
        deleteLater();
        return;
    }
    if (mTipTimer)
        mTipTimer->stop();
    mStage = tr("Ready");
    if (!mOutroTimer) {
        mOutroTimer = new QTimer(this);
        mOutroTimer->setInterval(30);
        connect(mOutroTimer, &QTimer::timeout, this, [this]() {
            if (mProgress < 100) {
                mProgress += (100 - mProgress) / 4 + 2;
                if (mProgress > 100)
                    mProgress = 100;
            } else {
                mOpacity -= 0.10;
                if (mOpacity <= 0.0) {
                    mOutroTimer->stop();
                    close();
                    deleteLater();
                    return;
                }
                setWindowOpacity(mOpacity);
            }
            update();
        });
    }
    mOutroTimer->start();
}

void DbsSplashScreen::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF full(rect());
    if (!mArt.isNull()) {
        // Cover-crop, anchored high: faces live in the upper part of
        // every loading artwork and must stay clear of the text band.
        qreal scale = qMax(full.width() / mArt.width(),
                           full.height() / mArt.height());
        qreal sw = full.width() / scale;
        qreal sh = full.height() / scale;
        QRectF src((mArt.width() - sw) / 2.0,
                   (mArt.height() - sh) * 0.22, sw, sh);
        p.drawPixmap(full, mArt, src);
    } else {
        p.fillRect(full, QColor(QStringLiteral("#101018")));
    }
    // Bottom band: gradient only, low enough to never touch a face
    // (it starts at 64% height; every loading artwork's face ends
    // above that line).
    QLinearGradient grad(0, full.height() * 0.64, 0, full.height());
    grad.setColorAt(0.0, QColor(0, 0, 0, 0));
    grad.setColorAt(0.5, QColor(0, 0, 0, 130));
    grad.setColorAt(1.0, QColor(0, 0, 0, 238));
    p.fillRect(QRectF(0, full.height() * 0.64, full.width(),
                      full.height() * 0.36),
               grad);

    const qreal h = full.height();
    const qreal w = full.width();
    // Theme name, small and letterspaced, above the wordmark.
    if (!mThemeName.isEmpty()) {
        QFont f = font();
        f.setPointSize(11);
        f.setBold(true);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 2.5);
        p.setFont(f);
        p.setPen(mAccent);
        p.drawText(QRectF(42, h - 142, w - 84, 20), Qt::AlignLeft,
                   mThemeName.toUpper());
    }
    // Wordmark: GTA-style, gradient from the theme accent.
    dbsPaintWordmark(p, QRectF(40, h - 114, w * 0.62, 48),
                     QStringLiteral("Dev by Sufyan"), mAccent);
    // Accent underline.
    p.fillRect(QRectF(42, h - 64, 64, 4), mAccent);
    // Stage line + credit, bottom-left.
    {
        QFont f = font();
        f.setPointSize(11);
        p.setFont(f);
        p.setPen(QColor(QStringLiteral("#DDDDDD")));
        p.drawText(QRectF(42, h - 54, w * 0.55, 18), Qt::AlignLeft,
                   mStage);
        f.setPointSize(9);
        p.setFont(f);
        p.setPen(QColor(QStringLiteral("#9A9A9A")));
        p.drawText(QRectF(42, h - 33, w * 0.55, 16), Qt::AlignLeft,
                   QStringLiteral("Made by Abu Sufyan"));
    }
    // Percent + rotating tip, bottom-right.
    {
        QFont f = font();
        f.setPointSize(21);
        f.setBold(true);
        p.setFont(f);
        p.setPen(Qt::white);
        p.drawText(QRectF(w - 220, h - 108, 178, 42),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("%1%").arg(mProgress));
        f.setPointSize(10);
        f.setBold(false);
        f.setItalic(true);
        p.setFont(f);
        p.setPen(QColor(QStringLiteral("#BBBBBB")));
        if (!mTips.isEmpty())
            p.drawText(QRectF(w - 462, h - 56, 420, 36),
                       Qt::AlignRight | Qt::AlignTop | Qt::TextWordWrap,
                       mTips.at(mTipIndex));
    }
    // Progress bar across the very bottom.
    p.fillRect(QRectF(0, h - 6, w, 6), QColor(255, 255, 255, 38));
    p.fillRect(QRectF(0, h - 6, w * mProgress / 100.0, 6), mAccent);
}

DbsSplashScreen *dbsCreateSplashScreen(const QString &schemeName)
{
    const DbsThemePack *pack = dbsThemePackById(dbsActivePackId());
    if (!pack)
        pack = dbsPackForScheme(schemeName);
    QString artPath;
    QColor accent(QStringLiteral("#E8452C"));
    QString themeName;
    if (pack) {
        artPath = pack->loadingArt;
        accent = pack->accent;
        themeName = pack->name;
    } else {
        // Classic anime schemes (v1.3.0): reuse their Home art.
        struct Row {
            const char *scheme;
            const char *art;
            const char *accent;
        };
        static const Row rows[] = {
            {"Zenitsu", "welcome-art-05.jpg", "#FFD23F"},
            {"Sukuna", "welcome-art-02.jpg", "#FF2A2A"},
            {"Gojo", "welcome-art-03.jpg", "#57C7FF"},
            {"Naruto x Sasuke", "welcome-art-06.jpg", "#FF7A18"},
            {"Luffy Gear 5", "welcome-art-10.jpg", "#DCEEFF"},
        };
        artPath = QStringLiteral(":/icons/images/welcome-art-01.jpg");
        for (const Row &row : rows) {
            if (schemeName == QLatin1String(row.scheme)) {
                artPath = QStringLiteral(":/icons/images/")
                          + QLatin1String(row.art);
                accent = QColor(QLatin1String(row.accent));
                themeName = schemeName;
                break;
            }
        }
    }
    QPixmap art(artPath);
    if (art.isNull())
        return nullptr;
    return new DbsSplashScreen(art, accent, themeName);
}

void dbsFadeInWindow(QWidget *window, int durationMs)
{
    if (!window)
        return;
    if (!dbsAnimationsEnabled()) {
        window->setWindowOpacity(1.0);
        return;
    }
    auto *anim = new QPropertyAnimation(window, "windowOpacity",
                                        window);
    anim->setDuration(durationMs);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    window->setWindowOpacity(0.0);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void dbsPaintWordmark(QPainter &p, const QRectF &box,
                      const QString &text, const QColor &accent)
{
    QFont font(QStringLiteral("Inter"));
    font.setWeight(QFont::Black);  // ExtraBold face ships bundled
    font.setPointSizeF(box.height() * 0.80);
    QFontMetricsF fm(font);
    const qreal advance = fm.horizontalAdvance(text);
    if (advance > box.width() && advance > 0) {
        font.setPointSizeF(font.pointSizeF() * box.width() / advance);
        fm = QFontMetricsF(font);
    }
    QPainterPath path;
    const qreal baseline = box.bottom() - fm.descent()
                           - box.height() * 0.04;
    path.addText(QPointF(box.left(), baseline), font, text);

    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    // Soft drop shadow: two stacked offset fills.
    QPainterPath shadow = path;
    shadow.translate(0, box.height() * 0.045);
    p.fillPath(shadow, QColor(0, 0, 0, 95));
    shadow.translate(0, box.height() * 0.05);
    p.fillPath(shadow, QColor(0, 0, 0, 55));
    // Dark outline (stroke first; the fill covers its inner half).
    p.strokePath(path,
                 QPen(QColor(14, 9, 18),
                      qMax<qreal>(1.5, box.height() * 0.05),
                      Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    // Sunset gradient from the theme accent: pale -> accent -> deep.
    QLinearGradient grad(box.topLeft(), box.bottomLeft());
    grad.setColorAt(0.0, accent.lighter(178));
    grad.setColorAt(0.42, accent.lighter(120));
    grad.setColorAt(0.72, accent);
    grad.setColorAt(1.0, accent.darker(168));
    p.fillPath(path, grad);
    p.restore();
}

// ---------------------------------------------------------------------------
// Effects overlay
// ---------------------------------------------------------------------------

DbsEffectsOverlay::DbsEffectsOverlay(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
    if (parent)
        resize(parent->size());
    mClock.start();
    mTimer = new QTimer(this);
    mTimer->setInterval(40);
    connect(mTimer, &QTimer::timeout, this, &DbsEffectsOverlay::tick);
}

bool DbsEffectsOverlay::fxRunning() const
{
    return mEnabled && mKind != DbsFxKind::None;
}

void DbsEffectsOverlay::setVeilAlpha(qreal value)
{
    mVeil = value;
    update();
}

void DbsEffectsOverlay::fadeThrough(std::function<void()> midpoint)
{
    if (mVeilBusy || !dbsAnimationsEnabled()) {
        if (midpoint)
            midpoint();
        return;
    }
    show();
    raise();
    mVeilBusy = true;
    mVeilMid = std::move(midpoint);
    if (!mVeilAnim) {
        mVeilAnim = new QPropertyAnimation(this, "veilAlpha", this);
        connect(mVeilAnim, &QPropertyAnimation::finished, this,
                [this]() {
            if (mVeilAnim->direction()
                    == QAbstractAnimation::Forward) {
                // Peak of the veil: switch under cover, then fade
                // back out.
                if (mVeilMid)
                    mVeilMid();
                mVeilMid = nullptr;
                mVeilAnim->setDirection(QAbstractAnimation::Backward);
                mVeilAnim->start();
            } else {
                mVeilBusy = false;
                mVeil = 0;
                update();
            }
        });
    }
    mVeilAnim->stop();
    mVeilAnim->setDirection(QAbstractAnimation::Forward);
    mVeilAnim->setStartValue(0.0);
    mVeilAnim->setEndValue(0.88);
    mVeilAnim->setEasingCurve(QEasingCurve::InOutCubic);
    // One leg is 230 ms; the finished handler flips the direction
    // at the peak, so a full in+out cycle takes ~460 ms.
    mVeilAnim->setDuration(230);
    mVeilAnim->start();
}

void DbsEffectsOverlay::setFxKind(DbsFxKind kind)
{
    show();
    raise();
    if (mKind == kind)
        return;
    mKind = kind;
    mParts.clear();
    mToolbarBolts.clear();
    if (fxRunning()) {
        // A welcome surge shortly after the theme lands; the
        // persistent lines are alive from the first frame anyway.
        mNextToolbarAt = mClock.elapsed() + 900;
        mTimer->start();
    } else {
        mTimer->stop();
    }
    update();
}

void DbsEffectsOverlay::setFxEnabled(bool on)
{
    if (mEnabled == on)
        return;
    mEnabled = on;
    if (fxRunning()) {
        mNextToolbarAt = mClock.elapsed() + 700;
        mTimer->start();
    } else {
        mTimer->stop();
        mParts.clear();
        mToolbarBolts.clear();
    }
    update();
}

void DbsEffectsOverlay::triggerStreaksNow()
{
    if (!fxRunning())
        return;
    fireToolbarStreak(mClock.elapsed());
    update();
}

qreal DbsEffectsOverlay::envelope(qint64 born, qint64 life,
                                  qint64 now) const
{
    if (life <= 0)
        return 0;
    qreal t = qreal(now - born) / qreal(life);
    if (t <= 0 || t >= 1)
        return 0;
    // Fast attack, smooth release.
    return t < 0.14 ? t / 0.14 : 1.0 - (t - 0.14) / 0.86;
}

qreal DbsEffectsOverlay::flicker(qint64 now, qreal phase,
                                 qreal speed) const
{
    const qreal t = now / 1000.0 * speed + phase;
    qreal v = 0.62 + 0.20 * std::sin(t * 2.3)
              + 0.12 * std::sin(t * 5.7 + 1.7)
              + 0.06 * std::sin(t * 13.1 + 0.6) + rnd(-0.05, 0.05);
    // Occasional bright surge (~0.35 s every ~6 s).
    const qreal cyc = std::fmod(now / 1000.0 + phase * 3.0, 6.0);
    if (cyc < 0.35)
        v += 0.30 * (1.0 - cyc / 0.35);
    return qBound(0.30, v, 1.0);
}

qint64 DbsEffectsOverlay::rotationMs() const
{
    static const qint64 ms = [] {
        qint64 v = qEnvironmentVariableIntValue("DBS_FX_ROT_MS");
        return v >= 3000 ? v : 25000;
    }();
    return ms;
}

qreal DbsEffectsOverlay::slotAlpha(qint64 now, int slot,
                                   int count) const
{
    if (count <= 1)
        return 1;
    const qint64 period = rotationMs();
    const qint64 cycle = period * count;
    const qint64 t = now % cycle;
    const int active = int(t / period);
    const qint64 local = t - qint64(active) * period;
    const qint64 fade = 1200;
    if (slot == active)
        return local > period - fade
                   ? 1.0 - qreal(local - (period - fade)) / fade
                   : 1.0;
    if (slot == (active + 1) % count)
        return local > period - fade
                   ? qreal(local - (period - fade)) / fade
                   : 0.0;
    return 0;
}

// Jagged lightning polyline between two points: recursive midpoint
// displacement, the classic thunder-arc shape. Regenerated with
// fresh randomness on every repaint while a bolt lives, which is
// what makes it crackle.
QVector<QPointF> DbsEffectsOverlay::boltPoints(const QPointF &a,
                                               const QPointF &b,
                                               qreal jag) const
{
    QVector<QPointF> pts{a, b};
    qreal amp = jag;
    for (int pass = 0; pass < 5 && pts.size() < 40; pass++) {
        QVector<QPointF> next;
        next.reserve(pts.size() * 2 - 1);
        for (int i = 0; i < pts.size(); i++) {
            next.append(pts.at(i));
            if (i + 1 < pts.size()) {
                QPointF mid = (pts.at(i) + pts.at(i + 1)) / 2;
                QPointF seg = pts.at(i + 1) - pts.at(i);
                qreal len = qMax<qreal>(1.0, std::hypot(seg.x(), seg.y()));
                QPointF perp(-seg.y() / len, seg.x() / len);
                mid += perp * rnd(-amp, amp);
                next.append(mid);
            }
        }
        pts = next;
        amp *= 0.55;
    }
    return pts;
}

void DbsEffectsOverlay::fireToolbarStreak(qint64 now)
{
    if (mToolbarRect.isEmpty())
        return;
    const QRect &r = mToolbarRect;
    const qreal cy = r.center().y();
    Bolt bolt;
    bolt.born = now;
    bolt.from = QPointF(r.left() + 12, cy + rnd(-3, 3));
    bolt.to = QPointF(r.right() - 12, cy + rnd(-3, 3));
    bolt.jag = qMax<qreal>(8.0, r.height() * 0.42);
    switch (mKind) {
    case DbsFxKind::DemonSlayer:
        bolt.core = QColor(255, 248, 214);
        bolt.glow = QColor(255, 194, 28);
        bolt.width = 2.4;
        bolt.life = 460;
        mToolbarBolts.append(bolt);
        break;
    case DbsFxKind::DragonBall:
        bolt.core = QColor(255, 243, 196);
        bolt.glow = QColor(255, 179, 0);
        bolt.width = 2.9;
        bolt.life = 560;
        mToolbarBolts.append(bolt);
        bolt.from.ry() += 9;
        bolt.to.ry() += 9;
        bolt.width = 2.0;
        bolt.born = now + 70;
        mToolbarBolts.append(bolt);
        break;
    case DbsFxKind::Mha: {
        const qreal seg = (r.width() - 24) / 3.0;
        for (int i = 0; i < 3; i++) {
            Bolt b = bolt;
            b.from = QPointF(r.left() + 12 + i * seg, cy + rnd(-4, 4));
            b.to = QPointF(r.left() + 12 + (i + 1) * seg - 6,
                           cy + rnd(-4, 4));
            b.core = QColor(255, 248, 205);
            b.glow = QColor(255, 210, 63);
            b.width = 2.1;
            b.life = 420;
            b.born = now + i * 90;
            mToolbarBolts.append(b);
        }
        break;
    }
    default:
        break;
    }
}

void DbsEffectsOverlay::tick()
{
    if (!fxRunning())
        return;
    QWidget *win = window();
    // Paused while minimized or hidden. Activation state is
    // deliberately NOT part of the gate: it is unreliable in the
    // environments this app runs in (Winlator, WM-less X) and would
    // silently disable the effects there.
    if (!win || win->isMinimized() || !win->isVisible())
        return;
    if (parentWidget() && size() != parentWidget()->size()) {
        resize(parentWidget()->size());
        raise();
    }
    // Startup and tab switches can raise other children of the main
    // window above this overlay after it was created; re-assert the
    // top spot about once a second.
    static int raiseCounter = 0;
    if (++raiseCounter % 25 == 0)
        raise();
    qint64 now = mClock.elapsed();
    qreal dt = mLastTickMs > 0 ? (now - mLastTickMs) / 1000.0 : 0.04;
    mLastTickMs = now;
    if (dt > 0.25)
        dt = 0.25;
    mAuraPhase += dt;

    if (now >= mNextRectPollAt) {
        if (mRoomsProvider)
            mRoomsRect = mRoomsProvider();
        if (mToolbarProvider)
            mToolbarRect = mToolbarProvider();
        mNextRectPollAt = now + 400;
    }

    spawnParticles(dt);
    for (int i = mParts.size() - 1; i >= 0; i--) {
        Particle &pt = mParts[i];
        pt.life += dt;
        if (pt.life >= pt.ttl) {
            mParts.removeAt(i);
            continue;
        }
        pt.pos += pt.vel * dt;
        if (mKind == DbsFxKind::DemonSlayer)
            pt.pos.rx() += std::sin(pt.life * 2.2 + pt.size * 3.0) * 0.35;
    }

    // Occasional bigger surge across the toolbar strip, on top of
    // the persistent top-edge line.
    if (now >= mNextToolbarAt) {
        fireToolbarStreak(now);
        mNextToolbarAt = now + rndInt(14000, 26000);
    }
    for (int i = mToolbarBolts.size() - 1; i >= 0; i--)
        if (now - mToolbarBolts.at(i).born > mToolbarBolts.at(i).life)
            mToolbarBolts.removeAt(i);

    update();
}

// Faint background seasoning ONLY: few, dim, slow — the anchored
// signature lines are the headline.
void DbsEffectsOverlay::spawnParticles(qreal dt)
{
    Q_UNUSED(dt);
    auto spawn = [&](int cap, qreal chance, const QColor &color,
                     qreal vyLo, qreal vyHi, qreal sizeLo,
                     qreal sizeHi) {
        if (mParts.size() < cap && rnd(0, 1) < chance) {
            Particle pt;
            pt.pos = QPointF(rnd(0, width()), height() + 6);
            pt.vel = QPointF(rnd(-4, 4), -rnd(vyLo, vyHi));
            pt.ttl = rnd(4, 8);
            pt.size = rnd(sizeLo, sizeHi);
            pt.color = color;
            mParts.append(pt);
        }
    };
    switch (mKind) {
    case DbsFxKind::DemonSlayer:
        spawn(14, 0.30, QColor(255, 168, 66), 9, 20, 1.6, 3.0);
        break;
    case DbsFxKind::DragonBall:
        spawn(18, 0.45, QColor(255, 196, 64), 16, 34, 1.6, 3.2);
        break;
    case DbsFxKind::Mha:
        spawn(8, 0.20, QColor(140, 255, 190), 8, 16, 1.4, 2.6);
        break;
    default:
        break;
    }
}

bool DbsEffectsOverlay::zoneBlocked(const QRectF &zone) const
{
    if (mAvoidRect.isEmpty())
        return false;
    return QRectF(mAvoidRect).adjusted(-16, -16, 16, 16)
        .intersects(zone);
}

// Builds a lightning tree the way rendered bolts look: a main
// channel (midpoint displacement), 3 branches splitting off the
// channel at descending angles, and one sub-branch per branch.
// The hot point (brightest node, for the halo) is the channel
// node farthest from the a-b line.
DbsEffectsOverlay::BoltTree
DbsEffectsOverlay::makeBoltTree(const QPointF &a, const QPointF &b,
                                qreal jag, qreal maxBranch,
                                qreal dive) const
{
    BoltTree tree;
    const QVector<QPointF> main = boltPoints(a, b, jag);
    tree.lines.append(main);
    const QPointF d = b - a;
    const qreal len = std::hypot(d.x(), d.y());
    qreal bestDev = -1;
    tree.hot = main.value(main.size() / 2);
    for (const QPointF &pt : main) {
        const qreal dev = len > 1
            ? std::abs((pt.x() - a.x()) * d.y()
                       - (pt.y() - a.y()) * d.x()) / len
            : 0;
        if (dev > bestDev) {
            bestDev = dev;
            tree.hot = pt;
        }
    }
    if (main.size() > 8 && len > 60) {
        const qreal dirx = d.x() / len, diry = d.y() / len;
        for (int k = 0; k < 3; k++) {
            const int idx = qBound(2, int(main.size() * (0.24 + 0.24 * k)
                                          + rnd(-1.5, 1.5)),
                                   main.size() - 3);
            const QPointF base = main.at(idx);
            // Branches dive away from the channel, biased along
            // its direction and downward — lightning grammar.
            const qreal bl = qMin(len * rnd(0.16, 0.26), maxBranch);
            const qreal ang = rnd(0.55, 1.05) * (k % 2 ? 1 : -1);
            const qreal ca = std::cos(ang), sa = std::sin(ang);
            QPointF tip(base.x() + (dirx * ca - diry * sa) * bl,
                        base.y() + (dirx * sa + diry * ca) * bl
                            + bl * dive);
            const QVector<QPointF> br = boltPoints(base, tip, jag * 0.55);
            tree.lines.append(br);
            if (br.size() > 5) {
                const QPointF b2 = br.at(br.size() / 2);
                const QPointF tip2 = b2 + QPointF((tip.x() - base.x()) * 0.42,
                                                  (tip.y() - base.y()) * 0.42
                                                      + bl * 0.14);
                tree.lines.append(boltPoints(b2, tip2, jag * 0.35));
            }
        }
    }
    return tree;
}

void DbsEffectsOverlay::strokeTree(QPainter &p, const BoltTree &tree,
                                   const QColor &core,
                                   const QColor &glow, qreal width,
                                   qreal alphaScale) const
{
    if (alphaScale <= 0.01 || tree.lines.isEmpty())
        return;
    auto stroke = [&](const QVector<QPointF> &pts, qreal w,
                      const QColor &c, int alpha) {
        if (pts.size() < 2)
            return;
        QColor col = c;
        col.setAlpha(qBound(0, int(alpha * alphaScale), 255));
        p.setPen(QPen(col, w, Qt::SolidLine, Qt::RoundCap,
                      Qt::RoundJoin));
        p.drawPolyline(pts.constData(), pts.size());
    };
    // Soft halo around the hottest node — the glow that makes a
    // bolt read as rendered light instead of a drawn line.
    {
        QRadialGradient halo(tree.hot, 30);
        QColor hc = glow;
        hc.setAlpha(qBound(0, int(85 * alphaScale), 255));
        halo.setColorAt(0, hc);
        QColor hz = glow;
        hz.setAlpha(0);
        halo.setColorAt(1, hz);
        p.setPen(Qt::NoPen);
        p.setBrush(halo);
        p.drawEllipse(tree.hot, 30, 30);
    }
    for (int i = 0; i < tree.lines.size(); i++) {
        const bool mainLine = i == 0;
        const qreal w = mainLine ? width : width * 0.62;
        stroke(tree.lines.at(i), w * 3.4, glow, mainLine ? 78 : 62);
        stroke(tree.lines.at(i), w * 1.8, glow, mainLine ? 160 : 120);
        stroke(tree.lines.at(i), w * 0.95, core, mainLine ? 250 : 195);
    }
}

void DbsEffectsOverlay::drawBolt(QPainter &p, const Bolt &bolt,
                                 qint64 now)
{
    const qreal env = envelope(bolt.born, bolt.life, now);
    if (env <= 0)
        return;
    if (bolt.branches) {
        strokeTree(p, makeBoltTree(bolt.from, bolt.to, bolt.jag),
                   bolt.core, bolt.glow, bolt.width, env);
        return;
    }
    auto stroke = [&](const QVector<QPointF> &pts, qreal w,
                      const QColor &c, int alpha) {
        if (pts.size() < 2)
            return;
        QColor col = c;
        col.setAlpha(qBound(0, int(alpha * env), 255));
        p.setPen(QPen(col, w, Qt::SolidLine, Qt::RoundCap,
                      Qt::RoundJoin));
        p.drawPolyline(pts.constData(), pts.size());
    };
    const QVector<QPointF> pts = boltPoints(bolt.from, bolt.to, bolt.jag);
    stroke(pts, bolt.width * 3.6, bolt.glow, 80);
    stroke(pts, bolt.width * 1.9, bolt.glow, 165);
    stroke(pts, bolt.width, bolt.core, 255);
}

// The persistent top-edge lightning — exactly where the user
// drew it on his screenshot, upgraded to rendered branching
// bolts that crackle at ~110 ms cadence and shimmer via
// flicker(). While a face-avoid rect is set (fullscreen Home),
// the bolts live only in the empty stretches of the band.
void DbsEffectsOverlay::drawTopLine(QPainter &p, qint64 now)
{
    QColor core, glow;
    switch (mKind) {
    case DbsFxKind::DemonSlayer:  // Zenitsu — Thunder Breathing
        core = QColor(255, 248, 214); glow = QColor(255, 194, 28); break;
    case DbsFxKind::DragonBall:   // Goku — Super-Saiyan lightning
        core = QColor(255, 243, 196); glow = QColor(255, 179, 0); break;
    case DbsFxKind::Mha:          // Denki — electric discharge
        core = QColor(255, 248, 205); glow = QColor(255, 210, 63); break;
    default:
        return;
    }
    const qreal I = flicker(now, 0.0, 1.0);
    const qreal y = 6.5;
    const qreal W = width();

    // Allowed stretches of the top band: everything except the
    // face zone (when the avoid rect reaches into the band).
    QVector<QPair<qreal, qreal>> spans;
    if (!mAvoidRect.isEmpty() && mAvoidRect.top() < 120) {
        const qreal bx0 = mAvoidRect.left() - 16;
        const qreal bx1 = mAvoidRect.right() + 16;
        if (bx0 > 6 + 130)
            spans.append({6, bx0});
        if (bx1 < W - 6 - 130)
            spans.append({bx1, W - 6});
    } else {
        spans.append({6, W - 6});
    }
    if (spans.isEmpty())
        return;

    const bool stale = now - mTopLinesAt > 110 || mTopLinesW != int(W)
        || mTopLinesKind != mKind || mTopLinesAvoid != mAvoidRect;
    if (stale) {
        mTopLines.clear();
        for (const auto &sp : spans) {
            const qreal x0 = sp.first, x1 = sp.second;
            if (mKind == DbsFxKind::DragonBall) {
                mTopLines.append({makeBoltTree(QPointF(x0, y),
                                               QPointF(x1, y), 5.5, 110, 0.12),
                                  1.0});
                mTopLines.append({makeBoltTree(QPointF(x0, y + 8.5),
                                               QPointF(x1, y + 8.5), 4.5, 110, 0.12),
                                  0.55});
            } else if (mKind == DbsFxKind::Mha) {
                const qreal seg = (x1 - x0) / 3.0;
                for (int i = 0; i < 3; i++) {
                    if (seg - 30 < 60)
                        continue;
                    mTopLines.append(
                        {makeBoltTree(QPointF(x0 + i * seg, y),
                                      QPointF(x0 + (i + 1) * seg - 30, y),
                                      6.0, 110, 0.12),
                         1.0});
                }
            } else {
                mTopLines.append({makeBoltTree(QPointF(x0, y),
                                               QPointF(x1, y), 6.5, 110, 0.12),
                                  1.0});
            }
        }
        mTopLinesAt = now;
        mTopLinesW = int(W);
        mTopLinesKind = mKind;
        mTopLinesAvoid = mAvoidRect;
    }
    for (const TopLine &tl : std::as_const(mTopLines))
        strokeTree(p, tl.tree, core, glow, 1.6, I * tl.scale);
}

// The Rooms-box signature line, per theme — persistent and alive.
void DbsEffectsOverlay::drawRoomsLine(QPainter &p, qint64 now)
{
    if (mRoomsRect.isEmpty())
        return;
    const QRect &r = mRoomsRect;
    auto pathStroke = [&](const QPainterPath &path, qreal w,
                          const QColor &c, int alpha) {
        QColor col = c;
        col.setAlpha(qBound(0, alpha, 255));
        p.setPen(QPen(col, w, Qt::SolidLine, Qt::RoundCap,
                      Qt::RoundJoin));
        p.drawPath(path);
    };
    if (mKind == DbsFxKind::DemonSlayer) {
        // Obanai — Snake Breathing: a pale serpentine line that
        // perpetually winds across the panel, an energy pulse
        // running through it.
        const qreal I = 0.62 + 0.38 * std::sin(now * 0.0013 + 1.0);
        const qreal yC = r.top()
                         + (0.42 + 0.15 * std::sin(now * 0.00045))
                               * r.height();
        QPainterPath path;
        QVector<QPointF> pts;
        for (qreal x = r.left() + 6; x <= r.right() - 6; x += 9) {
            const qreal y = yC
                            + std::sin(x * 0.045 + now * 0.0035) * 8
                            + std::sin(x * 0.013 - now * 0.0016) * 5;
            pts.append(QPointF(x, y));
        }
        for (int i = 0; i < pts.size(); i++)
            i == 0 ? path.moveTo(pts.at(i)) : path.lineTo(pts.at(i));
        pathStroke(path, 8.5, QColor(198, 224, 208), int(42 * I));
        pathStroke(path, 3.8, QColor(226, 238, 230), int(122 * I));
        pathStroke(path, 1.7, QColor(248, 252, 248), int(215 * I));
        // Travelling bright pulse.
        const qreal cx = r.left() - 60
                         + std::fmod(now * 0.09, r.width() + 120.0);
        QPainterPath pulse;
        bool started = false;
        for (const QPointF &pt : pts) {
            if (qAbs(pt.x() - cx) > 55)
                continue;
            if (!started) { pulse.moveTo(pt); started = true; }
            else pulse.lineTo(pt);
        }
        if (started)
            pathStroke(pulse, 2.6, QColor(255, 255, 255), int(190 * I));
    } else if (mKind == DbsFxKind::DragonBall) {
        // Goku's ki: a golden flame line licking along the panel's
        // bottom edge, with three pulsing flame tongues.
        const qreal I = flicker(now, 2.0, 1.3);
        const qreal baseY = r.bottom() - 7;
        QPainterPath path;
        for (qreal x = r.left() + 4; x <= r.right() - 4; x += 9) {
            const qreal y = baseY
                            - (5 + 4.5 * std::sin(x * 0.085 + now * 0.011)
                               + 3 * std::sin(x * 0.029 - now * 0.0063));
            x <= r.left() + 5 ? path.moveTo(x, y) : path.lineTo(x, y);
        }
        pathStroke(path, 7.5, QColor(255, 122, 26), int(44 * I));
        pathStroke(path, 3.4, QColor(255, 166, 48), int(128 * I));
        pathStroke(path, 1.8, QColor(255, 216, 112), int(222 * I));
        const qreal fr[3] = {0.22, 0.5, 0.78};
        for (int i = 0; i < 3; i++) {
            const qreal bx = r.left() + fr[i] * r.width();
            const qreal h = (0.20 + 0.09 * (0.5 + 0.5 * std::sin(
                                     now * 0.0016 + i * 2.2)))
                            * r.height();
            QPainterPath tongue;
            for (int sIdx = 0; sIdx <= 6; sIdx++) {
                const qreal f = sIdx / 6.0;
                const qreal y = (r.bottom() - 4) - h * f;
                const qreal x = bx + std::sin(y * 0.05 - now * 0.008
                                              + i * 1.7)
                                         * 5 * f;
                sIdx == 0 ? tongue.moveTo(x, y) : tongue.lineTo(x, y);
            }
            pathStroke(tongue, 8, QColor(255, 122, 26), int(34 * I));
            pathStroke(tongue, 3.6, QColor(255, 176, 64), int(115 * I));
            pathStroke(tongue, 1.6, QColor(255, 220, 130), int(200 * I));
        }
    } else if (mKind == DbsFxKind::Mha) {
        // Deku — Full Cowling: green lightning wisps living on the
        // panel's edges, crackling continuously.
        const qreal I = flicker(now, 4.0, 1.6);
        auto wisp = [&](const QPointF &a, const QPointF &b, qreal sc) {
            const QVector<QPointF> pts = boltPoints(a, b, 7);
            auto stroke = [&](qreal w, const QColor &c, int alpha) {
                QColor col = c;
                col.setAlpha(qBound(0, int(alpha * I * sc), 255));
                p.setPen(QPen(col, w, Qt::SolidLine, Qt::RoundCap,
                              Qt::RoundJoin));
                p.drawPolyline(pts.constData(), pts.size());
            };
            stroke(4.6, QColor(43, 217, 106), 85);
            stroke(1.6, QColor(216, 255, 230), 240);
        };
        wisp(QPointF(r.left() + 8, r.top() + 10),
             QPointF(r.left() + 8, r.top() + r.height() * 0.52), 1.0);
        wisp(QPointF(r.left() + 10, r.top() + 8),
             QPointF(r.left() + r.width() * 0.55, r.top() + 8), 0.9);
        wisp(QPointF(r.left() + r.width() * 0.35, r.bottom() - 8),
             QPointF(r.right() - 10, r.bottom() - 8), 0.65);
    }
}

// Bottom status strip zone: one signature at a time, rotating
// every ~25 s with a crossfade where the zone is shared.
void DbsEffectsOverlay::drawBottomZone(QPainter &p, qint64 now)
{
    const qreal baseY = height() - 7;
    p.save();
    if (!mAvoidRect.isEmpty() && mAvoidRect.bottom() > height() - 60) {
        QRegion clip(rect());
        clip -= QRegion(QRect(mAvoidRect.left() - 16, height() - 34,
                              mAvoidRect.width() + 32, 34));
        p.setClipRegion(clip);
    }
    auto waveStroke = [&](const QPainterPath &path, qreal w,
                          const QColor &c, int alpha) {
        QColor col = c;
        col.setAlpha(qBound(0, alpha, 255));
        p.setPen(QPen(col, w, Qt::SolidLine, Qt::RoundCap,
                      Qt::RoundJoin));
        p.drawPath(path);
    };
    if (mKind == DbsFxKind::DemonSlayer) {
        // Slot 0 — Tanjiro, Water Breathing: a flowing blue stream.
        // Slot 1 — Rengoku, Flame Breathing: a licking fire line.
        for (int slot = 0; slot < 2; slot++) {
            const qreal A = slotAlpha(now, slot, 2);
            if (A <= 0.01)
                continue;
            QPainterPath path;
            for (qreal x = 6; x <= width() - 6; x += 10) {
                qreal y;
                if (slot == 0)
                    y = baseY + std::sin(x * 0.020 + now * 0.0022) * 3.5
                        + std::sin(x * 0.006 - now * 0.0011) * 2.5;
                else
                    y = baseY - 2
                        - (4 + 4 * std::sin(x * 0.080 + now * 0.010)
                           + 2.5 * std::sin(x * 0.027 - now * 0.006));
                x <= 7 ? path.moveTo(x, y) : path.lineTo(x, y);
            }
            if (slot == 0) {
                waveStroke(path, 6.5, QColor(30, 110, 255), int(46 * A));
                waveStroke(path, 3.0, QColor(64, 164, 255), int(118 * A));
                QPainterPath hi = path;
                hi.translate(0, -2.2);
                waveStroke(hi, 1.4, QColor(170, 220, 255), int(175 * A));
            } else {
                waveStroke(path, 7.0, QColor(255, 110, 20), int(46 * A));
                waveStroke(path, 3.2, QColor(255, 152, 40), int(124 * A));
                waveStroke(path, 1.7, QColor(255, 208, 96), int(215 * A));
            }
        }
    } else if (mKind == DbsFxKind::DragonBall) {
        // Beam rotation — slot 0 Goku's Kamehameha (blue), slot 1
        // Vegeta's Galick Ho (purple). Each slot runs a charge ->
        // fire -> fade cycle.
        for (int slot = 0; slot < 2; slot++) {
            const qreal A = slotAlpha(now, slot, 2);
            if (A <= 0.01)
                continue;
            const qreal t = std::fmod(
                (now + (slot == 0 ? 0 : 4000)) / 1000.0, 9.0) / 9.0;
            const QColor glowC = slot == 0 ? QColor(33, 150, 243)
                                           : QColor(156, 39, 176);
            const QColor coreC = slot == 0 ? QColor(223, 244, 255)
                                           : QColor(246, 222, 255);
            const qreal oy = height() - 16;
            // Charge orb at the left end.
            const qreal charge = t < 0.22 ? t / 0.22
                                          : (t > 0.8 ? qMax(0.0, (1 - t) / 0.2) : 1.0);
            const qreal orbR = 3 + 5.5 * charge;
            p.setPen(Qt::NoPen);
            QColor oc = glowC;
            oc.setAlpha(int(70 * A * charge));
            p.setBrush(oc);
            p.drawEllipse(QPointF(26, oy), orbR * 2.0, orbR * 2.0);
            oc = coreC;
            oc.setAlpha(int(200 * A * charge));
            p.setBrush(oc);
            p.drawEllipse(QPointF(26, oy), orbR, orbR);
            // The beam itself.
            if (t > 0.16 && t < 0.86) {
                const qreal fire = t < 0.26 ? (t - 0.16) / 0.10
                                            : (t > 0.72 ? (0.86 - t) / 0.14 : 1.0);
                const qreal I = flicker(now, slot * 2.0, 1.4);
                const qreal xEnd = 30 + (width() * 0.72) * qMin(1.0, fire * 1.6);
                const qreal bh = (3.2 + 1.6 * I) * fire;
                auto beamStroke = [&](qreal w, const QColor &c, int al) {
                    QColor col = c;
                    col.setAlpha(qBound(0, int(al * A * fire), 255));
                    p.setPen(QPen(col, w, Qt::SolidLine, Qt::RoundCap));
                    p.drawLine(QPointF(30, oy), QPointF(xEnd, oy));
                };
                beamStroke(bh * 3.4, glowC, 52);
                beamStroke(bh * 1.9, glowC, 120);
                beamStroke(bh, coreC, 235);
            }
        }
    } else if (mKind == DbsFxKind::Mha) {
        // Iida — engine speed streaks zipping along the strip.
        for (int k = 0; k < 3; k++) {
            const qreal span = width() + 320.0;
            const qreal head = std::fmod(now * (0.11 + 0.03 * k)
                                         + k * 390.0, span)
                               - 160;
            const qreal y = height() - 9 - k * 8;
            const qreal fadeIn = qBound(0.0, head / 120.0, 1.0);
            const qreal fadeOut = qBound(0.0, (width() - head) / 120.0, 1.0);
            const qreal A = qMin(fadeIn, fadeOut);
            if (A <= 0.02)
                continue;
            QColor c(80, 170, 255);
            c.setAlpha(int(60 * A));
            p.setPen(QPen(c, 3.4, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(head - 95, y), QPointF(head, y));
            c.setAlpha(int(190 * A));
            p.setPen(QPen(c, 1.6, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(head - 95, y), QPointF(head, y));
        }
    }
    p.restore();
}

// Side-edge zones (window's left/right edges).
void DbsEffectsOverlay::drawSideEdges(QPainter &p, qint64 now)
{
    // Face avoidance: clip out a side strip whose zone the
    // current artwork's face box reaches into.
    const bool blockL = zoneBlocked(QRectF(0, 20, 48, height() - 40));
    const bool blockR = zoneBlocked(
        QRectF(width() - 48, 20, 48, height() - 40));
    if (blockL && blockR)
        return;
    p.save();
    if (blockL || blockR) {
        QRegion clip(rect());
        if (blockL)
            clip -= QRegion(QRect(0, 0, 48, height()));
        if (blockR)
            clip -= QRegion(QRect(width() - 48, 0, 48, height()));
        p.setClipRegion(clip);
    }
    auto vWave = [&](qreal cx, const QColor &g, const QColor &m,
                     const QColor &c, qreal phase) {
        QPainterPath path;
        for (qreal y = 26; y <= height() - 26; y += 10) {
            const qreal x = cx + std::sin(y * 0.030 + now * 0.0024
                                          + phase) * 6
                            + std::sin(y * 0.011 - now * 0.0013 + phase)
                                  * 4;
            y <= 27 ? path.moveTo(x, y) : path.lineTo(x, y);
        }
        auto st = [&](qreal w, const QColor &cc, int a) {
            QColor col = cc;
            col.setAlpha(a);
            p.setPen(QPen(col, w, Qt::SolidLine, Qt::RoundCap,
                          Qt::RoundJoin));
            p.drawPath(path);
        };
        st(7, g, 42);
        st(3.4, m, 118);
        st(1.5, c, 205);
    };
    if (mKind == DbsFxKind::DemonSlayer) {
        // Left — Muichiro's mist: slow soft wisps drifting down.
        for (int i = 0; i < 4; i++) {
            const qreal y = std::fmod(now * 0.011 * (1 + i * 0.13)
                                      + i * 260.0, height() + 160.0)
                            - 80;
            const qreal x = 16 + 9 * std::sin(now * 0.0007 + i * 1.9);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(208, 216, 222, 30));
            p.drawEllipse(QPointF(x, y), 30 + i * 4.0, 11 + i * 1.5);
            p.setBrush(QColor(226, 232, 236, 22));
            p.drawEllipse(QPointF(x + 7, y - 5), 20 + i * 3.0, 8);
        }
        // Right — rotation: Mitsuri's Love ribbon / Sanemi's wind
        // slashes.
        const qreal aM = slotAlpha(now, 0, 2);
        if (aM > 0.01) {
            p.save();
            p.setOpacity(aM);
            vWave(width() - 12, QColor(255, 120, 180),
                  QColor(255, 160, 200), QColor(255, 226, 240), 0.8);
            p.restore();
        }
        const qreal aS = slotAlpha(now, 1, 2);
        if (aS > 0.01) {
            // Wind slashes flash every ~3.5 s inside his window.
            const qint64 cyc = now % 3500;
            if (cyc < 430) {
                const qreal e = envelope(0, 430, cyc);
                const int cycIdx = int(now / 3500);
                const qreal y0 = 60 + (cycIdx * 173) % qMax(80, height() - 320);
                auto slash = [&](qreal dx, qreal yy, qreal len) {
                    QColor g(120, 255, 170);
                    g.setAlpha(int(85 * e * aS));
                    p.setPen(QPen(g, 4.6, Qt::SolidLine, Qt::RoundCap));
                    p.drawLine(QPointF(width() - 34 - dx, yy),
                               QPointF(width() - 34 - dx - len * 0.42,
                                       yy + len));
                    QColor c(240, 255, 244);
                    c.setAlpha(int(225 * e * aS));
                    p.setPen(QPen(c, 1.7, Qt::SolidLine, Qt::RoundCap));
                    p.drawLine(QPointF(width() - 34 - dx, yy),
                               QPointF(width() - 34 - dx - len * 0.42,
                                       yy + len));
                };
                slash(0, y0, 120);
                slash(30, y0 + 52, 92);
            }
        }
    } else if (mKind == DbsFxKind::DragonBall) {
        // Left — Ultra Instinct silver shimmer rising.
        p.setPen(Qt::NoPen);
        for (int i = 0; i < 7; i++) {
            const qreal y = height() + 10
                            - std::fmod(i * 197.0 + now * (0.020 + 0.004 * i),
                                        height() + 20.0);
            const qreal x = 7 + (i % 3) * 4
                            + 2.5 * std::sin(now * 0.001 + i * 2.0);
            const qreal tw = 0.5 + 0.5 * std::sin(now * 0.004 + i * 2.7);
            QColor c(226, 233, 240);
            c.setAlpha(int(70 + 120 * tw));
            p.setBrush(c);
            const qreal r = 1.3 + (i % 3) * 0.55;
            p.drawEllipse(QPointF(x, y), r, r);
        }
        // Right — Goku Black's rose energy blade, shimmering.
        const qreal I = flicker(now, 1.0, 1.1);
        QPainterPath path;
        for (qreal y = 30; y <= height() - 30; y += 10) {
            const qreal x = width() - 10
                            + std::sin(y * 0.040 + now * 0.003) * 3;
            y <= 31 ? path.moveTo(x, y) : path.lineTo(x, y);
        }
        auto st = [&](qreal w, const QColor &cc, int a) {
            QColor col = cc;
            col.setAlpha(int(a * I));
            p.setPen(QPen(col, w, Qt::SolidLine, Qt::RoundCap,
                          Qt::RoundJoin));
            p.drawPath(path);
        };
        st(6.5, QColor(255, 80, 120), 48);
        st(3.0, QColor(255, 110, 150), 128);
        st(1.4, QColor(255, 215, 225), 208);
    } else if (mKind == DbsFxKind::Mha) {
        // Todoroki owns both edges: frost (left) + fire (right).
        const qreal pulse = 0.6 + 0.4 * std::sin(now * 0.0011);
        QLinearGradient frost(QPointF(0, 0), QPointF(17, 0));
        frost.setColorAt(0, QColor(170, 220, 255, int(30 * pulse)));
        frost.setColorAt(1, QColor(170, 220, 255, 0));
        p.fillRect(QRectF(0, 0, 17, height()), frost);
        p.setPen(Qt::NoPen);
        for (int i = 0; i < 5; i++) {
            const qreal y = 40 + i * (height() - 80) / 4.0
                            + 8 * std::sin(now * 0.0009 + i * 2.3);
            const qreal tw = 0.5 + 0.5 * std::sin(now * 0.003 + i * 3.1);
            QColor c(200, 232, 255);
            c.setAlpha(int(60 + 130 * tw));
            p.setBrush(c);
            const qreal s = 3.2;
            QPolygonF star;
            star << QPointF(9, y - s * 1.7) << QPointF(9 + s * 0.42, y - s * 0.42)
                 << QPointF(9 + s * 1.7, y) << QPointF(9 + s * 0.42, y + s * 0.42)
                 << QPointF(9, y + s * 1.7) << QPointF(9 - s * 0.42, y + s * 0.42)
                 << QPointF(9 - s * 1.7, y) << QPointF(9 - s * 0.42, y - s * 0.42);
            p.drawPolygon(star);
        }
        // Fire side: a vertical flame ribbon.
        QPainterPath path;
        for (qreal y = 26; y <= height() - 26; y += 10) {
            const qreal x = width() - 9
                            + std::sin(y * 0.060 - now * 0.009) * 4
                            + std::sin(y * 0.021 + now * 0.004) * 3;
            y <= 27 ? path.moveTo(x, y) : path.lineTo(x, y);
        }
        auto st = [&](qreal w, const QColor &cc, int a) {
            QColor col = cc;
            col.setAlpha(a);
            p.setPen(QPen(col, w, Qt::SolidLine, Qt::RoundCap,
                          Qt::RoundJoin));
            p.drawPath(path);
        };
        st(7, QColor(255, 110, 20), 40);
        st(3.2, QColor(255, 152, 40), 116);
        st(1.6, QColor(255, 210, 100), 205);
    }
    p.restore();
}

// Corner zones: bottom-left (rotating pair) + bottom-right.
void DbsEffectsOverlay::drawCorners(QPainter &p, qint64 now)
{
    const qreal W = width(), H = height();
    p.save();
    {
        QRegion clip(rect());
        bool any = false;
        if (zoneBlocked(QRectF(0, H - 120, 170, 120))) {
            clip -= QRegion(QRect(0, int(H) - 120, 170, 120));
            any = true;
        }
        if (zoneBlocked(QRectF(W - 170, H - 120, 170, 120))) {
            clip -= QRegion(QRect(int(W) - 170, int(H) - 120, 170, 120));
            any = true;
        }
        if (any)
            p.setClipRegion(clip);
    }
    if (mKind == DbsFxKind::DemonSlayer) {
        // BL slot 0 — Nezuko's pink blood-flames.
        const qreal aN = slotAlpha(now, 0, 2);
        if (aN > 0.01) {
            for (int i = 0; i < 3; i++) {
                const qreal bx = 24 + i * 27;
                const qreal h = 17 + 9 * (0.5 + 0.5 * std::sin(
                                    now * 0.0021 + i * 2.4));
                QPainterPath tongue;
                for (int sIdx = 0; sIdx <= 5; sIdx++) {
                    const qreal f = sIdx / 5.0;
                    const qreal y = (H - 5) - h * f;
                    const qreal x = bx + std::sin(y * 0.09 - now * 0.009
                                                  + i * 1.9)
                                             * 3.6 * f;
                    sIdx == 0 ? tongue.moveTo(x, y) : tongue.lineTo(x, y);
                }
                auto st = [&](qreal w, const QColor &c, int a) {
                    QColor col = c;
                    col.setAlpha(int(a * aN));
                    p.setPen(QPen(col, w, Qt::SolidLine, Qt::RoundCap,
                                  Qt::RoundJoin));
                    p.drawPath(tongue);
                };
                st(7, QColor(255, 90, 140), 40);
                st(3.4, QColor(255, 120, 165), 118);
                st(1.6, QColor(255, 196, 214), 205);
            }
        }
        // BL slot 1 — Shinobu's butterflies, drifting + flapping.
        const qreal aB = slotAlpha(now, 1, 2);
        if (aB > 0.01) {
            for (int i = 0; i < 2; i++) {
                const qreal cx = 58 + 42 * std::sin(now * 0.00021 + i * 2.4);
                const qreal cy = H - 68 + 25 * std::sin(now * 0.00034 + i * 1.7);
                const qreal flap = 0.5 + 0.5 * std::sin(now * 0.020 + i * 1.3);
                const qreal ww = 3.2 + 4.4 * flap;
                p.setPen(Qt::NoPen);
                QColor wc(178, 132, 255);
                wc.setAlpha(int(165 * aB));
                p.setBrush(wc);
                p.drawEllipse(QPointF(cx - ww, cy - 1.5), ww, 3.1);
                p.drawEllipse(QPointF(cx + ww, cy - 1.5), ww, 3.1);
                QColor bc(52, 32, 84);
                bc.setAlpha(int(210 * aB));
                p.setBrush(bc);
                p.drawEllipse(QPointF(cx, cy), 1.5, 3.4);
            }
        }
        // BR — Giyu's calm water ripple: slow expanding rings.
        p.setBrush(Qt::NoBrush);
        for (int i = 0; i < 3; i++) {
            const qreal r = std::fmod(now * 0.014 + i * 22.0, 66.0);
            QColor c(70, 140, 230);
            c.setAlpha(int((1.0 - r / 66.0) * 115));
            p.setPen(QPen(c, 1.6));
            p.drawEllipse(QPointF(W - 70, H - 42), r, r * 0.42);
        }
    } else if (mKind == DbsFxKind::DragonBall) {
        // BR — Beerus's destruction orbs: purple, pulsing, with a
        // crackle when they flare.
        for (int i = 0; i < 2; i++) {
            const QPointF c0 = i == 0 ? QPointF(W - 52, H - 56)
                                      : QPointF(W - 88, H - 40);
            const QPointF c = c0 + QPointF(
                7 * std::sin(now * 0.0009 + i * 2.6),
                6 * std::sin(now * 0.0013 + i * 1.9));
            const qreal pulse = 0.72 + 0.28 * std::sin(now * 0.0022 + i * 2.2);
            const qreal R = (i == 0 ? 12.0 : 8.0) * pulse;
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(168, 85, 247, 56));
            p.drawEllipse(c, R * 1.9, R * 1.9);
            p.setBrush(QColor(216, 140, 255, 148));
            p.drawEllipse(c, R, R);
            p.setBrush(QColor(250, 235, 255, 228));
            p.drawEllipse(c, R * 0.42, R * 0.42);
            if (flicker(now, i * 2.9, 1.2) > 0.86) {
                const QVector<QPointF> pts = boltPoints(
                    c + QPointF(-R, 0), c + QPointF(-R - 15, -7), 3.5);
                QColor cc(226, 170, 255, 190);
                p.setPen(QPen(cc, 1.2, Qt::SolidLine, Qt::RoundCap,
                              Qt::RoundJoin));
                p.drawPolyline(pts.constData(), pts.size());
            }
        }
    } else if (mKind == DbsFxKind::Mha) {
        // BL slot 0 — Bakugo's explosion bursts.
        const qreal aK = slotAlpha(now, 0, 2);
        if (aK > 0.01) {
            const qint64 cyc = now % 4600;
            const QPointF c(48, H - 50);
            if (cyc < 520) {
                const qreal f = cyc / 520.0;
                const qreal e = 1.0 - f;
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(255, 170, 40, int(130 * e * aK)));
                p.drawEllipse(c, 5 + 15 * f, 5 + 15 * f);
                for (int k = 0; k < 7; k++) {
                    const qreal ang = k * 0.898 + 0.4;
                    const QPointF dir(std::cos(ang), std::sin(ang));
                    const QPointF tip = c + dir * (8 + 40 * f);
                    QColor rc(255, 140, 30);
                    rc.setAlpha(int(200 * e * aK));
                    p.setPen(QPen(rc, 2.2, Qt::SolidLine, Qt::RoundCap));
                    p.drawLine(c + dir * 6, tip);
                    p.setPen(Qt::NoPen);
                    p.setBrush(QColor(255, 205, 90, int(220 * e * aK)));
                    p.drawEllipse(tip, 1.9, 1.9);
                }
            } else {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(255, 150, 50, int(70 * aK)));
                p.drawEllipse(c + QPointF(-9, 4), 2.2, 2.2);
                p.drawEllipse(c + QPointF(11, -3), 1.7, 1.7);
            }
        }
        // BL slot 1 — Uraraka's zero-gravity bubbles.
        const qreal aU = slotAlpha(now, 1, 2);
        if (aU > 0.01) {
            for (int i = 0; i < 6; i++) {
                const qreal y = H - 14 - std::fmod(
                    i * 31.0 + now * 0.008 * (1 + 0.15 * i), 96.0);
                const qreal x = 24 + i * 17
                                + 5 * std::sin(now * 0.001 + i * 2.0);
                const qreal r = 3.4 + (i % 3) * 1.4;
                QColor c(255, 170, 200);
                c.setAlpha(int(125 * aU));
                p.setPen(QPen(c, 1.3));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(QPointF(x, y), r, r);
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(255, 235, 244, int(170 * aU)));
                p.drawEllipse(QPointF(x - r * 0.35, y - r * 0.35),
                              1.1, 1.1);
            }
        }
        // BR — All Might's Plus Ultra glow: a golden breathing halo.
        const qreal pulse = 0.55 + 0.45 * std::sin(now * 0.0016);
        QRadialGradient grad(QPointF(W - 64, H - 58), 76);
        grad.setColorAt(0, QColor(255, 205, 64, int(50 * pulse)));
        grad.setColorAt(1, QColor(255, 205, 64, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(grad);
        p.drawEllipse(QPointF(W - 64, H - 58), 76, 76);
        const qreal cyc = std::fmod(now / 1000.0, 5.0);
        if (cyc < 0.4) {
            const qreal a = (1.0 - cyc / 0.4) * 200;
            const QPointF c(W - 96, H - 92);
            const qreal s = 5.5;
            QPolygonF star;
            star << QPointF(c.x(), c.y() - s * 1.8)
                 << QPointF(c.x() + s * 0.45, c.y() - s * 0.45)
                 << QPointF(c.x() + s * 1.8, c.y())
                 << QPointF(c.x() + s * 0.45, c.y() + s * 0.45)
                 << QPointF(c.x(), c.y() + s * 1.8)
                 << QPointF(c.x() - s * 0.45, c.y() + s * 0.45)
                 << QPointF(c.x() - s * 1.8, c.y())
                 << QPointF(c.x() - s * 0.45, c.y() - s * 0.45);
            p.setBrush(QColor(255, 224, 130, int(a)));
            p.drawPolygon(star);
        }
    }
    p.restore();
}

void DbsEffectsOverlay::paintEvent(QPaintEvent *)
{
    const qint64 now = mClock.elapsed();
    if (!fxRunning()) {
        // Only the Part 7 transition veil can be visible then.
        if (mVeil > 0) {
            QPainter veilPainter(this);
            veilPainter.fillRect(
                rect(), QColor(5, 5, 10, int(mVeil * 200)));
        }
        return;
    }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Faint seasoning particles (dim by design).
    for (const Particle &pt : std::as_const(mParts)) {
        qreal fade = 1.0 - pt.life / qMax<qreal>(0.01, pt.ttl);
        QColor c = pt.color;
        c.setAlpha(qBound(0, int(105 * fade), 255));
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawEllipse(pt.pos, pt.size, pt.size);
    }

    // Dragon Ball aura: a soft golden breathing glow at the edges.
    if (mKind == DbsFxKind::DragonBall) {
        const qreal pulse = 0.5 + 0.5 * std::sin(mAuraPhase * 1.7);
        QColor aura(255, 176, 32, int(14 + 18 * pulse));
        const int band = 26;
        QLinearGradient top(QPointF(0, 0), QPointF(0, band));
        top.setColorAt(0, aura);
        top.setColorAt(1, QColor(255, 176, 32, 0));
        if (mAvoidRect.isEmpty() || mAvoidRect.top() > 30)
            p.fillRect(QRectF(0, 0, width(), band), top);
        QLinearGradient bottom(QPointF(0, height()),
                               QPointF(0, height() - band));
        bottom.setColorAt(0, aura);
        bottom.setColorAt(1, QColor(255, 176, 32, 0));
        p.fillRect(QRectF(0, height() - band, width(), band), bottom);
    }

    // Zone system: side edges, corners, bottom strip, Rooms line.
    drawSideEdges(p, now);
    drawCorners(p, now);
    drawBottomZone(p, now);
    if (!mRoomsRect.isEmpty()) {
        p.save();
        p.setClipRect(QRectF(mRoomsRect).adjusted(-6, -16, 6, 6));
        drawRoomsLine(p, now);
        p.restore();
    }

    // Occasional toolbar surges, then the persistent top line.
    for (const Bolt &b : std::as_const(mToolbarBolts))
        drawBolt(p, b, now);
    drawTopLine(p, now);

    // Part 7: the transition veil sits above the effects.
    if (mVeil > 0)
        p.fillRect(rect(), QColor(5, 5, 10, int(mVeil * 200)));
}
