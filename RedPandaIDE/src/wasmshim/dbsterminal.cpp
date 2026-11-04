/*
 * Dev by Sufyan — WebAssembly port (Part 9). See dbsterminal.h
 * for the design notes. Compiled only under __EMSCRIPTEN__.
 */
#ifdef __EMSCRIPTEN__

#include "dbsterminal.h"

#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace {
// Console palette (Windows console on #0C0C0C).
const QColor kText(0xCC, 0xCC, 0xCC);
const QColor kDim(0x8A, 0x8A, 0x8A);
const QColor kError(0xFF, 0x9E, 0x99);   // soft red for stderr
const QColor kCommand(0xFF, 0xFF, 0xFF);
} // namespace

DbsTerminalWindow::DbsTerminalWindow(QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setAttribute(Qt::WA_DeleteOnClose, true);
    setWindowTitle(tr("Terminal"));
    resize(740, 480);

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
    auto *inputLabel = new QLabel(
                tr("Input\n(stdin):"), this);
    inputLabel->setStyleSheet(QStringLiteral("color: #8A8A8A;"));
    inputRow->addWidget(inputLabel);

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
    setWindowTitle(tr("%1 — Terminal").arg(displayName));
    if (!mPositioned && parentWidget()) {
        mPositioned = true;
        const QRect host = parentWidget()->geometry();
        move(host.center() - rect().center());
    }
}

void DbsTerminalWindow::beginRerun(const QString &displayName)
{
    Q_UNUSED(displayName);
    mPending = true;
    mFinished = false;
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
    if (!mStdinPristine)
        return; // the user's own input wins over a later prefill
    mSettingStdin = true;
    mInput->setPlainText(text);
    mSettingStdin = false;
    mStdinPristine = true;
}

QString DbsTerminalWindow::stdinText() const
{
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
    finishWithClosingLine(tr(
        "— Compilation failed — the errors are listed in the "
        "Compiler Output panel — press any key or close this "
        "window —"));
}

void DbsTerminalWindow::showErrorLine(const QString &text)
{
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

#endif // __EMSCRIPTEN__
