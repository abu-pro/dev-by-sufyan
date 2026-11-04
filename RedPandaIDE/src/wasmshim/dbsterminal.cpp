/*
 * Dev by Sufyan — WebAssembly port (Part 9). See dbsterminal.h
 * for the design notes. Compiled only under __EMSCRIPTEN__.
 */
#ifdef __EMSCRIPTEN__

#include "dbsterminal.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QShowEvent>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include <algorithm>

namespace {
// Console palette (Windows console on #0C0C0C).
const QColor kText(0xCC, 0xCC, 0xCC);
const QColor kDim(0x8A, 0x8A, 0x8A);
const QColor kError(0xFF, 0x9E, 0x99);   // soft red for stderr
const QColor kCommand(0xFF, 0xFF, 0xFF);

// Part 9 Addendum: the panel deletes itself on close
// (WA_DeleteOnClose), so its last geometry is kept here and the
// next instance reopens where the user left it.
QPoint sSavedPos;
QSize sSavedSize;
bool sHasSavedGeometry = false;

// =====================================================================
// Part 9 Option 2 — the instrumented-probe machinery (file-local).
//
// The functions below mirror a Python reference prototype that was
// validated against the production backend before this build
// (~/workspace/tmp/p9opt2/proto.py): marker wrap compiles, marker
// offsets are exact, the consumption simulation predicts the
// starved/finished state, and the probe's marker-stripped output
// equals the real program output when the committed input covers
// every read.
// =====================================================================

// Sentinel stdin tail appended to every probe run: enough varied
// values to carry execution past the point where the committed
// input runs out, whatever the program reads (numbers first, so
// numeric extractions succeed; a spread of small integers so menu
// loops tend to find their exit choice quickly instead of running
// into the backend time limit).
QString ixSentinelTail()
{
    static const QString tail = [] {
        QString t;
        for (int i = 0; i < 30; ++i)
            t += QStringLiteral(
                "1 2 3 4 5 6 7 8 9 10 0 -1 42 100 1000 3.5 987654321 hello A\n");
        return t;
    }();
    return tail;
}

// Blank out comments and string/char literal contents (offsets and
// newlines preserved), so read-pattern searches only see real code.
QString ixMaskSource(const QString &code)
{
    QString out = code;
    const int n = code.size();
    enum { Normal, Line, Block, Str, Chr, Raw } state = Normal;
    QString rawDelim;
    auto blank = [&](int i) { if (out[i] != QLatin1Char('\n')) out[i] = QLatin1Char(' '); };
    int i = 0;
    while (i < n) {
        const QChar c = code[i];
        if (state == Normal) {
            if (c == QLatin1Char('/') && i + 1 < n && code[i + 1] == QLatin1Char('/')) {
                out[i] = QLatin1Char(' '); out[i + 1] = QLatin1Char(' ');
                i += 2; state = Line; continue;
            }
            if (c == QLatin1Char('/') && i + 1 < n && code[i + 1] == QLatin1Char('*')) {
                out[i] = QLatin1Char(' '); out[i + 1] = QLatin1Char(' ');
                i += 2; state = Block; continue;
            }
            if (c == QLatin1Char('R') && i + 1 < n && code[i + 1] == QLatin1Char('"')) {
                const int j = code.indexOf(QLatin1Char('('), i + 2);
                if (j > i + 2 && j - (i + 2) <= 16) {
                    rawDelim = QLatin1Char(')') + code.mid(i + 2, j - (i + 2)) + QLatin1Char('"');
                    for (int k = i; k <= j; ++k) out[k] = QLatin1Char(' ');
                    i = j + 1; state = Raw; continue;
                }
            }
            if (c == QLatin1Char('"')) { out[i] = QLatin1Char(' '); ++i; state = Str; continue; }
            if (c == QLatin1Char('\'')) { out[i] = QLatin1Char(' '); ++i; state = Chr; continue; }
            ++i; continue;
        }
        if (state == Line) {
            if (c == QLatin1Char('\n')) state = Normal; else out[i] = QLatin1Char(' ');
            ++i; continue;
        }
        if (state == Block) {
            if (c == QLatin1Char('*') && i + 1 < n && code[i + 1] == QLatin1Char('/')) {
                out[i] = QLatin1Char(' '); out[i + 1] = QLatin1Char(' ');
                i += 2; state = Normal; continue;
            }
            blank(i); ++i; continue;
        }
        if (state == Raw) {
            if (code.mid(i, rawDelim.size()) == rawDelim) {
                for (int k = i; k < i + rawDelim.size(); ++k) out[k] = QLatin1Char(' ');
                i += rawDelim.size(); state = Normal; continue;
            }
            blank(i); ++i; continue;
        }
        // Str / Chr
        const QChar q = state == Str ? QLatin1Char('"') : QLatin1Char('\'');
        if (c == QLatin1Char('\\') && i + 1 < n) {
            out[i] = QLatin1Char(' ');
            if (code[i + 1] != QLatin1Char('\n')) out[i + 1] = QLatin1Char(' ');
            i += 2; continue;
        }
        if (c == q) { out[i] = QLatin1Char(' '); ++i; state = Normal; continue; }
        blank(i); ++i; continue;
    }
    return out;
}

int ixMatchParen(const QString &masked, int openIdx)
{
    int depth = 0;
    for (int i = openIdx; i < masked.size(); ++i) {
        const QChar c = masked[i];
        if (c == QLatin1Char('(')) ++depth;
        else if (c == QLatin1Char(')')) {
            if (--depth == 0) return i;
        }
    }
    return -1;
}

// End (exclusive) of a `cin >> …` chain expression starting at
// `start` in the masked source.
int ixChainExtent(const QString &masked, int start)
{
    int dp = 0, db = 0, dk = 0;
    int i = start;
    const int n = masked.size();
    while (i < n) {
        const QChar c = masked[i];
        if (c == QLatin1Char('(')) ++dp;
        else if (c == QLatin1Char(')')) {
            if (dp == 0 && db == 0 && dk == 0) break;
            --dp;
        } else if (c == QLatin1Char('[')) ++db;
        else if (c == QLatin1Char(']')) {
            if (dp == 0 && db == 0 && dk == 0) break;
            --db;
        } else if (c == QLatin1Char('{')) ++dk;
        else if (c == QLatin1Char('}')) {
            if (dp == 0 && db == 0 && dk == 0) break;
            --dk;
        } else if (dp == 0 && db == 0 && dk == 0) {
            if (c == QLatin1Char(';') || c == QLatin1Char(',')
                    || c == QLatin1Char('?') || c == QLatin1Char(':'))
                break;
            if (masked.mid(i, 2) == QStringLiteral("&&")
                    || masked.mid(i, 2) == QStringLiteral("||")
                    || masked.mid(i, 2) == QStringLiteral("<<"))
                break;
        }
        ++i;
    }
    return i;
}

int ixCountTopShifts(const QString &seg)
{
    int cnt = 0, dp = 0, db = 0, dk = 0;
    for (int i = 0; i < seg.size(); ++i) {
        const QChar c = seg[i];
        if (c == QLatin1Char('(')) ++dp;
        else if (c == QLatin1Char(')')) --dp;
        else if (c == QLatin1Char('[')) ++db;
        else if (c == QLatin1Char(']')) --db;
        else if (c == QLatin1Char('{')) ++dk;
        else if (c == QLatin1Char('}')) --dk;
        else if (c == QLatin1Char('>') && dp == 0 && db == 0 && dk == 0
                 && seg.mid(i, 2) == QStringLiteral(">>")) {
            ++cnt; ++i;
        }
    }
    return cnt;
}

struct IxSpan { int start; int end; QVector<DbsTermReadOp> ops; };

// Find every input-read expression in the source. Spans refer to
// the original source (the mask has identical offsets).
QVector<IxSpan> ixFindSites(const QString &masked, const QString &original)
{
    QVector<IxSpan> spans;
    auto overlaps = [&](int s, int e) {
        for (const IxSpan &sp : spans)
            if (s < sp.end && sp.start < e) return true;
        return false;
    };
    auto addSpan = [&](int s, int e, QVector<DbsTermReadOp> ops) {
        if (s >= 0 && e > s && !overlaps(s, e))
            spans.push_back({s, e, ops});
    };
    static const QString kCin = QStringLiteral("(?:std\\s*::\\s*)?cin");
    // getline(cin, …)
    {
        QRegularExpression re(QStringLiteral("\\b(?:std\\s*::\\s*)?getline\\s*\\(\\s*(?:::)?\\s*") + kCin + QStringLiteral("\\b"));
        auto it = re.globalMatch(masked);
        while (it.hasNext()) {
            const auto m = it.next();
            const int openIdx = masked.indexOf(QLatin1Char('('), m.capturedStart());
            const int close = openIdx >= 0 ? ixMatchParen(masked, openIdx) : -1;
            if (close > 0)
                addSpan(m.capturedStart(), close + 1, {DbsTermReadOp{DbsTermReadOp::Line}});
        }
    }
    // cin.getline / cin.get / cin.ignore / cin.read
    {
        QRegularExpression re(QStringLiteral("\\b") + kCin + QStringLiteral("\\s*\\.\\s*(getline|get|ignore|read)\\s*\\("));
        auto it = re.globalMatch(masked);
        while (it.hasNext()) {
            const auto m = it.next();
            const int openIdx = masked.indexOf(QLatin1Char('('), m.capturedStart());
            const int close = openIdx >= 0 ? ixMatchParen(masked, openIdx) : -1;
            if (close < 0) continue;
            const QString meth = m.captured(1);
            DbsTermReadOp::Kind kind = DbsTermReadOp::Char;
            if (meth == QStringLiteral("getline") || meth == QStringLiteral("read"))
                kind = DbsTermReadOp::Line;
            else if (meth == QStringLiteral("ignore"))
                kind = DbsTermReadOp::Skip;
            else if (meth == QStringLiteral("get")
                     && masked.mid(openIdx + 1, close - openIdx - 1).contains(QLatin1Char(',')))
                kind = DbsTermReadOp::Line; // buffer form: get(buf, n)
            addSpan(m.capturedStart(), close + 1, {DbsTermReadOp{kind}});
        }
    }
    // scanf / fscanf(stdin, …): ops derived from the format string
    // (taken from the ORIGINAL source; the mask has it blanked).
    {
        QRegularExpression re(QStringLiteral("\\b(?:std\\s*::\\s*)?(fscanf|scanf)\\s*\\("));
        auto it = re.globalMatch(masked);
        while (it.hasNext()) {
            const auto m = it.next();
            const int openIdx = masked.indexOf(QLatin1Char('('), m.capturedStart());
            const int close = openIdx >= 0 ? ixMatchParen(masked, openIdx) : -1;
            if (close < 0) continue;
            if (m.captured(1) == QStringLiteral("fscanf")) {
                const QString inner = original.mid(openIdx + 1, close - openIdx - 1).trimmed();
                if (!inner.startsWith(QStringLiteral("stdin"))) continue;
            }
            const QString seg = original.mid(m.capturedStart(), close + 1 - m.capturedStart());
            QString fmt;
            {
                QRegularExpression fre(QStringLiteral("\"((?:[^\"\\\\]|\\\\.)*)\""));
                const auto fm = fre.match(seg);
                if (fm.hasMatch()) fmt = fm.captured(1);
            }
            QVector<DbsTermReadOp> ops;
            for (int i = 0; i < fmt.size(); ++i) {
                if (fmt[i] != QLatin1Char('%')) continue;
                if (i + 1 < fmt.size() && fmt[i + 1] == QLatin1Char('%')) { ++i; continue; }
                int j = i + 1;
                while (j < fmt.size()
                       && !QStringLiteral("diouxXeEfFgGaAcspn[").contains(fmt[j]))
                    ++j;
                if (j < fmt.size()) {
                    const QChar t = fmt[j];
                    ops.push_back(DbsTermReadOp{
                        t == QLatin1Char('c') ? DbsTermReadOp::Char
                        : t == QLatin1Char('[') ? DbsTermReadOp::Line
                        : DbsTermReadOp::Token});
                    i = j;
                }
            }
            if (ops.isEmpty()) ops.push_back(DbsTermReadOp{DbsTermReadOp::Token});
            addSpan(m.capturedStart(), close + 1, ops);
        }
    }
    // cin >> … chains
    {
        QRegularExpression re(QStringLiteral("\\b") + kCin + QStringLiteral("\\s*>>"));
        auto it = re.globalMatch(masked);
        while (it.hasNext()) {
            const auto m = it.next();
            if (overlaps(m.capturedStart(), m.capturedStart() + 3)) continue;
            const int end = ixChainExtent(masked, m.capturedStart());
            int nops = ixCountTopShifts(masked.mid(m.capturedStart(), end - m.capturedStart()));
            if (nops == 0) nops = 1;
            QVector<DbsTermReadOp> ops;
            for (int k = 0; k < nops; ++k) ops.push_back(DbsTermReadOp{DbsTermReadOp::Token});
            addSpan(m.capturedStart(), end, ops);
        }
    }
    std::sort(spans.begin(), spans.end(),
              [](const IxSpan &a, const IxSpan &b) { return a.start < b.start; });
    return spans;
}

// Build the probe source: every read expression EXPR becomes
// (::std::cout << "\001DBS<id>\002", EXPR). Insertions add no
// newlines, so compiler line numbers survive; the marker bytes
// (\x01 … \x02) cannot collide with coursework output.
QString ixInstrument(const QString &code, const QVector<IxSpan> &spans)
{
    QString out;
    out.reserve(code.size() + spans.size() * 40);
    int last = 0;
    for (int id = 0; id < spans.size(); ++id) {
        out += code.mid(last, spans[id].start - last);
        out += QStringLiteral("(::std::cout << \"\\001DBS%1\\002\", ").arg(id);
        out += code.mid(spans[id].start, spans[id].end - spans[id].start);
        out += QLatin1Char(')');
        last = spans[id].end;
    }
    out += code.mid(last);
    return out;
}

// Split probe stdout into the marker-free output and the ordered
// read occurrences (site id, offset in the stripped output).
void ixParseProbe(const QString &raw, QString &stripped,
                  QVector<int> &occSites, QVector<int> &occOffsets)
{
    stripped.clear(); occSites.clear(); occOffsets.clear();
    int pos = 0;
    const int n = raw.size();
    while (pos < n) {
        const int hit = raw.indexOf(QChar(0x01), pos);
        if (hit < 0) { stripped += raw.mid(pos); break; }
        // marker shape: \x01 'D' 'B' 'S' digits \x02
        int j = hit + 1;
        bool ok = raw.mid(j, 3) == QStringLiteral("DBS");
        if (ok) {
            j += 3;
            const int d0 = j;
            while (j < n && raw[j].isDigit()) ++j;
            ok = j > d0 && j < n && raw[j] == QChar(0x02);
            if (ok) {
                stripped += raw.mid(pos, hit - pos);
                occSites.push_back(raw.mid(d0, j - d0).toInt());
                occOffsets.push_back(stripped.size());
                pos = j + 1;
                continue;
            }
        }
        stripped += raw.mid(pos, hit - pos + 1); // literal \x01 byte
        pos = hit + 1;
    }
}

struct IxSimResult {
    bool finished = false;
    int starvedOcc = -1;          // occurrence index, when !finished
    QVector<int> lineFirstOcc;    // per input line, -1 = never consumed
};

// Simulate C++ stream consumption of the committed input `text`
// (whose lines are `lines`, without their '\n') by the read
// occurrences in execution order.
IxSimResult ixSimulate(const QString &text, const QStringList &lines,
                       const QVector<int> &occSites,
                       const QVector<DbsTermReadSite> &sites)
{
    IxSimResult res;
    res.lineFirstOcc.fill(-1, lines.size());
    // Line start offsets in the text.
    QVector<int> lineStart, lineEnd;
    {
        int start = 0;
        for (int i = 0; i < text.size(); ++i) {
            if (text[i] == QLatin1Char('\n')) {
                lineStart.push_back(start); lineEnd.push_back(i + 1);
                start = i + 1;
            }
        }
        if (start < text.size()) { lineStart.push_back(start); lineEnd.push_back(text.size()); }
    }
    auto markConsumed = [&](int a, int b, int occIdx) {
        for (int li = 0; li < lineStart.size(); ++li)
            if (res.lineFirstOcc[li] < 0 && a < lineEnd[li] && lineStart[li] < b)
                res.lineFirstOcc[li] = occIdx;
    };
    auto isWs = [](QChar c) {
        return c == QLatin1Char(' ') || c == QLatin1Char('\t')
            || c == QLatin1Char('\n') || c == QLatin1Char('\r')
            || c == QLatin1Char('\v') || c == QLatin1Char('\f');
    };
    int pos = 0;
    for (int oi = 0; oi < occSites.size(); ++oi) {
        const DbsTermReadSite &site = sites[occSites[oi]];
        for (const DbsTermReadOp &op : site.ops) {
            if (op.kind == DbsTermReadOp::Token) {
                const int a = pos;
                while (pos < text.size() && isWs(text[pos])) ++pos;
                if (pos >= text.size()) {
                    markConsumed(a, pos, oi);
                    res.starvedOcc = oi;
                    return res;
                }
                while (pos < text.size() && !isWs(text[pos])) ++pos;
                markConsumed(a, pos, oi);
            } else if (op.kind == DbsTermReadOp::Line) {
                if (pos >= text.size()) {
                    res.starvedOcc = oi;
                    return res;
                }
                const int a = pos;
                const int nl = text.indexOf(QLatin1Char('\n'), pos);
                pos = nl < 0 ? text.size() : nl + 1;
                markConsumed(a, pos, oi);
            } else { // Char / Skip
                if (pos >= text.size()) {
                    res.starvedOcc = oi;
                    return res;
                }
                markConsumed(pos, pos + 1, oi);
                ++pos;
            }
        }
    }
    res.finished = true;
    return res;
}

// Outcome of one stage, cached per committed-input text.
struct IxStageOutcome {
    bool compileFailed = false;
    QString compileStderr;
    bool waiting = false;         // starved (or unverifiable end)
    int waitingOffset = 0;
    bool finished = false;        // simulation: all reads satisfied
    QString stripped;             // probe's marker-free full output
    IxSimResult sim;
    QVector<int> occOffsets;
};

IxStageOutcome ixEvaluateProbe(const DbsOnlineRunResult &r,
                               const QString &inputText,
                               const QStringList &lines,
                               const QVector<DbsTermReadSite> &sites)
{
    IxStageOutcome out;
    if (r.stage == QLatin1String("compile")) {
        out.compileFailed = true;
        out.compileStderr = r.stdErr;
        return out;
    }
    QString stripped;
    QVector<int> occSites;
    ixParseProbe(r.stdOut, stripped, occSites, out.occOffsets);
    out.stripped = stripped;
    // The server slices stdout at 65536 chars and annotates the
    // time limit in stderr: either means the observed execution
    // may be incomplete.
    const bool complete = !r.stdErr.contains(QStringLiteral("[Time limit exceeded"))
            && r.stdOut.size() < 65536;
    out.sim = ixSimulate(inputText, lines, occSites, sites);
    if (out.sim.finished && complete) {
        out.finished = true;
    } else if (out.sim.finished) {
        // Observed reads were all satisfied but the run did not
        // provably end: treat the end of the observed output as
        // the waiting point (honest "cannot verify" state).
        out.waiting = true;
        out.waitingOffset = stripped.size();
    } else {
        out.waiting = true;
        out.waitingOffset = out.occOffsets[out.sim.starvedOcc];
    }
    return out;
}
} // namespace

void DbsTermRunWorker::run()
{
    qRegisterMetaType<DbsOnlineRunResult>("DbsOnlineRunResult");
    const DbsOnlineRunResult result = dbsOnlineRunCode(mLanguage, mCode, mStdinText);
    emit dbsDone(mGeneration, mProbe, result);
}

DbsTerminalWindow::DbsTerminalWindow(QWidget *parent)
    // Part 9 Addendum: NOT Qt::Window — on Qt for WebAssembly a
    // plain window top-level is stretched over the whole canvas.
    // Qt::Dialog gets a true floating window (platform title bar
    // with close button, draggable, honoured geometry), and
    // Qt::WindowStaysOnTopHint keeps it in front, chat-panel
    // style, when the user clicks back into the editor. It is
    // still shown with show() only — never modal.
    : QWidget(parent, Qt::Dialog | Qt::WindowStaysOnTopHint)
{
    setAttribute(Qt::WA_DeleteOnClose, true);
    setWindowTitle(tr("Terminal"));
    resize(760, 500);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    mOutput = new QPlainTextEdit(this);
    mOutput->setReadOnly(true);
    mOutput->setUndoRedoEnabled(false);
    mOutput->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    QFont mono(QStringLiteral("JetBrains Mono"), 11);
    mono.setStyleHint(QFont::Monospace);
    mOutput->setFont(mono);
    mOutput->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background: #0C0C0C; color: #CCCCCC;"
        " border: 1px solid #2A2A2A; padding: 6px;"
        " selection-background-color: #264F78; }"));
    layout->addWidget(mOutput, 1);

    auto *inputRow = new QHBoxLayout();
    inputRow->setSpacing(6);
    mInputLabel = new QLabel(
                tr("Input\n(stdin):"), this);
    mInputLabel->setStyleSheet(QStringLiteral("color: #8A8A8A;"));
    inputRow->addWidget(mInputLabel);

    mInput = new QPlainTextEdit(this);
    mInput->setFont(mono);
    mInput->setFixedHeight(58);
    mInput->setPlaceholderText(tr(
        "If the program reads input (cin / scanf), type it here, "
        "then run again — Ctrl+Enter"));
    mInput->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background: #111111; color: #CCCCCC;"
        " border: 1px solid #2A2A2A; padding: 4px;"
        " selection-background-color: #264F78; }"));
    inputRow->addWidget(mInput, 1);

    auto *buttonCol = new QVBoxLayout();
    mRunAgainButton = new QPushButton(
                tr("▶ Run again\nwith this input"), this);
    connect(mRunAgainButton, &QPushButton::clicked,
            this, &DbsTerminalWindow::rerunRequested);
    buttonCol->addWidget(mRunAgainButton);
    buttonCol->addStretch(1);
    inputRow->addLayout(buttonCol);
    layout->addLayout(inputRow);

    // "Press any key to close" watches the output pane (typing in
    // the input box must never close the window); Esc closes from
    // anywhere, and Ctrl+Enter in the input box re-runs.
    mOutput->installEventFilter(this);
    mInput->installEventFilter(this);
    connect(mInput, &QPlainTextEdit::textChanged, this, [this]() {
        if (!mSettingStdin)
            mStdinPristine = false;
    });
    // Option 2: while an interactive session owns the output pane,
    // everything in it before the input tail is locked. This watch
    // restores the locked transcript if an edit slips past the key
    // filter (context menu, IME…) and tracks the tail text.
    connect(mOutput, &QPlainTextEdit::textChanged, this, [this]() {
        if (mIxGuard || (mIxState != DbsIxState::Waiting
                         && mIxState != DbsIxState::Probing))
            return;
        const QString doc = mOutput->toPlainText();
        if (!doc.startsWith(mIxLockedText)) {
            mIxGuard = true;
            mOutput->setPlainText(mIxLockedText + mIxTailText);
            QTextCursor c = mOutput->textCursor();
            c.movePosition(QTextCursor::End);
            mOutput->setTextCursor(c);
            mIxGuard = false;
            return;
        }
        QString tail = doc.mid(mIxLockedText.size());
        if (tail.contains(QLatin1Char('\n'))) {
            tail.replace(QLatin1Char('\n'), QLatin1Char(' '));
            mIxGuard = true;
            mOutput->setPlainText(mIxLockedText + tail);
            QTextCursor c = mOutput->textCursor();
            c.movePosition(QTextCursor::End);
            mOutput->setTextCursor(c);
            mIxGuard = false;
        }
        mIxTailText = tail;
    });
}

// Part 9 Stage 3 (Sufyan, 2026-11-04): "only output show ho" —
// the console shows ONLY the program's own stdout/stderr, plus
// the closing line when the run finishes. No header, no "> file"
// line, no "Starting..."/"Running..." placeholders: while a run
// is in flight the pane simply stays empty until output arrives.
void DbsTerminalWindow::beginSession(const QString &displayName)
{
    mOutput->clear();
    mHasSession = true;
    mPending = true;
    mFinished = false;
    mIxDisplayName = displayName;
    ixReset();
    mIxArmed = true;
    setWindowTitle(tr("%1 — Terminal").arg(displayName));
    if (!mPositioned) {
        mPositioned = true;
        if (sHasSavedGeometry) {
            // Reopen where the user left the previous panel.
            resize(sSavedSize);
            move(sSavedPos);
        }
        // Authoritative placement once the platform frame exists
        // (the caller shows the window right after this returns):
        // frameGeometry() is only meaningful then, and the frame —
        // title bar included — must end up fully inside the IDE.
        QTimer::singleShot(0, this, [this]() {
            if (sHasSavedGeometry) {
                move(sSavedPos);
            } else if (parentWidget()) {
                // First open: a generous panel anchored bottom-right
                // of the IDE, editor visible around it.
                const int margin = 24;
                const QSize fs = frameGeometry().size();
                move(std::max(0, parentWidget()->width() - fs.width() - margin),
                     std::max(0, parentWidget()->height() - fs.height() - margin));
            }
            clampInsideHost();
            // The single-thread build's compositor settles the
            // frame later than the multi-thread one's; re-check
            // once it has had time to (a no-op when already inside).
            QTimer::singleShot(250, this, [this]() { clampInsideHost(); });
        });
    }
}

void DbsTerminalWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    QTimer::singleShot(0, this, [this]() { clampInsideHost(); });
}

void DbsTerminalWindow::clampInsideHost()
{
    QWidget *host = parentWidget();
    if (!host)
        return;
    const QRect fr = frameGeometry();
    int x = fr.x();
    int y = fr.y();
    if (fr.width() < host->width())
        x = std::max(0, std::min(x, host->width() - fr.width()));
    if (fr.height() < host->height())
        y = std::max(0, std::min(y, host->height() - fr.height()));
    if (x != fr.x() || y != fr.y())
        move(x, y);
}

void DbsTerminalWindow::closeEvent(QCloseEvent *event)
{
    // Remember where the panel was (and how big), so the next
    // Run reopens it there instead of jumping back to default.
    sSavedPos = pos();
    sSavedSize = size();
    sHasSavedGeometry = true;
    ++mIxGeneration; // any stage still in flight dies with us
    QWidget::closeEvent(event);
}

void DbsTerminalWindow::beginRerun(const QString &displayName)
{
    Q_UNUSED(displayName);
    mPending = true;
    mFinished = false;
    // A fresh interactive takeover may start with the rerun's
    // stdin (the input zone's text) as pre-committed input.
    ixReset();
    mIxArmed = true;
    // A single blank line separates the previous run's output from
    // the next one, console-fashion — no text of our own.
    appendLine(QString(), kText);
}

void DbsTerminalWindow::setRunContext(const QString &sourceFile,
                                       const QString &exeName,
                                       const QString &workDir)
{
    mSourceFile = sourceFile;
    mExeName = exeName;
    mWorkDir = workDir;
}

void DbsTerminalWindow::setStdinText(const QString &text)
{
    // Option 2: the first stdin delivery of a fresh session is
    // the interactive engine's cue (it knows the run's source
    // from setRunContext and uses this text as the pre-committed
    // input). If the engine takes over, the zone text below is
    // irrelevant — the zone is hidden.
    if (mIxArmed && mIxState == DbsIxState::Inactive)
        ixMaybeStart(text);
    if (!mStdinPristine)
        return; // the user's own input wins over a later prefill
    mSettingStdin = true;
    mInput->setPlainText(text);
    mSettingStdin = false;
    mStdinPristine = true;
}

QString DbsTerminalWindow::stdinText() const
{
    if (mIxState != DbsIxState::Inactive)
        return mIxInputText;
    return mInput->toPlainText();
}

void DbsTerminalWindow::appendText(const QString &text, const QColor &color)
{
    if (text.isEmpty())
        return;
    QTextCursor cursor = mOutput->textCursor();
    cursor.movePosition(QTextCursor::End);
    QTextCharFormat format;
    format.setForeground(color);
    cursor.setCharFormat(format);
    cursor.insertText(text);
    mOutput->setTextCursor(cursor);
    mOutput->ensureCursorVisible();
}

void DbsTerminalWindow::appendLine(const QString &text, const QColor &color)
{
    // Start on a fresh line unless the output is empty or already
    // ends at a line start.
    const QString current = mOutput->toPlainText();
    if (!current.isEmpty() && !current.endsWith(QLatin1Char('\n')))
        appendText(QStringLiteral("\n"), kText);
    appendText(text + QLatin1Char('\n'), color);
}

void DbsTerminalWindow::finishWithClosingLine(const QString &line)
{
    appendLine(line, kDim);
    mPending = false;
    mFinished = true;
    mOutput->setFocus();
}

void DbsTerminalWindow::showResult(const DbsOnlineRunResult &result)
{
    // Option 2: while the interactive engine owns this session,
    // the IDE run's own one-shot result is held back (it used the
    // same stdin the engine started from). It is replayed only if
    // the session degrades to the plain display.
    if (mIxState != DbsIxState::Inactive) {
        mIxSwallowed = result;
        mIxHasSwallowed = true;
        return;
    }
    if (result.stage == QLatin1String("compile")
            || result.stage == QLatin1String("input")) {
        // The run's own compile failed (e.g. the code changed
        // under a stale placeholder): diagnostics, no program ran.
        if (!result.stdErr.isEmpty())
            appendText(result.stdErr, kText);
        if (!result.stdOut.isEmpty())
            appendText(result.stdOut, kText);
        finishWithClosingLine(tr(
            "— Compilation failed (online compiler) — press any "
            "key or close this window —"));
        return;
    }
    if (!result.stdOut.isEmpty())
        appendText(result.stdOut, kText);
    if (!result.stdErr.isEmpty())
        appendText(result.stdErr, kError);
    finishWithClosingLine(tr(
        "— Process exited with code %1 · %2 ms — press any key "
        "or close this window —").arg(result.exitCode).arg(result.timeMs));
}

void DbsTerminalWindow::showCompileFailed()
{
    if (!mPending)
        return;
    if (mIxState != DbsIxState::Inactive)
        return; // the engine surfaces compile failures itself
    finishWithClosingLine(tr(
        "— Compilation failed — the errors are listed in the "
        "Compiler Output panel — press any key or close this "
        "window —"));
}

void DbsTerminalWindow::showErrorLine(const QString &text)
{
    if (mIxState != DbsIxState::Inactive) {
        mIxSwallowedError = text;
        mIxHasSwallowedError = true;
        return;
    }
    appendLine(text, kError);
    finishWithClosingLine(tr("— Press any key or close this window —"));
}

bool DbsTerminalWindow::closeOnKey(QKeyEvent *event, bool fromInputBox)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return true;
    }
    if (fromInputBox)
        return false; // typing input must never dismiss the console
    if (!mFinished)
        return false;
    const Qt::KeyboardModifiers mods = event->modifiers();
    if (mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        return false; // keep Ctrl+C (copy), Ctrl+A, etc. working
    switch (event->key()) {
    case Qt::Key_Shift: case Qt::Key_Control: case Qt::Key_Alt:
    case Qt::Key_Meta: case Qt::Key_CapsLock: case Qt::Key_NumLock:
    case Qt::Key_Up: case Qt::Key_Down: case Qt::Key_Left:
    case Qt::Key_Right: case Qt::Key_PageUp: case Qt::Key_PageDown:
    case Qt::Key_Home: case Qt::Key_End: case Qt::Key_Tab:
    case Qt::Key_Backtab: case Qt::Key_F1: case Qt::Key_F5:
    case Qt::Key_F9: case Qt::Key_F11:
        return false; // navigation and IDE hotkeys are not "any key"
    default:
        break;
    }
    if (event->text().isEmpty()
            && event->key() != Qt::Key_Return
            && event->key() != Qt::Key_Enter
            && event->key() != Qt::Key_Space)
        return false;
    close();
    return true;
}

bool DbsTerminalWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (watched == mInput
                && (keyEvent->key() == Qt::Key_Return
                    || keyEvent->key() == Qt::Key_Enter)
                && (keyEvent->modifiers() & Qt::ControlModifier)) {
            emit rerunRequested();
            return true;
        }
        // Option 2: the output pane is the input line while an
        // interactive session waits (or works). Enter commits,
        // editing is confined to the tail after the locked
        // transcript, and none of it may fall into closeOnKey.
        if (watched == mOutput && mIxState != DbsIxState::Inactive
                && mIxState != DbsIxState::Done) {
            const int key = keyEvent->key();
            if (key == Qt::Key_Escape) {
                close();
                return true;
            }
            if (key == Qt::Key_Return || key == Qt::Key_Enter) {
                if (keyEvent->modifiers() & Qt::ControlModifier)
                    return true; // swallowed; no meaning here
                if (mIxState == DbsIxState::Waiting)
                    ixCommitTail();
                else
                    mIxEnterQueued = true; // Probing / Finishing
                return true;
            }
            QTextCursor cur = mOutput->textCursor();
            const int lockEnd = mIxLockedText.size();
            const int selStart = std::min(cur.anchor(), cur.position());
            const int selEnd = std::max(cur.anchor(), cur.position());
            if (key == Qt::Key_Backspace) {
                if (selStart < lockEnd || (selStart == selEnd && cur.position() <= lockEnd)) {
                    cur.movePosition(QTextCursor::End);
                    mOutput->setTextCursor(cur);
                    return true;
                }
                return QWidget::eventFilter(watched, event);
            }
            if (key == Qt::Key_Delete) {
                if (selStart < lockEnd)
                    return true;
                return QWidget::eventFilter(watched, event);
            }
            if (keyEvent->modifiers() & Qt::ControlModifier) {
                // Copy / select-all stay; cut, paste and undo are
                // refused — the textChanged watch is the backstop.
                if (key == Qt::Key_X || key == Qt::Key_V || key == Qt::Key_Z)
                    return true;
                return QWidget::eventFilter(watched, event);
            }
            if (!keyEvent->text().isEmpty() && selStart < lockEnd) {
                // Typing with the cursor inside the locked region:
                // the character belongs at the end of the tail.
                cur.movePosition(QTextCursor::End);
                mOutput->setTextCursor(cur);
            }
            return QWidget::eventFilter(watched, event);
        }
        if (closeOnKey(keyEvent, watched == mInput))
            return true;
    }
    return QWidget::eventFilter(watched, event);
}

void DbsTerminalWindow::keyPressEvent(QKeyEvent *event)
{
    if (closeOnKey(event, false))
        return;
    QWidget::keyPressEvent(event);
}

// =====================================================================
// Part 9 Option 2 — interactive session engine.
// =====================================================================

void DbsTerminalWindow::ixReset()
{
    ++mIxGeneration;
    mIxState = DbsIxState::Inactive;
    mIxLanguage.clear();
    mIxCode.clear();
    mIxProbeCode.clear();
    mIxSites.clear();
    mIxInputText.clear();
    mIxLines.clear();
    mIxEchoOffsets.clear();
    mIxWaitingOffset = 0;
    mIxBaseText.clear();
    mIxStripped.clear();
    mIxLockedText.clear();
    mIxTailText.clear();
    mIxEnterQueued = false;
    mIxHasSwallowed = false;
    mIxHasSwallowedError = false;
    mIxRunsUsed = 0;
    mIxVerifyCompile = false;
    mIxProbeCache.clear();
    ixSetInputRowVisible(true);
    mOutput->setReadOnly(true);
    mOutput->setCursor(Qt::IBeamCursor);
    if (!mIxDisplayName.isEmpty())
        setWindowTitle(tr("%1 — Terminal").arg(mIxDisplayName));
}

void DbsTerminalWindow::ixSetInputRowVisible(bool visible)
{
    mInputLabel->setVisible(visible);
    mInput->setVisible(visible);
    mRunAgainButton->setVisible(visible);
}

void DbsTerminalWindow::ixMaybeStart(const QString &initialStdin)
{
    mIxArmed = false;
    if (mSourceFile.isEmpty())
        return;
    const QString language = dbsOnlineLanguageForFilename(mSourceFile);
    if (language != QLatin1String("cpp") && language != QLatin1String("c"))
        return; // instrumentation is a C/C++ source transform
    QFile f(mSourceFile);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QString code = QString::fromUtf8(f.readAll());
    const QString masked = ixMaskSource(code);
    const QVector<IxSpan> spans = ixFindSites(masked, code);
    if (spans.isEmpty())
        return; // no instrumentable reads: plain one-shot session
    // Take over.
    mIxLanguage = language;
    mIxCode = code;
    mIxProbeCode = ixInstrument(code, spans);
    mIxSites.clear();
    for (const IxSpan &sp : spans)
        mIxSites.push_back(DbsTermReadSite{sp.start, sp.end, sp.ops});
    mIxInputText = initialStdin;
    mIxLines = initialStdin.split(QLatin1Char('\n'));
    if (!mIxLines.isEmpty() && mIxLines.last().isEmpty()
            && initialStdin.endsWith(QLatin1Char('\n')))
        mIxLines.removeLast();
    if (initialStdin.isEmpty())
        mIxLines.clear();
    mIxEchoOffsets.fill(-1, mIxLines.size());
    mIxState = DbsIxState::Probing;
    ixSetInputRowVisible(false);
    mOutput->setReadOnly(false);
    qDebug("DBSTERM ix start: %d read sites, %d pre-committed lines",
           mIxSites.size(), mIxLines.size());
    ixStartStage();
}

void DbsTerminalWindow::ixStartStage()
{
    mIxState = DbsIxState::Probing;
    setWindowTitle(tr("%1 — Terminal (running…)").arg(mIxDisplayName));
    const auto cached = mIxProbeCache.constFind(mIxInputText);
    if (cached != mIxProbeCache.constEnd()) {
        qDebug("DBSTERM ix stage: probe cache hit");
        ixProcessProbe(*cached);
        return;
    }
    ixLaunch(true, mIxProbeCode, mIxInputText + ixSentinelTail());
    ixRender();
}

void DbsTerminalWindow::ixLaunch(bool probe, const QString &code,
                                 const QString &stdinText)
{
    ++mIxRunsUsed;
    qDebug("DBSTERM ix run #%d (%s), stdin %d chars",
           mIxRunsUsed, probe ? "probe" : "real", stdinText.size());
    auto *worker = new DbsTermRunWorker(mIxLanguage, code, stdinText,
                                        mIxGeneration, probe);
    connect(worker, &DbsTermRunWorker::dbsDone,
            this, &DbsTerminalWindow::ixOnRunDone);
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void DbsTerminalWindow::ixOnRunDone(int generation, bool probe,
                                    const DbsOnlineRunResult &result)
{
    if (generation != mIxGeneration
            || mIxState == DbsIxState::Inactive
            || mIxState == DbsIxState::Done)
        return; // stale (panel reset/closed, or session over)
    if (!result.networkOk) {
        qDebug("DBSTERM ix network failure: %s",
               qPrintable(result.networkError));
        ixDegrade();
        return;
    }
    if (probe) {
        mIxProbeCache.insert(mIxInputText, result);
        ixProcessProbe(result);
    } else {
        ixFinishFromReal(result);
    }
}

void DbsTerminalWindow::ixProcessProbe(const DbsOnlineRunResult &result)
{
    const IxStageOutcome out = ixEvaluateProbe(result, mIxInputText,
                                               mIxLines, mIxSites);
    if (out.compileFailed) {
        // Either the program really does not compile, or the
        // instrumentation broke valid code. One real run decides:
        // it is also exactly the fallback display if the probe was
        // at fault.
        qDebug("DBSTERM ix probe compile failed; verifying with real run");
        mIxState = DbsIxState::Finishing;
        mIxVerifyCompile = true;
        ixLaunch(false, mIxCode, mIxInputText);
        return;
    }
    // Refresh echo offsets from the simulation: a line's echo sits
    // at the output offset of the first read that consumed it.
    for (int li = 0; li < mIxLines.size(); ++li) {
        const int fo = out.sim.lineFirstOcc.value(li, -1);
        if (fo >= 0 && fo < out.occOffsets.size())
            mIxEchoOffsets[li] = out.occOffsets[fo];
    }
    mIxStripped = out.stripped;
    if (out.finished) {
        mIxState = DbsIxState::Finishing;
        ixLaunch(false, mIxCode, mIxInputText);
        return;
    }
    mIxWaitingOffset = out.waitingOffset;
    mIxBaseText = out.stripped.left(out.waitingOffset);
    mIxState = DbsIxState::Waiting;
    setWindowTitle(tr("%1 — Terminal").arg(mIxDisplayName));
    ixRender();
    // Focus the pane for typing, but only if the user is already
    // interacting with this window (never yank focus from the
    // editor while they are mid-edit there).
    if (isActiveWindow() || mOutput->hasFocus())
        mOutput->setFocus();
    if (mIxEnterQueued) {
        mIxEnterQueued = false;
        ixCommitTail();
    }
    ixWriteDebugState();
}

void DbsTerminalWindow::ixFinishFromReal(const DbsOnlineRunResult &result)
{
    // A compile-stage answer here means the REAL code failed to
    // compile (the probe said the same and we came to verify, or
    // the code changed under us): show it as a compile failure.
    if (result.stage == QLatin1String("compile")
            || result.stage == QLatin1String("input")) {
        ixCompileFailed(result.stdErr);
        return;
    }
    if (mIxVerifyCompile) {
        // The real code compiled and ran while the probe would
        // not compile: the instrumentation broke valid code.
        // Degrade, showing this genuine result the plain way.
        qDebug("DBSTERM ix probe instrumentation broke compilation; "
               "degrading");
        mIxVerifyCompile = false;
        mIxHasSwallowed = true;
        mIxSwallowed = result;
        ixDegrade();
        return;
    }
    // Integrity check: the definitive output must equal the
    // probe's marker-stripped output. Any difference means the
    // transcript's read points cannot be trusted (unseen read,
    // marker side effect, nondeterministic program): degrade to
    // the plain one-shot display of this very result.
    if (result.stdOut != mIxStripped) {
        qDebug("DBSTERM ix INTEGRITY MISMATCH: real stdout differs "
               "from probe-stripped output; degrading");
        mIxHasSwallowed = true;
        mIxSwallowed = result;
        ixDegrade();
        return;
    }
    mIxBaseText = result.stdOut;
    mIxState = DbsIxState::Done;
    mPending = false;
    mFinished = true;
    setWindowTitle(tr("%1 — Terminal").arg(mIxDisplayName));
    mIxTailText.clear();
    // Render the full transcript, then today's finish: the
    // program's stderr (soft red) and the closing line.
    mIxGuard = true;
    mOutput->setPlainText(ixAssemble(mIxBaseText));
    mIxGuard = false;
    mOutput->setReadOnly(true);
    if (!result.stdErr.isEmpty())
        appendText(result.stdErr, kError);
    finishWithClosingLine(tr(
        "— Process exited with code %1 · %2 ms — press any key "
        "or close this window —").arg(result.exitCode).arg(result.timeMs));
    qDebug("DBSTERM ix done: runs used %d", mIxRunsUsed);
    ixWriteDebugState();
}

void DbsTerminalWindow::ixCompileFailed(const QString &stdErr)
{
    mIxState = DbsIxState::Done;
    setWindowTitle(tr("%1 — Terminal").arg(mIxDisplayName));
    mIxGuard = true;
    mOutput->clear();
    mIxGuard = false;
    mOutput->setReadOnly(true);
    if (!stdErr.isEmpty())
        appendText(stdErr, kText);
    finishWithClosingLine(tr(
        "— Compilation failed (online compiler) — press any "
        "key or close this window —"));
    ixWriteDebugState();
}

void DbsTerminalWindow::ixDegrade()
{
    // Visible fallback to the pre-Option-2 behaviour: input zone
    // back (holding everything committed so far), pane read-only,
    // and the IDE run's own result — or the freshest real result —
    // shown the plain way.
    qDebug("DBSTERM ix DEGRADED to one-shot display");
    const bool have = mIxHasSwallowed;
    const DbsOnlineRunResult held = mIxSwallowed;
    const bool haveErr = mIxHasSwallowedError;
    const QString heldErr = mIxSwallowedError;
    const QString committed = mIxInputText;
    ixReset();
    mSettingStdin = true;
    mInput->setPlainText(committed);
    mSettingStdin = false;
    mStdinPristine = false;
    if (have)
        showResult(held);
    else if (haveErr)
        showErrorLine(heldErr);
    else
        showErrorLine(tr("The interactive session could not be "
                         "completed (network problem)."));
}

void DbsTerminalWindow::ixCommitTail()
{
    const QString line = mIxTailText;
    mIxTailText.clear();
    mIxLines.push_back(line);
    mIxEchoOffsets.push_back(mIxWaitingOffset);
    mIxInputText += line + QLatin1Char('\n');
    qDebug("DBSTERM ix commit line %d: %s",
           mIxLines.size(), qPrintable(line));
    ixRender(); // the echo appears at the read point at once
    ixStartStage();
}

QString DbsTerminalWindow::ixAssemble(const QString &base) const
{
    QVector<QPair<int, int>> order; // (offset, line index)
    for (int li = 0; li < mIxLines.size(); ++li)
        if (mIxEchoOffsets.value(li, -1) >= 0)
            order.push_back({mIxEchoOffsets[li], li});
    std::sort(order.begin(), order.end(),
              [](const QPair<int, int> &a, const QPair<int, int> &b) {
                  return a.first != b.first ? a.first < b.first
                                            : a.second < b.second;
              });
    QString out;
    int last = 0;
    for (const auto &pr : order) {
        const int off = std::min(pr.first, base.size());
        if (off < last) continue;
        out += base.mid(last, off - last);
        out += mIxLines[pr.second] + QLatin1Char('\n');
        last = off;
    }
    out += base.mid(last);
    return out;
}

void DbsTerminalWindow::ixRender()
{
    mIxLockedText = ixAssemble(mIxBaseText);
    mIxGuard = true;
    mOutput->setPlainText(mIxLockedText + mIxTailText);
    QTextCursor c = mOutput->textCursor();
    c.movePosition(QTextCursor::End);
    mOutput->setTextCursor(c);
    mOutput->ensureCursorVisible();
    mIxGuard = false;
    ixWriteDebugState();
}

// Rig/debug aid: the session's visible state + transcript on
// disk, so automated acceptance can read exactly what the user
// sees (the pane is a canvas; there is no DOM text to scrape).
void DbsTerminalWindow::ixWriteDebugState()
{
    const char *state = mIxState == DbsIxState::Waiting ? "waiting"
        : mIxState == DbsIxState::Done ? "done"
        : mIxState == DbsIxState::Inactive ? "inactive" : "busy";
    QFile f(QDir::homePath() + QStringLiteral("/dbsterm-state.txt"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString body = QStringLiteral("STATE ") + QLatin1String(state)
            + QStringLiteral("\nRUNS ") + QString::number(mIxRunsUsed)
            + QStringLiteral("\n---\n")
            + mOutput->toPlainText();
        f.write(body.toUtf8());
    }
}

#endif // __EMSCRIPTEN__
