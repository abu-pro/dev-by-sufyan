/*
 * Dev by Sufyan — WebAssembly port (Part 2).
 *
 * Minimal QProcess stand-in for Qt for WebAssembly builds.
 *
 * Qt for WebAssembly is compiled with QT_FEATURE_process disabled, so
 * the real QProcess class does not exist at all (its declaration is
 * #if'd out of qprocess.h and libQt6Core has no QProcess symbols).
 * Process spawning is fundamentally impossible in a browser sandbox,
 * so this shim does not pretend otherwise: it provides exactly the
 * API surface the Dev by Sufyan sources use, and start() always fails
 * with QProcess::FailedToStart.
 *
 * Why this exists: the debugger clients, the compiler's (bypassed)
 * native runCommand(), the executable runner and the Rooms "Run All"
 * state machine all reference QProcess. On the web build the real
 * work is rerouted to the online /api/run backend (see
 * compiler/onlinebackend.h) and the debugger is disabled in the UI,
 * so the shim only has to keep those translation units compiling and
 * failing gracefully if ever reached — never silently "succeeding".
 *
 * This header is only included from __EMSCRIPTEN__-guarded include
 * sites and the translation unit is only added to the build by CMake
 * for EMSCRIPTEN, so native builds never see it. The whole file is
 * additionally wrapped in #ifdef __EMSCRIPTEN__ for safety.
 */
#ifndef DBS_WASM_QPROCESS_SHIM_H
#define DBS_WASM_QPROCESS_SHIM_H

#ifdef __EMSCRIPTEN__

#include <QIODevice>
#include <QProcess> // for QProcessEnvironment (which DOES exist on WASM)
#include <QString>
#include <QStringList>

// NOTE: this intentionally shadows the (absent) Qt class name so the
// existing sources compile unmodified beyond their include line.
class QProcess : public QIODevice
{
    Q_OBJECT
public:
    enum ProcessError {
        FailedToStart,
        Crashed,
        Timedout,
        ReadError,
        WriteError,
        UnknownError
    };
    Q_ENUM(ProcessError)

    enum ProcessState {
        NotRunning,
        Starting,
        Running
    };
    Q_ENUM(ProcessState)

    enum ExitStatus {
        NormalExit,
        CrashExit
    };
    Q_ENUM(ExitStatus)

    enum ProcessChannel {
        StandardOutput,
        StandardError
    };

    enum ProcessChannelMode {
        SeparateChannels,
        MergedChannels,
        ForwardedChannels,
        ForwardedOutputChannel,
        ForwardedErrorChannel
    };

    enum InputChannelMode {
        ManagedInputChannel,
        ForwardedInputChannel
    };

    explicit QProcess(QObject *parent = nullptr);
    ~QProcess() override;

    // -- configuration (stored, never acted on) ----------------------
    void setProgram(const QString &program) { mProgram = program; }
    QString program() const { return mProgram; }
    void setArguments(const QStringList &arguments) { mArguments = arguments; }
    QStringList arguments() const { return mArguments; }
    void setWorkingDirectory(const QString &dir) { mWorkingDirectory = dir; }
    QString workingDirectory() const { return mWorkingDirectory; }
    void setProcessEnvironment(const QProcessEnvironment &environment)
    { mEnvironment = environment; }
    QProcessEnvironment processEnvironment() const { return mEnvironment; }
    void setProcessChannelMode(ProcessChannelMode mode) { mChannelMode = mode; }
    ProcessChannelMode processChannelMode() const { return mChannelMode; }
    void setReadChannel(ProcessChannel channel) { mReadChannel = channel; }
    ProcessChannel readChannel() const { return mReadChannel; }
    void setInputChannelMode(InputChannelMode mode) { mInputChannelMode = mode; }
    InputChannelMode inputChannelMode() const { return mInputChannelMode; }
    void setNativeArguments(const QString &) {}
    QString nativeArguments() const { return QString(); }

    // -- execution control -------------------------------------------
    void start(QIODeviceBase::OpenMode mode = QIODeviceBase::ReadWrite);
    void start(const QString &program, const QStringList &arguments,
               QIODeviceBase::OpenMode mode = QIODeviceBase::ReadWrite);
    bool startDetached(qint64 *pid = nullptr);
    static bool startDetached(const QString &program,
                              const QStringList &arguments,
                              const QString &workingDirectory,
                              qint64 *pid = nullptr);
    static bool startDetached(const QString &program,
                              const QStringList &arguments = QStringList());

    bool waitForStarted(int msecs = 30000);
    bool waitForFinished(int msecs = 30000);
    bool waitForReadyRead(int msecs = 30000);

    ProcessState state() const { return NotRunning; }
    ProcessError error() const { return mError; }
    QString errorString() const { return mErrorString; }
    int exitCode() const { return -1; }
    ExitStatus exitStatus() const { return CrashExit; }
    qint64 processId() const { return 0; }

    void terminate();
    void kill();
    void closeWriteChannel();
    void closeReadChannel(ProcessChannel) {}
    void close();

    QByteArray readAllStandardOutput() { return QByteArray(); }
    QByteArray readAllStandardError() { return QByteArray(); }

    // QIODevice overrides
    qint64 bytesToWrite() const override { return 0; }
    bool isSequential() const override { return true; }
    bool atEnd() const override { return true; }

signals:
    void started();
    void finished(int exitCode, QProcess::ExitStatus exitStatus);
    void errorOccurred(QProcess::ProcessError error);
    void stateChanged(QProcess::ProcessState newState);
    void readyReadStandardOutput();
    void readyReadStandardError();

protected:
    qint64 readData(char *data, qint64 maxSize) override;
    qint64 writeData(const char *data, qint64 maxSize) override;

private:
    void failToStart();

    QString mProgram;
    QStringList mArguments;
    QString mWorkingDirectory;
    QProcessEnvironment mEnvironment;
    ProcessChannelMode mChannelMode = SeparateChannels;
    ProcessChannel mReadChannel = StandardOutput;
    InputChannelMode mInputChannelMode = ManagedInputChannel;
    ProcessError mError = UnknownError;
    QString mErrorString;
};

#endif // __EMSCRIPTEN__

#endif // DBS_WASM_QPROCESS_SHIM_H
