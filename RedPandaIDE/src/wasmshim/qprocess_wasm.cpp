/*
 * Dev by Sufyan — WebAssembly port (Part 2).
 * Implementation of the QProcess shim declared in qprocess_wasm.h.
 * Only compiled for Emscripten builds (see RedPandaIDE/CMakeLists.txt).
 */
#ifdef __EMSCRIPTEN__

#include "qprocess_wasm.h"

QProcess::QProcess(QObject *parent)
    : QIODevice(parent)
{
}

QProcess::~QProcess() = default;

// Qt 6.8.3 quirk, worked around here: rcc-generated resource code
// (qrc_*.cpp) unconditionally calls qResourceFeatureZstd() from
// qCleanupResources, but the WASM libQt6Core — built with
// QT_FEATURE_zstd off — only exports the zlib sibling of that
// function, so every resource object fails to link. Provide the
// honest definition: 0 means "zstd-compressed resources are not
// supported", which is exactly true of this Qt build. Part 7 made
// the other half true as well: RedPandaIDE/CMakeLists.txt now
// passes --no-compress for the app's .qrc files in Emscripten
// builds — before that, rcc zstd-compressed them by default and
// every ":/…" resource read failed at runtime, which is what
// actually aborted WASM startup (see WASM-PROGRESS.md Part 7).
unsigned char qResourceFeatureZstd()
{
    return 0;
}

void QProcess::failToStart()
{
    mError = FailedToStart;
    mErrorString = tr("Processes can't be started in the web version "
                      "of Dev by Sufyan.");
    // Open the device so callers' readAll()/read() return empty data
    // instead of tripping over an unopened QIODevice.
    if (!isOpen())
        QIODevice::open(QIODeviceBase::ReadWrite);
    emit errorOccurred(mError);
}

void QProcess::start(QIODeviceBase::OpenMode)
{
    failToStart();
}

void QProcess::start(const QString &program, const QStringList &arguments,
                     QIODeviceBase::OpenMode mode)
{
    setProgram(program);
    setArguments(arguments);
    start(mode);
}

bool QProcess::startDetached(qint64 *pid)
{
    if (pid)
        *pid = 0;
    failToStart();
    return false;
}

bool QProcess::startDetached(const QString &program,
                             const QStringList &arguments,
                             const QString &workingDirectory,
                             qint64 *pid)
{
    Q_UNUSED(program);
    Q_UNUSED(arguments);
    Q_UNUSED(workingDirectory);
    if (pid)
        *pid = 0;
    return false;
}

bool QProcess::startDetached(const QString &program,
                             const QStringList &arguments)
{
    return startDetached(program, arguments, QString(), nullptr);
}

bool QProcess::waitForStarted(int)
{
    return false;
}

bool QProcess::waitForFinished(int)
{
    return true; // never running: "finished" is immediately true
}

bool QProcess::waitForReadyRead(int)
{
    return false;
}

void QProcess::terminate()
{
}

void QProcess::kill()
{
}

void QProcess::closeWriteChannel()
{
}

void QProcess::close()
{
    QIODevice::close();
}

qint64 QProcess::readData(char *, qint64)
{
    return 0; // no output, ever (EOF)
}

qint64 QProcess::writeData(const char *, qint64)
{
    return -1; // nowhere to write: report the error honestly
}

#endif // __EMSCRIPTEN__
