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
                                 const QString &themeName, bool brandMode)
    : QWidget(nullptr, Qt::SplashScreen | Qt::FramelessWindowHint),
      mArt(art),
      mBrandMode(brandMode),
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
    if (mBrandMode) {
        // The brand animation runs on its own repaint clock.
        mBrandTimer = new QTimer(this);
        mBrandTimer->setInterval(30);
        connect(mBrandTimer, &QTimer::timeout, this, [this]() { update(); });
        mBrandTimer->start();
    }
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
    // v1.6.1: Sufyan's call — the loading screen is part of the
    // show: it stays up ~5 s (the outro fade adds ~0.3 s), so the
    // main window lands about 5-6 s after launch even on a fast
    // machine. (v1.6.0 padded to only 1.5 s and, combined with the
    // unpainted first frame, the screen was effectively invisible.)
    qint64 elapsed = mClock.elapsed();
    qint64 wait = 5000 - elapsed;
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
    if (mBrandTimer)
        mBrandTimer->stop();
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
    if (mBrandMode) {
        paintBrandScene(p, full);
        // Shared bottom elements (stage, %, tip, bar) are drawn by
        // the common code below; the brand scene replaces the art,
        // the gradient, the theme name and the wordmark block.
    } else if (!mArt.isNull()) {
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
    if (!mBrandMode && !mThemeName.isEmpty()) {
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
    if (!mBrandMode) {
        dbsPaintWordmark(p, QRectF(40, h - 114, w * 0.62, 48),
                         QStringLiteral("Dev by Sufyan"), mAccent);
        // Accent underline.
        p.fillRect(QRectF(42, h - 64, 64, 4), mAccent);
    }
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


namespace {
// Walks a polyline and returns the sub-path covering `len` units —
// the classic stroke draw-in used by the brand mark.
QPolygonF partialStroke(const QPolygonF &line, qreal len)
{
    QPolygonF out;
    if (line.isEmpty())
        return out;
    out << line.first();
    qreal acc = 0;
    for (int i = 1; i < line.size(); ++i) {
        const qreal seg = QLineF(line[i - 1], line[i]).length();
        if (acc + seg <= len) {
            out << line[i];
            acc += seg;
        } else {
            const qreal f = seg > 0 ? (len - acc) / seg : 0;
            out << line[i - 1] + (line[i] - line[i - 1]) * f;
            return out;
        }
    }
    return out;
}
qreal easeOut(qreal x)
{
    x = qBound(0.0, x, 1.0);
    return 1.0 - (1.0 - x) * (1.0 - x);
}
} // namespace

// Part 10 (v1.6.1): the Normal theme's loading scene — an original
// brand animation designed for this app: the </> mark draws itself
// in, the "Dev by Sufyan" letters assemble one by one, then the
// finished wordmark (theme-red gradient) takes over above the
// usual stage line and progress bar. ~2 s of motion inside the
// splash's normal lifetime.
void DbsSplashScreen::paintBrandScene(QPainter &p, const QRectF &full)
{
    const qreal w = full.width();
    const qreal h = full.height();
    const qint64 t = mClock.elapsed();
    // Slate stage.
    QLinearGradient bg(0, 0, 0, h);
    bg.setColorAt(0.0, QColor(QStringLiteral("#252B37")));
    bg.setColorAt(0.6, QColor(QStringLiteral("#161A21")));
    bg.setColorAt(1.0, QColor(QStringLiteral("#10131A")));
    p.fillRect(full, bg);
    const QPointF cx(w / 2.0, h * 0.30);
    // Soft red halo behind the mark, breathing slowly.
    {
        const qreal pulse = 0.5 + 0.5 * std::sin(t / 620.0);
        QRadialGradient glow(cx, 240);
        glow.setColorAt(0.0, QColor(mAccent.red(), mAccent.green(),
                                    mAccent.blue(),
                                    int(26 + 22 * pulse)));
        glow.setColorAt(1.0, QColor(mAccent.red(), mAccent.green(),
                                    mAccent.blue(), 0));
        p.fillRect(full, glow);
    }
    // The </> mark draws itself in over ~0.95 s.
    {
        const QPolygonF lt = QPolygonF()
            << cx + QPointF(-84, -38) << cx + QPointF(-122, 0)
            << cx + QPointF(-84, 38);
        const QPolygonF slash = QPolygonF()
            << cx + QPointF(26, 44) << cx + QPointF(-4, -44);
        const QPolygonF gt = QPolygonF()
            << cx + QPointF(58, -38) << cx + QPointF(96, 0)
            << cx + QPointF(58, 38);
        qreal total = 0;
        for (const QPolygonF &pl : {lt, slash, gt})
            for (int i = 1; i < pl.size(); ++i)
                total += QLineF(pl[i - 1], pl[i]).length();
        qreal draw = easeOut(t / 950.0) * total;
        p.setRenderHint(QPainter::Antialiasing);
        for (const QPolygonF &pl : {lt, slash, gt}) {
            qreal len = 0;
            for (int i = 1; i < pl.size(); ++i)
                len += QLineF(pl[i - 1], pl[i]).length();
            const QPolygonF part = partialStroke(pl, qMin(draw, len));
            draw -= len;
            if (part.size() < 2)
                continue;
            QPen glowPen(QColor(mAccent.red(), mAccent.green(),
                                mAccent.blue(), 44), 24);
            glowPen.setCapStyle(Qt::RoundCap);
            glowPen.setJoinStyle(Qt::RoundJoin);
            p.setPen(glowPen);
            p.drawPolyline(part);
            QPen core(mAccent.lighter(135), 10);
            core.setCapStyle(Qt::RoundCap);
            core.setJoinStyle(Qt::RoundJoin);
            p.setPen(core);
            p.drawPolyline(part);
            if (draw <= 0)
                break;
        }
    }
    // Letters assemble (staggered rise + fade), then hand over to
    // the painted wordmark.
    const QString text = QStringLiteral("Dev by Sufyan");
    QFont font(QStringLiteral("Inter"));
    font.setWeight(QFont::Black);
    font.setPointSizeF(46);
    QFontMetricsF fm(font);
    const qreal totalW = fm.horizontalAdvance(text);
    const qreal baseY = h * 0.545;
    qreal lettersAlpha = 1.0;
    if (t > 1850)
        lettersAlpha = qBound(0.0, (2250 - t) / 400.0, 1.0);
    if (lettersAlpha > 0.0) {
        qreal x = cx.x() - totalW / 2.0;
        for (int i = 0; i < text.size(); ++i) {
            const QString ch = text.at(i);
            const qreal cw = fm.horizontalAdvance(ch);
            const qreal lt0 = 620 + i * 65;
            const qreal ph = easeOut((t - lt0) / 380.0);
            if (ph > 0.0) {
                p.save();
                p.setOpacity(ph * lettersAlpha);
                p.setFont(font);
                p.setPen(QColor(QStringLiteral("#F2F4F8")));
                p.drawText(QPointF(x, baseY + (1.0 - ph) * 16), ch);
                p.restore();
            }
            x += cw;
        }
    }
    if (t > 1850) {
        p.save();
        p.setOpacity(qBound(0.0, (t - 1850) / 400.0, 1.0));
        dbsPaintWordmark(p, QRectF(cx.x() - totalW / 2.0 - 6, baseY - 52,
                                   totalW + 12, 58),
                         text, mAccent);
        p.restore();
    }
    // Three compile dots under the wordmark once letters land.
    if (t > 1300) {
        for (int i = 0; i < 3; ++i) {
            const qreal ph = 0.5 + 0.5 * std::sin(t / 300.0 - i * 1.1);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(mAccent.red(), mAccent.green(),
                              mAccent.blue(), int(90 + 140 * ph)));
            p.drawEllipse(QPointF(cx.x() - 20 + i * 20, h * 0.615),
                          3.6, 3.6);
        }
    }
    // Bottom darkening so the shared stage/%/tip texts stay readable.
    QLinearGradient grad(0, h * 0.68, 0, h);
    grad.setColorAt(0.0, QColor(0, 0, 0, 0));
    grad.setColorAt(1.0, QColor(0, 0, 0, 190));
    p.fillRect(QRectF(0, h * 0.68, w, h * 0.32), grad);
}

DbsSplashScreen *dbsCreateSplashScreen(const QString &schemeName)
{
    const DbsThemePack *pack = dbsResolvedPack(schemeName);
    QString artPath;
    QColor accent(QStringLiteral("#E8452C"));
    QString themeName;
    if (pack) {
        // The Normal theme has no loading art: its splash runs the
        // animated brand scene instead.
        if (pack->id == QStringLiteral("normal"))
            return new DbsSplashScreen(QPixmap(), pack->accent,
                                       pack->name, true);
        // v1.6.1: the loading screen rotates through the pack's
        // full art set — the hero loading art plus the backdropArt
        // collection (the frame-filling faces that can never be
        // Home backgrounds get their stage here).
        QStringList artPool{pack->loadingArt};
        artPool += pack->backdropArt;
        artPool.removeAll(QString());
        artPath = artPool.at(
            QRandomGenerator::global()->bounded(artPool.size()));
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

DbsVeil::DbsVeil(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
    if (parent)
        resize(parent->size());
}

void DbsVeil::setVeilAlpha(qreal value)
{
    mVeil = value;
    update();
}

void DbsVeil::fadeThrough(std::function<void()> midpoint)
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

void DbsVeil::paintEvent(QPaintEvent *)
{
    // The veil is the only thing this layer ever paints.
    if (mVeil > 0) {
        QPainter veilPainter(this);
        veilPainter.fillRect(rect(),
                             QColor(5, 5, 10, int(mVeil * 200)));
    }
}
