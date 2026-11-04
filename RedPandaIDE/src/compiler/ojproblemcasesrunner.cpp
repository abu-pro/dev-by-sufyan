/*
 * Copyright (C) 2020-2022 Roy Qu (royqh1979@gmail.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "ojproblemcasesrunner.h"
#include "../utils.h"
#include "../settings.h"
#include "../systemconsts.h"
#include <QElapsedTimer>
#include <QProcess>
#ifdef __EMSCRIPTEN__
#include "../wasmshim/qprocess_wasm.h" // QProcess is absent in Qt for WebAssembly
#include "onlinebackend.h"
#include <QPointer>
#endif
#ifdef Q_OS_WINDOWS
#include <psapi.h>
#endif


OJProblemCasesRunner::OJProblemCasesRunner(const QString& filename, const QStringList& arguments, const QString& workDir,
                                           const QVector<POJProblemCase>& problemCases, QObject *parent):
    Runner(filename,arguments,workDir,parent),
    mExecTimeout(0),
    mMemoryLimit(0)
{
    mProblemCases = problemCases;
    mBufferSize = 8192;
    mOutputRefreshTime = 1000;
    setWaitForFinishTime(100);
}

OJProblemCasesRunner::OJProblemCasesRunner(const QString& filename, const QStringList& arguments, const QString& workDir,
                                           POJProblemCase problemCase, QObject *parent):
    Runner(filename,arguments,workDir,parent),
    mExecTimeout(0),
    mMemoryLimit(0)
{
    mProblemCases.append(problemCase);
    mBufferSize = 8192;
    mOutputRefreshTime = 1000;
    setWaitForFinishTime(100);
}

void OJProblemCasesRunner::runCase(int index,POJProblemCase problemCase)
{
    emit caseStarted(problemCase->id(),index, mProblemCases.count());
    auto action = finally([this,&index, &problemCase]{
        emit caseFinished(problemCase->id(), index, mProblemCases.count());
    });
    QProcess process;
    bool errorOccurred = false;
    QByteArray readed;
    QByteArray buffer;
    QByteArray output;
    int noOutputTime = 0;
    QElapsedTimer elapsedTimer;
    bool execTimeouted = false;
    process.setProgram(mFilename);
    process.setArguments(mArguments);
    process.setWorkingDirectory(mWorkDir);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString path = env.value("PATH");
    QStringList pathAdded;
    bool writeChannelClosed = false;
    if (pSettings->compilerSets().defaultSet()) {
        foreach(const QString& dir, pSettings->compilerSets().defaultSet()->binDirs()) {
            pathAdded.append(dir);
        }
    }
    pathAdded.append(pSettings->dirs().appDir());
    if (!path.isEmpty()) {
        path= pathAdded.join(PATH_SEPARATOR) + PATH_SEPARATOR + path;
    } else {
        path = pathAdded.join(PATH_SEPARATOR);
    }
    env.insert("PATH",path);
    process.setProcessEnvironment(env);
    if (pSettings->executor().redirectStderrToToolLog()) {
        emit logStderrOutput("\n"+tr("--- stderr from %1 ---").arg(problemCase->name()+"\n"));
    } else {
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.setReadChannel(QProcess::StandardOutput);
    }
    process.connect(
                &process, &QProcess::errorOccurred,
                [&](){
        errorOccurred= true;
    });
    problemCase->output.clear();
    process.start();
    process.waitForStarted(5000);
#ifdef Q_OS_WIN
    HANDLE hProcess = NULL;
    if (process.processId()!=0) {
        hProcess = OpenProcess(PROCESS_ALL_ACCESS,FALSE,process.processId());
    }
#endif
    if (process.state()==QProcess::Running) {
        if (fileExists(problemCase->inputFileName()))
            process.write(readFileToByteArray(problemCase->inputFileName()));
        else
            process.write(problemCase->input().toLocal8Bit());
        process.waitForFinished(0);
    }

    elapsedTimer.start();
    while (true) {
        if (process.bytesToWrite()==0 && !writeChannelClosed) {
            writeChannelClosed = true;
            process.closeWriteChannel();
        }
        process.waitForFinished(mWaitForFinishTime);
        if (process.state()!=QProcess::Running) {
            break;
        }
        if (mExecTimeout>0) {
            int msec = elapsedTimer.elapsed();
            if (msec>mExecTimeout) {
                execTimeouted=true;
            }
        }
        if (mStop || execTimeouted) {
            process.terminate();
            process.kill();
            break;
        }
        if (errorOccurred)
            break;
        if (pSettings->executor().redirectStderrToToolLog()) {
            QString s = QString::fromLocal8Bit(process.readAllStandardError());
            if (!s.isEmpty())
                emit logStderrOutput(s);
        }
        readed = process.read(mBufferSize);
        buffer += readed;
        if (buffer.length()>=mBufferSize || noOutputTime > mOutputRefreshTime) {
            if (!buffer.isEmpty()) {
                emit newOutputGetted(problemCase->id(),QString::fromLocal8Bit(buffer));
                output.append(buffer);
                buffer.clear();
            }
            noOutputTime = 0;
        } else {
            noOutputTime += mWaitForFinishTime;
        }
    }
    problemCase->runningTime=elapsedTimer.elapsed();
    problemCase->runningMemory = 0;
#ifdef Q_OS_WIN
    if (hProcess!=NULL) {
        PROCESS_MEMORY_COUNTERS counter{0};
        counter.cb = sizeof(counter);
        if (GetProcessMemoryInfo(hProcess,&counter,
                                 sizeof(counter))){
            problemCase->runningMemory = counter.PeakPagefileUsage;
        }
        FILETIME creationTime;
        FILETIME exitTime;
        FILETIME kernelTime;
        FILETIME userTime;
        if (GetProcessTimes(hProcess,&creationTime,&exitTime,&kernelTime,&userTime)) {
            LONGLONG t=((LONGLONG)kernelTime.dwHighDateTime<<32)
                    +((LONGLONG)userTime.dwHighDateTime<<32)
                    +(kernelTime.dwLowDateTime)+(userTime.dwLowDateTime);
            problemCase->runningTime=(double)t/10000;
        }
    }
#endif
    if (execTimeouted) {
        problemCase->output = tr("Time limit exceeded!");
        emit resetOutput(problemCase->id(), problemCase->output);
    } else if (mMemoryLimit>0 && problemCase->runningMemory>mMemoryLimit) {
        problemCase->output = tr("Memory limit exceeded!");
        emit resetOutput(problemCase->id(), problemCase->output);
    } else {
        if (pSettings->executor().redirectStderrToToolLog()) {
            QString s = QString::fromLocal8Bit(process.readAllStandardError());
            if (!s.isEmpty())
                emit logStderrOutput(s);
        }
        if (process.state() == QProcess::ProcessState::NotRunning)
            buffer += process.readAll();
        emit newOutputGetted(problemCase->id(),QString::fromLocal8Bit(buffer));
        output.append(buffer);
        problemCase->output = QString::fromLocal8Bit(output);

        if (errorOccurred) {
            //qDebug()<<"process error:"<<process.error();
            switch (process.error()) {
            case QProcess::FailedToStart:
                emit runErrorOccurred(tr("The runner process '%1' failed to start.").arg(mFilename));
                break;
    //        case QProcess::Crashed:
    //            if (!mStop)
    //                emit runErrorOccurred(tr("The runner process crashed after starting successfully."));
    //            break;
            case QProcess::Timedout:
                emit runErrorOccurred(tr("The last waitFor...() function timed out."));
                break;
            case QProcess::WriteError:
                emit runErrorOccurred(tr("An error occurred when attempting to write to the runner process."));
                break;
            case QProcess::ReadError:
                emit runErrorOccurred(tr("An error occurred when attempting to read from the runner process."));
                break;
            default:
                break;
            }
        }
    }
}

void OJProblemCasesRunner::run()
{
#if defined(__EMSCRIPTEN__) && defined(DBS_WASM_ST)
    // Part 6, single-thread build: no finally guard — terminated()
    // is emitted when the asynchronous case chain finishes (or at
    // once on a setup failure), not when this function returns.
    {
        const QString sourceFile = dbsOnlineFindSourceForExecutable(mFilename);
        if (sourceFile.isEmpty()) {
            emit runErrorOccurred(tr("Couldn't find the source file for \"%1\".")
                                  .arg(mFilename));
            emit terminated();
            return;
        }
        mOnlineLanguage = dbsOnlineLanguageForFilename(sourceFile);
        mOnlineCode = QString::fromUtf8(readFileToByteArray(sourceFile));
        if (mProblemCases.isEmpty()) {
            emit terminated();
            return;
        }
        runOnlineCaseAsync(0);
    }
    return;
#endif
    auto action = finally([this]{
        emit terminated();
    });
#ifdef __EMSCRIPTEN__
    // Web build: no local process exists to run the cases with.
    runOnlineCases();
    return;
#endif
    for (int i=0; i < mProblemCases.size(); i++) {
        if (mStop)
            break;
        POJProblemCase problemCase = mProblemCases[i];
        runCase(i,problemCase);
    }
}

#ifdef __EMSCRIPTEN__
// Web build: each case is one /api/run round trip with the case's
// input as stdin. The same signals the native per-case process flow
// emits (caseStarted / newOutputGetted / resetOutput / caseFinished)
// are emitted here, and problemCase->output / runningTime are filled
// the same way, so MainWindow's validation works unchanged. Limits
// differ honestly from native: the per-case exec timeout and memory
// limit settings can't be enforced around a synchronous request —
// the server caps execution itself (~30 s, Judge0 limits) and
// reports time-limit failures in the result's stderr.
void OJProblemCasesRunner::runOnlineCases()
{
    const QString sourceFile = dbsOnlineFindSourceForExecutable(mFilename);
    if (sourceFile.isEmpty()) {
        emit runErrorOccurred(tr("Couldn't find the source file for \"%1\".")
                              .arg(mFilename));
        return;
    }
    const QString language = dbsOnlineLanguageForFilename(sourceFile);
    const QString code = QString::fromUtf8(
                readFileToByteArray(sourceFile));
    for (int i = 0; i < mProblemCases.size(); i++) {
        if (mStop)
            break;
        POJProblemCase problemCase = mProblemCases[i];
        emit caseStarted(problemCase->id(), i, mProblemCases.count());
        const QString input = onlineCaseInput(problemCase);
        problemCase->output.clear();
        const DbsOnlineRunResult result = dbsOnlineRunCode(
                    language, code, input);
        applyOnlineCaseResult(problemCase, result);
        emit caseFinished(problemCase->id(), i, mProblemCases.count());
    }
}

QString OJProblemCasesRunner::onlineCaseInput(
        const POJProblemCase &problemCase)
{
    if (fileExists(problemCase->inputFileName()))
        return QString::fromUtf8(
                    readFileToByteArray(problemCase->inputFileName()));
    return problemCase->input();
}

void OJProblemCasesRunner::applyOnlineCaseResult(
        const POJProblemCase &problemCase,
        const DbsOnlineRunResult &result)
{
    problemCase->runningTime =
            static_cast<qulonglong>(result.timeMs);
    problemCase->runningMemory = 0;
    if (!result.networkOk) {
        problemCase->output = result.networkError;
        emit resetOutput(problemCase->id(), problemCase->output);
    } else if (result.stage == QLatin1String("compile")
               || result.stage == QLatin1String("input")) {
        problemCase->output = result.stdErr;
        emit resetOutput(problemCase->id(), problemCase->output);
    } else {
        QString output = result.stdOut;
        if (pSettings->executor().redirectStderrToToolLog()) {
            if (!result.stdErr.isEmpty())
                emit logStderrOutput(result.stdErr);
        } else {
            output += result.stdErr;
        }
        problemCase->output = output;
        emit newOutputGetted(problemCase->id(), output);
    }
}

#ifdef DBS_WASM_ST
// Part 6, single-thread build: the asynchronous twin of
// runOnlineCases() — identical per-case behaviour, but the cases
// are chained through fetch replies on the main thread instead of
// blocking a worker thread on synchronous requests.
void OJProblemCasesRunner::runOnlineCaseAsync(int index)
{
    POJProblemCase problemCase = mProblemCases[index];
    emit caseStarted(problemCase->id(), index, mProblemCases.count());
    const QString input = onlineCaseInput(problemCase);
    problemCase->output.clear();
    QPointer<OJProblemCasesRunner> self(this);
    dbsOnlineRunCodeAsync(mOnlineLanguage, mOnlineCode, input,
        [self, index](const DbsOnlineRunResult &result) {
            if (self)
                self->handleOnlineCaseResult(index, result);
        });
}

void OJProblemCasesRunner::handleOnlineCaseResult(
        int index, const DbsOnlineRunResult &result)
{
    POJProblemCase problemCase = mProblemCases[index];
    applyOnlineCaseResult(problemCase, result);
    emit caseFinished(problemCase->id(), index, mProblemCases.count());
    const int next = index + 1;
    if (mStop || next >= mProblemCases.count()) {
        emit terminated();
        return;
    }
    runOnlineCaseAsync(next);
}
#endif // DBS_WASM_ST
#endif // __EMSCRIPTEN__

int OJProblemCasesRunner::execTimeout() const
{
    return mExecTimeout;
}

void OJProblemCasesRunner::setExecTimeout(int newExecTimeout)
{
    mExecTimeout = newExecTimeout;
}

void OJProblemCasesRunner::setMemoryLimit(size_t limit)
{
    mMemoryLimit = limit;
}

int OJProblemCasesRunner::waitForFinishTime() const
{
    return mWaitForFinishTime;
}

void OJProblemCasesRunner::setWaitForFinishTime(int newWaitForFinishTime)
{
    mWaitForFinishTime = newWaitForFinishTime;
}

int OJProblemCasesRunner::outputRefreshTime() const
{
    return mOutputRefreshTime;
}

void OJProblemCasesRunner::setOutputRefreshTime(int newOutputRefreshTime)
{
    mOutputRefreshTime = newOutputRefreshTime;
}

int OJProblemCasesRunner::bufferSize() const
{
    return mBufferSize;
}

void OJProblemCasesRunner::setBufferSize(int newBufferSize)
{
    mBufferSize = newBufferSize;
}


