/*
 * Dev by Sufyan — WebAssembly port (Part 9).
 *
 * A terminal-style window for program output in the web build.
 *
 * On the PC the runner spawns a real console window (console
 * pauser) and the program's output appears there. A browser has
 * no console to spawn, so the web build shows the output of the
 * online run in this window instead: a top-level, NON-modal
 * window (shown with show(), never exec() — exec() is the Part
 * 7/8 wall) styled after the Windows console: near-black
 * background, monospace text, the program's stderr in a soft red,
 * and the classic console-pauser ending — after the result is
 * shown, pressing any key (outside the input box) or the window's
 * close button dismisses it.
 *
 * It is NOT a real terminal: the online backend runs the program
 * in one shot (POST /api/run), so there is no keystroke-by-
 * keystroke interaction. The honest emulation: the window carries
 * an input (stdin) zone — prefilled with the stdin the current
 * run used (the executor's redirect-input file) — and a
 * "Run again with this input" affordance (button or Ctrl+Enter
 * in the input box) that re-runs the program with that stdin and
 * appends the new output in the same window, console fashion.
 *
 * Part 9 Addendum (2026-11-04, Sufyan): the console must FLOAT
 * over the IDE like a chat panel, not take over the whole page.
 * On Qt for WebAssembly a plain Qt::Window top-level is mapped
 * onto the full canvas (rig-proven with a flag probe), while a
 * Qt::Dialog top-level gets a real floating window: platform
 * title bar (window title + close button, draggable), honoured
 * size/position, and — with Qt::WindowStaysOnTopHint — it stays
 * in front when the user clicks back into the editor, without
 * ever being modal. The window therefore uses
 * Qt::Dialog | Qt::WindowStaysOnTopHint. Its geometry is
 * remembered across instances (it deletes itself on close), so
 * a dragged panel reopens where the user left it; the first
 * open goes to the bottom-right of the IDE.
 *
 * Part 9 Option 2 (2026-11-04, Sufyan): the console must take
 * input LIKE A REAL TERMINAL — the program prints its prompt,
 * waits, the user types the answer on that very line, the next
 * output starts on a new line. The backend is still one-shot,
 * so for C/C++ programs that read stdin this window runs an
 * INTERACTIVE SESSION built from staged re-runs of an
 * INSTRUMENTED probe copy of the code:
 *
 *   - The probe copy has a marker print wrapped around every
 *     input-read expression (cin >> …, getline(cin,…),
 *     cin.get/getline/ignore, scanf/fscanf). The wrap is a
 *     comma expression inserted in place — no new lines, so
 *     compiler diagnostics keep their line numbers.
 *   - A probe run is fed the committed input plus a tail of
 *     sentinel lines, so it executes past the point where the
 *     committed input runs out. Marker positions in its output
 *     are the TRUE read points (in output coordinates); a
 *     simulation of C++ stream consumption over the committed
 *     input then decides, per read, satisfied vs. starved.
 *   - While starved, the transcript shows the program output
 *     up to the read point and the user types the next line
 *     right there (editable tail of the output pane). Enter
 *     commits the line (its echo is rendered at the read
 *     point) and the next stage re-runs.
 *   - When the simulation says every read was satisfied by
 *     the committed input, one final REAL run produces the
 *     definitive output. Its stdout must equal the probe's
 *     marker-stripped output exactly; if it does not (a read
 *     the instrumenter could not see, a marker side effect,
 *     nondeterminism), the session visibly DEGRADES to the
 *     plain one-shot display above instead of faking a
 *     transcript.
 *
 * Programs with no instrumentable reads (and non-C/C++
 * languages) keep the one-shot behaviour, input zone included.
 *
 * This file is only added to the build for EMSCRIPTEN (see
 * RedPandaIDE/CMakeLists.txt); native builds never see it.
 */
#ifndef DBS_TERMINAL_H
#define DBS_TERMINAL_H

#ifdef __EMSCRIPTEN__

#include "../compiler/onlinebackend.h"

#include <QHash>
#include <QThread>
#include <QVector>
#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;

// One staged backend call for the interactive session engine.
// dbsOnlineRunCode() is a blocking synchronous fetch, legal only
// off the GUI thread, so each stage runs on its own short-lived
// thread. The worker has no parent: if the panel is closed
// mid-run, the result signal is severed with the panel and the
// worker deletes itself when done.
class DbsTermRunWorker : public QThread
{
    Q_OBJECT
public:
    DbsTermRunWorker(const QString &language, const QString &code,
                     const QString &stdinText, int generation, bool probe)
        : mLanguage(language), mCode(code), mStdinText(stdinText),
          mGeneration(generation), mProbe(probe) {}

signals:
    void dbsDone(int generation, bool probe,
                 const DbsOnlineRunResult &result);

protected:
    void run() override;

private:
    QString mLanguage;
    QString mCode;
    QString mStdinText;
    int mGeneration;
    bool mProbe;
};

// One input operation inside a read expression, as modelled by
// the consumption simulation.
struct DbsTermReadOp
{
    enum Kind { Token,  // operator>> : skip whitespace, take a token
                Line,   // getline / cin.getline : take through '\n'
                Char,   // cin.get(ch) / scanf %c : take one character
                Skip }; // cin.ignore() : discard (approx. one char)
    Kind kind = Token;
};

struct DbsTermReadSite
{
    int start = 0;              // span in the original source
    int end = 0;
    QVector<DbsTermReadOp> ops; // extractions, in order
};

class DbsTerminalWindow : public QWidget
{
    Q_OBJECT
public:
    explicit DbsTerminalWindow(QWidget *parent = nullptr);

    // Start a fresh console session for a program (clears the
    // window, prints the console header + "Starting...").
    void beginSession(const QString &displayName);
    // Append a divider + "Running..." for a re-run of the same
    // program in the same window.
    void beginRerun(const QString &displayName);
    // The run this window belongs to (set by CompilerManager's
    // online path through MainWindow each time a run starts).
    void setRunContext(const QString &sourceFile, const QString &exeName,
                       const QString &workDir);
    // Prefill the input zone with the stdin a run used. Only
    // replaces the text while the user has not edited it.
    void setStdinText(const QString &text);
    QString stdinText() const;
    QString sourceFile() const { return mSourceFile; }
    QString exeName() const { return mExeName; }
    QString workDir() const { return mWorkDir; }
    bool hasSession() const { return mHasSession; }
    // A session has begun and no result/failure was shown yet.
    bool sessionPending() const { return mPending; }

    // Print one online-run result: stdout plain, stderr in soft
    // red, then the console-pauser closing line. A compile-stage
    // result prints its diagnostics + a compilation-failed line.
    void showResult(const DbsOnlineRunResult &result);
    // The F11 compile step failed before any run could start.
    void showCompileFailed();
    // A run-level failure line (network error, missing source...)
    // + the closing affordance.
    void showErrorLine(const QString &text);

signals:
    // The user asked to run the program again with the input
    // zone's current text (button or Ctrl+Enter in the input box).
    void rerunRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    // Keep the whole frame (platform title bar included) inside the
    // host IDE window; applied once the platform frame exists.
    void clampInsideHost();

private:
    // (printHeader removed in Stage 3: output-only console.)
    void appendText(const QString &text, const QColor &color);
    void appendLine(const QString &text, const QColor &color);
    void finishWithClosingLine(const QString &line);
    bool closeOnKey(QKeyEvent *event, bool fromInputBox);

    // ---- Part 9 Option 2: interactive sessions ----
    enum class DbsIxState { Inactive, Probing, Waiting, Finishing, Done };
    // Try to take over the session interactively. Called from
    // setStdinText(), i.e. once per run start, with the stdin the
    // IDE's own run is using (it becomes the pre-committed input).
    void ixMaybeStart(const QString &initialStdin);
    void ixReset();
    void ixStartStage();
    void ixLaunch(bool probe, const QString &code, const QString &stdinText);
    void ixOnRunDone(int generation, bool probe,
                     const DbsOnlineRunResult &result);
    void ixProcessProbe(const DbsOnlineRunResult &result);
    void ixFinishFromReal(const DbsOnlineRunResult &result);
    void ixCompileFailed(const QString &stdErr);
    void ixDegrade();
    void ixRender();
    void ixCommitTail();
    QString ixAssemble(const QString &base) const;
    void ixSetInputRowVisible(bool visible);
    void ixWriteDebugState();

    QPlainTextEdit *mOutput;
    QPlainTextEdit *mInput;
    QPushButton *mRunAgainButton;
    QLabel *mInputLabel;

    QString mSourceFile;
    QString mExeName;
    QString mWorkDir;
    bool mHasSession = false;
    bool mPending = false;
    bool mFinished = false;     // a result is on screen: any key closes
    bool mStdinPristine = true; // input zone not edited by the user yet
    bool mSettingStdin = false; // guard for the pristine tracking
    bool mPositioned = false;

    // Interactive-session state (Option 2).
    DbsIxState mIxState = DbsIxState::Inactive;
    bool mIxArmed = false;      // beginSession/beginRerun seen; the
                                // next setStdinText may take over
    int mIxGeneration = 0;      // stale-result guard
    QString mIxDisplayName;
    QString mIxLanguage;
    QString mIxCode;
    QString mIxProbeCode;
    QVector<DbsTermReadSite> mIxSites;
    QString mIxInputText;       // committed stdin, exact text
    QStringList mIxLines;       // committed lines (no trailing \n)
    QVector<int> mIxEchoOffsets;   // per line: offset in base-output
                                   // coordinates, -1 = no echo
    int mIxWaitingOffset = 0;   // current read point (base coords)
    QString mIxBaseText;        // output the transcript is built on
    QString mIxStripped;        // last probe's stripped full output
    QString mIxLockedText;      // rendered transcript (locked part)
    QString mIxTailText;        // uncommitted input being typed
    bool mIxEnterQueued = false;
    bool mIxGuard = false;      // reentrancy guard for text edits
    bool mIxHasSwallowed = false;
    DbsOnlineRunResult mIxSwallowed; // the IDE run's own result,
                                     // held while the engine works
    bool mIxHasSwallowedError = false;
    QString mIxSwallowedError;
    int mIxRunsUsed = 0;        // backend runs spent by the engine
    bool mIxVerifyCompile = false; // the pending real run arbitrates
                                   // a probe compile failure
    QHash<QString, DbsOnlineRunResult> mIxProbeCache; // per input text
};

#endif // __EMSCRIPTEN__
#endif // DBS_TERMINAL_H
