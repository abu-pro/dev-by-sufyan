/*
 * Dev by Sufyan — WebAssembly port (Part 2).
 *
 * Online compile & run backend for the web (Emscripten) build.
 *
 * A browser tab cannot spawn processes, so on the web build Compile,
 * Run and the OJ problem-case runner no longer drive a local gcc /
 * console pauser. Instead they POST to the same /api/run endpoint the
 * Dev by Sufyan web client already uses (see
 * projects/dev-by-sufyan/server.js):
 *
 *   request : POST /api/run  (same origin, relative URL)
 *             { "language": "cpp" | "c" | "python",
 *               "code": "<source>", "stdin": "<stdin text>",
 *               "pref": "online" }
 *   response: { "ok": bool,
 *               "stage": "compile" | "run" | "input",
 *               "stdout": "...", "stderr": "...",
 *               "code": <exit code>, "timeMs": <ms>,
 *               "engine": "online" }
 *
 * The endpoint compiles AND runs in one round trip (Judge0 behind the
 * server). stage == "compile" means compilation failed and stderr
 * holds the compiler diagnostics; stage == "run" means the program
 * ran and stdout/stderr are the program's own streams.
 *
 * This file is only added to the build for EMSCRIPTEN (see
 * RedPandaIDE/CMakeLists.txt); native builds never see it.
 */
#ifndef DBS_ONLINEBACKEND_H
#define DBS_ONLINEBACKEND_H

#include "runner.h"
#include <QString>
#include <functional>

struct DbsOnlineRunResult
{
    bool networkOk = false;   // false: the request itself failed
    bool ok = false;          // server's "ok" (compiled + exit code 0)
    QString stage;            // "compile" | "run" | "input"
    QString stdOut;
    QString stdErr;
    int exitCode = -1;
    qint64 timeMs = 0;
    QString networkError;     // human-readable, set when !networkOk
};
Q_DECLARE_METATYPE(DbsOnlineRunResult)

// Blocking POST to /api/run. Implemented with a SYNCHRONOUS
// emscripten_fetch, which is only legal off the browser main thread —
// every caller is a QThread::run() (Compiler, ExecutableRunner
// replacement, OJProblemCasesRunner), i.e. a pthread under the
// multithreaded WASM kit. Never call this from the GUI thread.
DbsOnlineRunResult dbsOnlineRunCode(const QString &language,
                                    const QString &code,
                                    const QString &stdinText);

#ifdef DBS_WASM_ST
// Part 6 — single-threaded WASM variant (no pthreads, no
// SharedArrayBuffer): the synchronous fetch above can never run here
// (it is only legal on a worker thread, and this build has none), so
// Compile / Run / OJ cases use this ASYNCHRONOUS variant instead. It
// posts the exact same /api/run request from the browser main thread
// and invokes the callback with the parsed result when the reply
// arrives (the callback runs on the main thread, inside the Qt event
// loop). Only compiled under DBS_WASM_ST; the multithread build keeps
// using the synchronous function above, unchanged.
void dbsOnlineRunCodeAsync(const QString &language,
                           const QString &code,
                           const QString &stdinText,
                           std::function<void(const DbsOnlineRunResult &)> callback);
#endif // DBS_WASM_ST

// "cpp" / "c" / "python" for files the backend accepts, "" otherwise.
QString dbsOnlineLanguageForFilename(const QString &filename);

// The fixed file name the server compiles under ("main.cpp" etc.) —
// compiler diagnostics reference this name, callers rewrite it to the
// user's real file name before parsing issues.
QString dbsOnlineServerFilename(const QString &language);

// The native flow hands the runner the *executable* path, not the
// source. Recover the source by trying the compilable extensions next
// to it (the IDE always writes the executable beside the source).
// Returns "" when no candidate exists.
QString dbsOnlineFindSourceForExecutable(const QString &exePath);

// Runner replacement used by CompilerManager::run() on the web build:
// runs the program online and surfaces its output through the
// outputReceived signal (CompilerManager connects it to
// MainWindow::logToolsOutput, the IDE's own bottom output panel —
// the native external-console flow has no browser equivalent).
class OnlineExecutableRunner : public Runner
{
    Q_OBJECT
public:
    OnlineExecutableRunner(const QString &filename,
                           const QString &workDir,
                           const QString &language,
                           const QString &code,
                           const QString &stdinText,
                           QObject *parent = nullptr);

signals:
    void outputReceived(const QString &text);
    // Part 9: the structured result, for the terminal window —
    // emitted alongside outputReceived in the two output paths of
    // handleOnlineResult() (never for network failures, which keep
    // using runErrorOccurred).
    void dbsResultReceived(const DbsOnlineRunResult &result);

protected:
    void run() override;
    void doStop() override;

private:
    // Surfaces one backend result (program output / compile failure /
    // network failure) through outputReceived / runErrorOccurred.
    // Shared by the multithread run() (result of the synchronous
    // fetch) and the single-thread async continuation.
    void handleOnlineResult(const DbsOnlineRunResult &result);

private:
    QString mLanguage;
    QString mCode;
    QString mStdinText;
    bool mAborted = false;
};

#endif // DBS_ONLINEBACKEND_H
