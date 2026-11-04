/*
 * Dev by Sufyan — WebAssembly port (Part 2).
 * Implementation of onlinebackend.h. Only compiled for Emscripten
 * builds (see RedPandaIDE/CMakeLists.txt).
 */
#include "onlinebackend.h"

#include "../utils/file.h"
#include "qt_utils/utils.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>

#include <emscripten/fetch.h>

#include <cstring>
#include <string>

// The /api/run request body, shared by the synchronous (multithread)
// and asynchronous (single-thread, Part 6) fetches below.
static QByteArray dbsOnlineRequestBody(const QString &language,
                                       const QString &code,
                                       const QString &stdinText)
{
    QJsonObject request;
    request["language"] = language;
    request["code"] = code;
    request["stdin"] = stdinText;
    // "online": the deployed server has no local compilers either, so
    // go straight to the Judge0-backed path instead of relying on the
    // server's ENOENT fallback.
    request["pref"] = QStringLiteral("online");
    return QJsonDocument(request).toJson(QJsonDocument::Compact);
}

// Interpret a finished fetch (either flavour) exactly the way the
// original synchronous implementation did.
static DbsOnlineRunResult dbsOnlineParseFetch(emscripten_fetch_t *fetch)
{
    DbsOnlineRunResult result;
    if (fetch->status == 200 && fetch->data) {
        const QByteArray payload(fetch->data,
                                 static_cast<int>(fetch->numBytes));
        const QJsonObject root = QJsonDocument::fromJson(payload).object();
        if (!root.isEmpty()) {
            result.networkOk = true;
            result.ok = root.value("ok").toBool();
            result.stage = root.value("stage").toString();
            result.stdOut = root.value("stdout").toString();
            result.stdErr = root.value("stderr").toString();
            result.exitCode = root.value("code").toInt(-1);
            result.timeMs = root.value("timeMs").toVariant().toLongLong();
        } else {
            result.networkError = QObject::tr(
                        "The compile server sent an unreadable reply.");
        }
    } else if (fetch->status == 0) {
        result.networkError = QObject::tr(
                    "Could not reach the compile server. "
                    "Check the internet connection and try again.");
    } else {
        result.networkError = QObject::tr(
                    "The compile server answered with an error (HTTP %1).")
                .arg(fetch->status);
    }
    return result;
}

DbsOnlineRunResult dbsOnlineRunCode(const QString &language,
                                    const QString &code,
                                    const QString &stdinText)
{
#ifdef DBS_WASM_ST
    // Safety net (Part 6): a synchronous fetch is only legal on a
    // pthread worker thread, and the single-thread build has none —
    // calling it here would freeze the browser tab. Every real
    // caller in this build uses dbsOnlineRunCodeAsync() instead; if
    // one is ever missed, fail honestly instead of hanging.
    Q_UNUSED(language)
    Q_UNUSED(code)
    Q_UNUSED(stdinText)
    DbsOnlineRunResult result;
    result.networkError = QObject::tr(
                "This action needs the internet compiler, which is "
                "not wired for the single-thread web build here.");
    return result;
#else
    const QByteArray body = dbsOnlineRequestBody(language, code, stdinText);

    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    std::strcpy(attr.requestMethod, "POST");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY
            | EMSCRIPTEN_FETCH_SYNCHRONOUS;
    const char *headers[] = {"Content-Type", "application/json", nullptr};
    attr.requestHeaders = headers;
    attr.requestData = body.constData();
    attr.requestDataSize = static_cast<size_t>(body.size());

    // Relative URL: the WASM page is served same-origin with /api/run.
    emscripten_fetch_t *fetch = emscripten_fetch(&attr, "/api/run");
    if (!fetch) {
        DbsOnlineRunResult result;
        result.networkError = QObject::tr(
                    "Could not start the request to the compile server.");
        return result;
    }

    const DbsOnlineRunResult result = dbsOnlineParseFetch(fetch);
    emscripten_fetch_close(fetch);
    return result;
#endif // DBS_WASM_ST
}

#ifdef DBS_WASM_ST
namespace {
// Everything an in-flight async fetch must keep alive until its
// callback runs: the request body and header strings (the runtime
// may reference them for the whole request) and the completion
// callback itself. Deleted by the callback, exactly once.
struct DbsAsyncFetchContext
{
    std::string body;
    std::string headerName{"Content-Type"};
    std::string headerValue{"application/json"};
    const char *headers[3] = {nullptr, nullptr, nullptr};
    std::function<void(const DbsOnlineRunResult &)> callback;
};

void dbsAsyncFetchDone(DbsAsyncFetchContext *ctx,
                       const DbsOnlineRunResult &result)
{
    auto callback = std::move(ctx->callback);
    delete ctx;
    if (callback)
        callback(result);
}

void dbsAsyncFetchSuccess(emscripten_fetch_t *fetch)
{
    auto *ctx = static_cast<DbsAsyncFetchContext *>(fetch->userData);
    const DbsOnlineRunResult result = dbsOnlineParseFetch(fetch);
    emscripten_fetch_close(fetch);
    dbsAsyncFetchDone(ctx, result);
}

void dbsAsyncFetchError(emscripten_fetch_t *fetch)
{
    auto *ctx = static_cast<DbsAsyncFetchContext *>(fetch->userData);
    DbsOnlineRunResult result;
    if (fetch->status == 200 && fetch->data) {
        // Paranoia: a completed 200 reply belongs to the success
        // interpretation, whichever callback the runtime picked.
        result = dbsOnlineParseFetch(fetch);
    } else if (fetch->status > 0) {
        result.networkError = QObject::tr(
                    "The compile server answered with an error (HTTP %1).")
                .arg(fetch->status);
    } else {
        result.networkError = QObject::tr(
                    "Could not reach the compile server. "
                    "Check the internet connection and try again.");
    }
    emscripten_fetch_close(fetch);
    dbsAsyncFetchDone(ctx, result);
}
} // namespace

void dbsOnlineRunCodeAsync(const QString &language,
                           const QString &code,
                           const QString &stdinText,
                           std::function<void(const DbsOnlineRunResult &)> callback)
{
    const QByteArray body = dbsOnlineRequestBody(language, code, stdinText);

    auto *ctx = new DbsAsyncFetchContext;
    ctx->body.assign(body.constData(),
                     static_cast<size_t>(body.size()));
    ctx->headers[0] = ctx->headerName.c_str();
    ctx->headers[1] = ctx->headerValue.c_str();
    ctx->headers[2] = nullptr;
    ctx->callback = std::move(callback);

    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    std::strcpy(attr.requestMethod, "POST");
    // NOTE: no EMSCRIPTEN_FETCH_SYNCHRONOUS — this build has no
    // worker thread to block; the reply arrives through the browser
    // event loop and onsuccess/onerror run on the main thread.
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.requestHeaders = ctx->headers;
    attr.requestData = ctx->body.data();
    attr.requestDataSize = ctx->body.size();
    attr.userData = ctx;
    attr.onsuccess = &dbsAsyncFetchSuccess;
    attr.onerror = &dbsAsyncFetchError;

    // Relative URL: the WASM page is served same-origin with /api/run.
    if (!emscripten_fetch(&attr, "/api/run")) {
        DbsOnlineRunResult result;
        result.networkError = QObject::tr(
                    "Could not start the request to the compile server.");
        dbsAsyncFetchDone(ctx, result);
    }
}
#endif // DBS_WASM_ST

QString dbsOnlineLanguageForFilename(const QString &filename)
{
    const QString suffix = QFileInfo(filename).suffix().toLower();
    if (suffix == "cpp" || suffix == "cc" || suffix == "cxx"
            || suffix == "c++")
        return QStringLiteral("cpp");
    if (suffix == "c")
        return QStringLiteral("c");
    if (suffix == "py")
        return QStringLiteral("python");
    return QString();
}

QString dbsOnlineServerFilename(const QString &language)
{
    if (language == "cpp")
        return QStringLiteral("main.cpp");
    if (language == "c")
        return QStringLiteral("main.c");
    if (language == "python")
        return QStringLiteral("main.py");
    return QString();
}

QString dbsOnlineFindSourceForExecutable(const QString &exePath)
{
    const QFileInfo info(exePath);
    const QString stem = info.absolutePath()
            + QLatin1Char('/') + info.completeBaseName();
    static const char *candidates[] = {".cpp", ".cc", ".cxx", ".c", ".py"};
    for (const char *ext : candidates) {
        const QString candidate = stem + QLatin1String(ext);
        if (fileExists(candidate))
            return candidate;
    }
    return QString();
}

OnlineExecutableRunner::OnlineExecutableRunner(
        const QString &filename, const QString &workDir,
        const QString &language, const QString &code,
        const QString &stdinText, QObject *parent)
    : Runner(filename, QStringList(), workDir, parent),
      mLanguage(language),
      mCode(code),
      mStdinText(stdinText)
{
    // Part 9: dbsResultReceived crosses from this runner thread
    // to the GUI thread (queued) — register the payload type once.
    qRegisterMetaType<DbsOnlineRunResult>("DbsOnlineRunResult");
}

void OnlineExecutableRunner::run()
{
#ifdef DBS_WASM_ST
    // Part 6, single-thread build: no worker thread exists to block
    // on a synchronous fetch (and QThread::finished fires the moment
    // this function returns — CompilerManager's single-thread
    // connections key off terminated() instead). Start the async
    // request and let its main-thread continuation surface the
    // result and emit terminated() at the true end of the run.
    QPointer<OnlineExecutableRunner> self(this);
    dbsOnlineRunCodeAsync(mLanguage, mCode, mStdinText,
        [self](const DbsOnlineRunResult &result) {
            if (!self)
                return; // runner was discarded meanwhile
            if (!self->mAborted)
                self->handleOnlineResult(result);
            emit self->terminated();
        });
    return;
#endif // DBS_WASM_ST
    auto action = finally([this] {
        emit terminated();
    });
    const DbsOnlineRunResult result = dbsOnlineRunCode(
                mLanguage, mCode, mStdinText);
    handleOnlineResult(result);
}

void OnlineExecutableRunner::handleOnlineResult(
        const DbsOnlineRunResult &result)
{
    if (!result.networkOk) {
        emit runErrorOccurred(
                    tr("The online run failed: %1").arg(result.networkError));
        return;
    }
    if (result.stage == QLatin1String("compile")
            || result.stage == QLatin1String("input")) {
        // E.g. a stale placeholder executable whose source no longer
        // compiles. Show the diagnostics as output text instead of
        // popping a "Run Failed" box for a compile problem.
        QString text = result.stdErr;
        if (!result.stdOut.isEmpty()) {
            if (!text.isEmpty())
                text += QLatin1Char('\n');
            text += result.stdOut;
        }
        text += tr("\n— Compilation failed (online compiler) —");
        emit dbsResultReceived(result);
        emit outputReceived(text);
        return;
    }
    // Mirror the web client's result presentation.
    emit dbsResultReceived(result);
    QString text = result.stdOut;
    if (!result.stdErr.isEmpty()) {
        if (!text.isEmpty() && !text.endsWith(QLatin1Char('\n')))
            text += QLatin1Char('\n');
        text += result.stdErr;
    }
    if (!text.isEmpty() && !text.endsWith(QLatin1Char('\n')))
        text += QLatin1Char('\n');
    text += tr("— Program finished, exit code %1, %2 ms · online compiler —")
            .arg(result.exitCode).arg(result.timeMs);
    emit outputReceived(text);
}

void OnlineExecutableRunner::doStop()
{
    // The fetch is synchronous and cannot be interrupted mid-flight;
    // it is bounded by the server (~30 s worst case). The flag only
    // documents intent — CompilerManager disconnects and drops its
    // pointer exactly like the native stop path.
    mAborted = true;
}
