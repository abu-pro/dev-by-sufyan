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
 * This file is only added to the build for EMSCRIPTEN (see
 * RedPandaIDE/CMakeLists.txt); native builds never see it.
 */
#ifndef DBS_TERMINAL_H
#define DBS_TERMINAL_H

#ifdef __EMSCRIPTEN__

#include "../compiler/onlinebackend.h"

#include <QWidget>

class QPlainTextEdit;
class QPushButton;

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

    QPlainTextEdit *mOutput;
    QPlainTextEdit *mInput;
    QPushButton *mRunAgainButton;

    QString mSourceFile;
    QString mExeName;
    QString mWorkDir;
    bool mHasSession = false;
    bool mPending = false;
    bool mFinished = false;     // a result is on screen: any key closes
    bool mStdinPristine = true; // input zone not edited by the user yet
    bool mSettingStdin = false; // guard for the pristine tracking
    bool mPositioned = false;
};

#endif // __EMSCRIPTEN__
#endif // DBS_TERMINAL_H
