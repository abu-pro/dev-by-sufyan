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
#include "compilermanager.h"
#include "filecompiler.h"
#include "../project.h"
#ifdef ENABLE_SDCC
#include "sdccfilecompiler.h"
#include "sdccprojectcompiler.h"
#endif
#include "nasmfilecompiler.h"
#include "stdincompiler.h"
#include "../mainwindow.h"
#include "../editormanager.h"
#include "executablerunner.h"
#ifdef __EMSCRIPTEN__
#include "onlinebackend.h"
#endif
#include "ojproblemcasesrunner.h"
#include "../utils.h"
#include "../utils/pe.h"
#include "../utils/parsearg.h"
#include "../utils/terminal.h"
#include "../systemconsts.h"
#include "../settings.h"
#include <QMessageBox>
#include <QUuid>
#include "projectcompiler.h"
#ifdef Q_OS_MACOS
#include <sys/posix_shm.h>
#endif

CompilerManager::CompilerManager(QObject *parent) : QObject(parent),
    mCompileMutex(),
    mBackgroundSyntaxCheckMutex(),
    mRunnerMutex()
{
    mCompiler = nullptr;
    mBackgroundSyntaxChecker = nullptr;
    mRunner = nullptr;
    mSyntaxCheckErrorCount = 0;
    mSyntaxCheckIssueCount = 0;
    mCompileErrorCount = 0;
    mCompileIssueCount = 0;
    mSyntaxCheckErrorCount = 0;
    mMainWindow = nullptr;
}

CompilerManager::~CompilerManager()
{
    stopAllRunners();
    stopCompile();
    stopCheckSyntax();
    stopRun();
}

bool CompilerManager::compiling() const
{
    QMutexLocker locker(&mCompileMutex);
    return mCompiler!=nullptr;
}

bool CompilerManager::backgroundSyntaxChecking() const
{
    QMutexLocker locker(&mBackgroundSyntaxCheckMutex);
    return mBackgroundSyntaxChecker!=nullptr;
}

bool CompilerManager::running() const
{
    QMutexLocker locker(&mRunnerMutex);
    return (mRunner!=nullptr && !mRunner->pausing());
}

void CompilerManager::compile(const QString& filename, FileType fileType, const QByteArray& encoding, bool rebuild, CppCompileType compileType)
{
#ifndef __EMSCRIPTEN__
    // Native builds refuse to work without a local compiler set.
    // The web build has no local compilers at all: single-file
    // compile/run go through the online backend instead, so this
    // gate is skipped there (Compiler::runOnlineCompile and
    // CompilerManager::run handle the web flow).
    if (!pSettings->compilerSets().defaultSet()) {
        QMessageBox::critical(mMainWindow,
                              tr("No compiler set"),
                              tr("No compiler set is configured.")+tr("Can't start debugging."));
        return;
    }
#endif
    {
        QMutexLocker locker(&mCompileMutex);
        if (mCompiler!=nullptr) {
            return;
        }
        mCompileErrorCount = 0;
        mCompileIssueCount = 0;
        //deleted when thread finished
// (Web build: defaultSet() may be null there and must not be
// dereferenced; the SDCC choice is irrelevant anyway because
// Compiler::run() reroutes every file compiler to the online
// backend — so the SDCC branch is compiled out for Emscripten.)
#if defined(ENABLE_SDCC) && !defined(__EMSCRIPTEN__)
        if (pSettings->compilerSets().defaultSet()->compilerType()==CompilerType::SDCC) {
            mCompiler = new SDCCFileCompiler(filename,encoding,compileType,false);
        } else
#endif
        if (fileType == FileType::NASM){
            mCompiler = new NASMFileCompiler(filename);
        } else  {
            mCompiler = new FileCompiler(filename,encoding,fileType,compileType,false);
        }
        mCompiler->setParserForFile(getParserForFile(filename));
        mCompiler->setRebuild(rebuild);
#if defined(__EMSCRIPTEN__) && defined(DBS_WASM_ST)
        // Part 6 (single-thread web build): QThread::finished fires
        // when run() returns — before the async online compile has
        // even answered — so the compiler must be deleted on its own
        // completion signal instead. (Same swap at every compiler /
        // runner connection site in this file.)
        connect(mCompiler, &Compiler::compileFinished, mCompiler, &QObject::deleteLater);
#else
        connect(mCompiler, &Compiler::finished, mCompiler, &QObject::deleteLater);
#endif
        connect(mCompiler, &Compiler::compileFinished, this, &CompilerManager::onCompileFinished);
        connect(mCompiler, &Compiler::compileIssue, this, &CompilerManager::onCompileIssue);
        if (mMainWindow) {
            connect(mCompiler, &Compiler::compileStarted, mMainWindow, &MainWindow::onCompileStarted);
            connect(mCompiler, &Compiler::compileStarted, mMainWindow, &MainWindow::clearToolsOutput);

            connect(mCompiler, &Compiler::compileOutput, mMainWindow, &MainWindow::logToolsOutput);
            connect(mCompiler, &Compiler::compileIssue, mMainWindow, &MainWindow::onCompileIssue);
            connect(mCompiler, &Compiler::compileErrorOccured, mMainWindow, &MainWindow::onCompileErrorOccured);
        }
        mCompiler->start();
    }
}

void CompilerManager::compileProject(std::shared_ptr<Project> project, bool rebuild)
{
#ifndef __EMSCRIPTEN__
    // Native builds refuse to work without a local compiler set.
    // The web build has no local compilers at all: single-file
    // compile/run go through the online backend instead, so this
    // gate is skipped there (Compiler::runOnlineCompile and
    // CompilerManager::run handle the web flow).
    if (!pSettings->compilerSets().defaultSet()) {
        QMessageBox::critical(mMainWindow,
                              tr("No compiler set"),
                              tr("No compiler set is configured.")+tr("Can't start debugging."));
        return;
    }
#endif
    {
        QMutexLocker locker(&mCompileMutex);
        if (mCompiler!=nullptr) {
            return;
        }
        mCompileErrorCount = 0;
        mCompileIssueCount = 0;
        //deleted when thread finished
        mCompiler = createProjectCompiler(project);
        mCompiler->setRebuild(rebuild);
#if defined(__EMSCRIPTEN__) && defined(DBS_WASM_ST)
        connect(mCompiler, &Compiler::compileFinished, mCompiler, &QObject::deleteLater);
#else
        connect(mCompiler, &Compiler::finished, mCompiler, &QObject::deleteLater);
#endif
        connect(mCompiler, &Compiler::compileFinished, this, &CompilerManager::onCompileFinished);

        connect(mCompiler, &Compiler::compileIssue, this, &CompilerManager::onCompileIssue);
        if (mMainWindow) {
            connect(mCompiler, &Compiler::compileStarted, mMainWindow, &MainWindow::onProjectCompileStarted);
            connect(mCompiler, &Compiler::compileStarted, mMainWindow, &MainWindow::clearToolsOutput);

            connect(mCompiler, &Compiler::compileOutput, mMainWindow, &MainWindow::logToolsOutput);
            connect(mCompiler, &Compiler::compileIssue, mMainWindow, &MainWindow::onCompileIssue);
            connect(mCompiler, &Compiler::compileErrorOccured, mMainWindow, &MainWindow::onCompileErrorOccured);
        }
        mCompiler->start();
    }
}

void CompilerManager::cleanProject(std::shared_ptr<Project> project)
{
#ifndef __EMSCRIPTEN__
    // Native builds refuse to work without a local compiler set.
    // The web build has no local compilers at all: single-file
    // compile/run go through the online backend instead, so this
    // gate is skipped there (Compiler::runOnlineCompile and
    // CompilerManager::run handle the web flow).
    if (!pSettings->compilerSets().defaultSet()) {
        QMessageBox::critical(mMainWindow,
                              tr("No compiler set"),
                              tr("No compiler set is configured.")+tr("Can't start debugging."));
        return;
    }
#endif
    {
        QMutexLocker locker(&mCompileMutex);
        if (mCompiler!=nullptr) {
            return;
        }
        mCompileErrorCount = 0;
        mCompileIssueCount = 0;
        //deleted when thread finished
        ProjectCompiler* compiler = createProjectCompiler(project);
        compiler->setOnlyClean(true);
        mCompiler = compiler;
        mCompiler->setRebuild(false);
#if defined(__EMSCRIPTEN__) && defined(DBS_WASM_ST)
        connect(mCompiler, &Compiler::compileFinished, mCompiler, &QObject::deleteLater);
#else
        connect(mCompiler, &Compiler::finished, mCompiler, &QObject::deleteLater);
#endif
        connect(mCompiler, &Compiler::compileFinished, this, &CompilerManager::onCompileFinished);

        connect(mCompiler, &Compiler::compileIssue, this, &CompilerManager::onCompileIssue);
        if (mMainWindow) {
            connect(mCompiler, &Compiler::compileStarted, mMainWindow, &MainWindow::onProjectCompileStarted);
            connect(mCompiler, &Compiler::compileStarted, mMainWindow, &MainWindow::clearToolsOutput);

            connect(mCompiler, &Compiler::compileOutput, mMainWindow, &MainWindow::logToolsOutput);
            connect(mCompiler, &Compiler::compileIssue, mMainWindow, &MainWindow::onCompileIssue);
            connect(mCompiler, &Compiler::compileErrorOccured, mMainWindow, &MainWindow::onCompileErrorOccured);
        }
        mCompiler->start();
    }
}

void CompilerManager::buildProjectMakefile(std::shared_ptr<Project> project)
{
#ifndef __EMSCRIPTEN__
    // Native builds refuse to work without a local compiler set.
    // The web build has no local compilers at all: single-file
    // compile/run go through the online backend instead, so this
    // gate is skipped there (Compiler::runOnlineCompile and
    // CompilerManager::run handle the web flow).
    if (!pSettings->compilerSets().defaultSet()) {
        QMessageBox::critical(mMainWindow,
                              tr("No compiler set"),
                              tr("No compiler set is configured.")+tr("Can't start debugging."));
        return;
    }
#endif
    {
        QMutexLocker locker(&mCompileMutex);
        if (mCompiler!=nullptr) {
            return;
        }
        ProjectCompiler* pCompiler=createProjectCompiler(project);
        pCompiler->buildMakeFile();
        delete pCompiler;
    }
}

void CompilerManager::checkSyntax(const QString &filename, const QByteArray& encoding, const QString &content, std::shared_ptr<Project> project)
{
#ifdef __EMSCRIPTEN__
    // Web build — Part 8 resolves the Part 2/5 open decision:
    // DISABLE the automatic background syntax check. It has no
    // local compiler here; Part 2 routed it to the online backend,
    // which EXECUTES the program on the server — on every line
    // change and every save, with no explicit user action. That is
    // both a leading typing-lag source (a remote compile per edit
    // burst) and an unwanted remote execution of in-progress code.
    // Manual Compile / Compile & Run (F9/F11) do not pass through
    // this function and keep producing the same diagnostics on
    // demand.
    Q_UNUSED(filename);
    Q_UNUSED(encoding);
    Q_UNUSED(content);
    Q_UNUSED(project);
    return;
#endif
#ifndef __EMSCRIPTEN__
    // Native builds refuse to work without a local compiler set.
    // The web build has no local compilers at all: single-file
    // compile/run go through the online backend instead, so this
    // gate is skipped there (Compiler::runOnlineCompile and
    // CompilerManager::run handle the web flow).
    if (!pSettings->compilerSets().defaultSet()) {
        QMessageBox::critical(mMainWindow,
                              tr("No compiler set"),
                              tr("No compiler set is configured.")+tr("Can't start debugging."));
        return;
    }
#endif
    {
        QMutexLocker locker(&mBackgroundSyntaxCheckMutex);
        if (mBackgroundSyntaxChecker!=nullptr) {
            return;
        }

        mSyntaxCheckErrorCount = 0;
        mSyntaxCheckIssueCount = 0;

        //deleted when thread finished
        mBackgroundSyntaxChecker = new StdinCompiler(filename,encoding, content,true);
        if (!project)
            mBackgroundSyntaxChecker->setParserForFile(getParserForFile(filename));
        mBackgroundSyntaxChecker->setProject(project);
#if defined(__EMSCRIPTEN__) && defined(DBS_WASM_ST)
        connect(mBackgroundSyntaxChecker, &Compiler::compileFinished, mBackgroundSyntaxChecker, &QThread::deleteLater);
#else
        connect(mBackgroundSyntaxChecker, &Compiler::finished, mBackgroundSyntaxChecker, &QThread::deleteLater);
#endif
        connect(mBackgroundSyntaxChecker, &Compiler::compileIssue, this, &CompilerManager::onSyntaxCheckIssue);
        connect(mBackgroundSyntaxChecker, &Compiler::compileFinished, this, &CompilerManager::onSyntaxCheckFinished);
        if (mMainWindow) {
            connect(mBackgroundSyntaxChecker, &Compiler::compileStarted, mMainWindow, &MainWindow::onSyntaxCheckStarted);
            connect(mBackgroundSyntaxChecker, &Compiler::compileIssue, mMainWindow, &MainWindow::onCompileIssue);
            connect(mBackgroundSyntaxChecker, &Compiler::compileErrorOccured, mMainWindow, &MainWindow::onCompileErrorOccured);
            //connect(mBackgroundSyntaxChecker, &Compiler::compileOutput, mMainWindow, &MainWindow::logToolsOutput);
        }
        mBackgroundSyntaxChecker->start();
    }
}

void CompilerManager::run(
        const QString &filename,
        const QString &arguments,
        const QString &workDir,
        const QStringList& binDirs)
{
    QMutexLocker locker(&mRunnerMutex);
    if (mRunner!=nullptr && !mRunner->pausing()) {
        return;
    }
    QString redirectInputFilename;
    bool redirectInput=false;
    if (pSettings->executor().redirectInput()
            && !pSettings->executor().inputFilename().isEmpty()) {
        redirectInput =true;
        redirectInputFilename = pSettings->executor().inputFilename();
    }
#ifdef __EMSCRIPTEN__
    {
        // Web build: there is no real executable (only the
        // placeholder the online compile left behind) and no console
        // pauser to spawn. Recover the source file that produced the
        // placeholder and run it through the online backend instead;
        // the program's output lands in the terminal window (Part 9;
        // before that it went to the Tools Output panel). Stdin
        // comes from the executor's existing redirect-input file
        // setting, exactly like the native runner's redirected
        // input — or from the terminal's input zone on a re-run.
        const QString sourceFile = dbsOnlineFindSourceForExecutable(filename);
        if (sourceFile.isEmpty()) {
            // Part 9: no QMessageBox::critical here — its static
            // exec() is the WASM wall. The failure lands in the
            // terminal (if a run opened one) and the Tools Output
            // log, through MainWindow's WASM helper.
            if (mMainWindow)
                mMainWindow->dbsWasmRunFailedLine(
                            tr("Run Failed: couldn't find the source "
                               "file for \"%1\".").arg(filename));
            return;
        }
        const QString language = dbsOnlineLanguageForFilename(sourceFile);
        const QString code = QString::fromUtf8(
                    readFileToByteArray(sourceFile));
        QString stdinText;
        if (redirectInput && fileExists(redirectInputFilename))
            stdinText = QString::fromUtf8(
                        readFileToByteArray(redirectInputFilename));
        // Part 9: the terminal window's "Run again with this
        // input" overrides the redirect-input file for this run.
        if (mDbsWasmStdinOverrideSet) {
            stdinText = mDbsWasmStdinOverride;
            mDbsWasmStdinOverride.clear();
            mDbsWasmStdinOverrideSet = false;
        }
        // Part 9: the run's output belongs in the terminal
        // window — make sure it exists, knows this run's context,
        // and has its input zone prefilled with the stdin in use.
        if (mMainWindow)
            mMainWindow->dbsWasmEnsureTerminal(
                        sourceFile, filename, workDir, stdinText);
        auto *onlineRunner = new OnlineExecutableRunner(
                    filename, workDir, language, code, stdinText);
        mRunner = onlineRunner;
#if defined(__EMSCRIPTEN__) && defined(DBS_WASM_ST)
        // Part 6 (single-thread web build): completion is the
        // runner's own terminated() signal, emitted by the async
        // continuation — QThread::finished fires at run() return,
        // before the program has even run.
        connect(mRunner, &Runner::terminated, this, &CompilerManager::onRunnerTerminated);
        connect(mRunner, &Runner::terminated, mRunner, &Runner::deleteLater);
#else
        connect(mRunner, &Runner::finished, this, &CompilerManager::onRunnerTerminated);
        connect(mRunner, &Runner::finished, mRunner, &Runner::deleteLater);
#endif
        if (mMainWindow) {
#if defined(__EMSCRIPTEN__) && defined(DBS_WASM_ST)
            connect(mRunner, &Runner::terminated, mMainWindow, &MainWindow::onRunFinished);
#else
            connect(mRunner, &Runner::finished, mMainWindow, &MainWindow::onRunFinished);
#endif
            // Part 9: run output goes to the terminal window
            // (structured result), and a run error is reported
            // there too — NOT through onRunErrorOccured, whose
            // QMessageBox::critical is the WASM exec() wall.
            connect(mRunner, &Runner::runErrorOccurred, mMainWindow, &MainWindow::dbsWasmShowRunError);
            connect(onlineRunner, &OnlineExecutableRunner::dbsResultReceived,
                    mMainWindow, &MainWindow::dbsWasmShowRunResult);
        }
        mRunner->start();
        return;
    }
#endif
    bool useCustomTerminal = pSettings->environment().useCustomTerminal();
    ExecutableRunner * execRunner;
    if (!PortableExecutable(filename).isWin32GuiApp()) {
        QString consolePauserPath = getFilePath(pSettings->dirs().appLibexecDir(), CONSOLE_PAUSER);
        QStringList execArgs = {consolePauserPath};
        if (redirectInput) {
            if (useCustomTerminal)
                execArgs << "--redirect-input" << redirectInputFilename;
            else
                execArgs << "--redirect-input" << "-";
        }
        if (pSettings->executor().pauseConsole())
            execArgs << "--pause-console";
#ifdef Q_OS_WIN
        if (pSettings->executor().enableVirualTerminalSequence())
            execArgs << "--enable-virtual-terminal-sequence";
        QString triplet = pSettings->compilerSets().defaultSet()->dumpMachine();
        if (triplet.contains("-linux-"))
            execArgs << "--run-in-wsl";
        QString sharedMemoryId = QUuid::createUuid().toString();
#else
        QString sharedMemoryId = "/r" + QUuid::createUuid().toString(QUuid::StringFormat::Id128);
#ifdef Q_OS_MACOS
        sharedMemoryId = sharedMemoryId.mid(0, PSHMNAMLEN);
#endif
#endif
        bool requireConsolePauser = execArgs.length() > 1;
        if (requireConsolePauser) {
            if (!fileExists(consolePauserPath)) {
                QMessageBox::critical(mMainWindow,
                                         tr("Can't find Console Pauser"),
                                         tr("Console Pauser \"%1\" doesn't exists!")
                                         .arg(consolePauserPath));
                return;
            }
            execArgs << "--shared-memory" << sharedMemoryId;

            // translation items: please keep sync with `tools/consolepauser/argparser.hpp`
            execArgs << "--add-translation" << "EXIT=" + tr("Press ANY key to exit...");
            execArgs << "--add-translation" << "USAGE_HEADER=" + tr("Process exited after");
            execArgs << "--add-translation" << "USAGE_RETURN_VALUE=" + tr("Return value");
            execArgs << "--add-translation" << "USAGE_CPU_TIME=" + tr("CPU time");
            execArgs << "--add-translation" << "USAGE_MEMORY=" + tr("Memory");

            // end of console pauser args
            execArgs << "--";
        } else {
            execArgs.clear();
        }
        execArgs << localizePath(filename);
        execArgs += parseArgumentsWithoutVariables(arguments);
        if (useCustomTerminal) {
            auto [filename, args, fileOwner] = wrapCommandForTerminalEmulator(
                pSettings->environment().terminalPath(),
                pSettings->environment().terminalArgumentsPattern(),
                execArgs,
                &pSettings->dirs()
            );
            //delete when thread finished
            execRunner = new ExecutableRunner(filename, args, workDir);
            mTempFileOwner = std::move(fileOwner);
        } else {
            //delete when thread finished
            execRunner = new ExecutableRunner(execArgs[0], execArgs.mid(1), workDir);
        }
        execRunner->setStartConsole(true);
        if (requireConsolePauser)
            execRunner->setShareMemoryId(sharedMemoryId);
    } else {
        //delete when thread finished
        execRunner = new ExecutableRunner(filename, parseArgumentsWithoutVariables(arguments), workDir);
    }
    if (redirectInput && !useCustomTerminal) {
        execRunner->setRedirectInput(true);
        execRunner->setRedirectInputFilename(redirectInputFilename);
    }
    execRunner->addBinDirs(binDirs);

    execRunner->addBinDir(pSettings->dirs().appDir());

    mRunner = execRunner;

    connect(mRunner, &Runner::finished, this ,&CompilerManager::onRunnerTerminated);
    connect(mRunner, &Runner::finished, mRunner ,&Runner::deleteLater);
    connect(mRunner, &Runner::pausingForFinish, this ,&CompilerManager::onRunnerPausing);
    if (mMainWindow) {
        connect(mRunner, &Runner::finished, mMainWindow ,&MainWindow::onRunFinished);
        connect(mRunner, &Runner::pausingForFinish, mMainWindow ,&MainWindow::onRunPausingForFinish);
        connect(mRunner, &Runner::runErrorOccurred, mMainWindow ,&MainWindow::onRunErrorOccured);
    }
    mRunner->start();
}


void CompilerManager::runProblem(const QString &filename, const QString &arguments, const QString &workDir, POJProblemCase problemCase,
                                 const POJProblem& problem
                                 )
{
    QMutexLocker locker(&mRunnerMutex);
    doRunProblem(filename, arguments, workDir, QVector<POJProblemCase> {problemCase}, problem);

}

void CompilerManager::runProblem(const QString &filename, const QString &arguments, const QString &workDir, const QVector<POJProblemCase>& problemCases,
                                 const POJProblem& problem
                                 )
{
    QMutexLocker locker(&mRunnerMutex);
    doRunProblem(filename, arguments, workDir, problemCases, problem);
}
void CompilerManager::doRunProblem(const QString &filename, const QString &arguments, const QString &workDir, const QVector<POJProblemCase>& problemCases,
                                   const POJProblem& problem)
{
    if (mRunner!=nullptr) {
        return;
    }
    OJProblemCasesRunner * execRunner = new OJProblemCasesRunner(filename, parseArgumentsWithoutVariables(arguments), workDir, problemCases);
    mRunner = execRunner;
    if (pSettings->executor().enableCaseLimit()) {
        execRunner->setExecTimeout(pSettings->executor().caseTimeout());
        execRunner->setMemoryLimit(pSettings->executor().caseMemoryLimit()*1024); //convert kb to bytes
    }
    size_t timeLimit = problem->getTimeLimitInMilliseconds();
    size_t memoryLimit = problem->getMemoryLimitInBytes();
    if (timeLimit>0)
        execRunner->setExecTimeout(timeLimit);
    if (memoryLimit)
        execRunner->setMemoryLimit(memoryLimit);
#if defined(__EMSCRIPTEN__) && defined(DBS_WASM_ST)
    // Part 6 (single-thread web build): OJ cases complete through
    // the runner's terminated() signal at the end of the async chain.
    connect(mRunner, &Runner::terminated, this ,&CompilerManager::onRunnerTerminated);
    connect(mRunner, &Runner::terminated, mRunner ,&Runner::deleteLater);
#else
    connect(mRunner, &Runner::finished, this ,&CompilerManager::onRunnerTerminated);
    connect(mRunner, &Runner::finished, mRunner ,&Runner::deleteLater);
#endif
    if (mMainWindow) {
#if defined(__EMSCRIPTEN__) && defined(DBS_WASM_ST)
        connect(mRunner, &Runner::terminated, mMainWindow ,&MainWindow::onRunProblemFinished);
#else
        connect(mRunner, &Runner::finished, mMainWindow ,&MainWindow::onRunProblemFinished);
#endif
        connect(mRunner, &Runner::runErrorOccurred, mMainWindow ,&MainWindow::onRunErrorOccured);
        connect(execRunner, &OJProblemCasesRunner::caseStarted, mMainWindow, &MainWindow::onOJProblemCaseStarted);
        connect(execRunner, &OJProblemCasesRunner::caseFinished, mMainWindow, &MainWindow::onOJProblemCaseFinished);
        connect(execRunner, &OJProblemCasesRunner::newOutputGetted, mMainWindow, &MainWindow::onOJProblemCaseNewOutputGetted);
        connect(execRunner, &OJProblemCasesRunner::resetOutput, mMainWindow, &MainWindow::onOJProblemCaseResetOutput);
        if (pSettings->executor().redirectStderrToToolLog())  {
            connect(execRunner, &OJProblemCasesRunner::logStderrOutput, mMainWindow, &MainWindow::logToolsOutput);
        }
    }
    mRunner->start();
}

void CompilerManager::stopRun()
{
    QMutexLocker locker(&mRunnerMutex);
    if (mRunner!=nullptr) {
        mRunner->stop();
        disconnect(mRunner, &Runner::finished, this ,&CompilerManager::onRunnerTerminated);
        mRunner=nullptr;
        mTempFileOwner=nullptr;
    }
}

void CompilerManager::stopAllRunners()
{
    emit signalStopAllRunners();
}

void CompilerManager::stopPausing()
{
    QMutexLocker locker(&mRunnerMutex);
    if (mRunner!=nullptr && mRunner->pausing()) {
        disconnect(mRunner, &Runner::finished, this ,&CompilerManager::onRunnerTerminated);
        mRunner->stop();
        mRunner=nullptr;
        mTempFileOwner=nullptr;
    }
}

void CompilerManager::stopCompile()
{
    QMutexLocker locker(&mCompileMutex);
    if (mCompiler!=nullptr)
        mCompiler->stopCompile();
}

void CompilerManager::stopCheckSyntax()
{
    QMutexLocker locker(&mBackgroundSyntaxCheckMutex);
    if (mBackgroundSyntaxChecker!=nullptr)
        mBackgroundSyntaxChecker->stopCompile();
}

bool CompilerManager::canCompile(const QString &) const
{
    return !compiling();
}

void CompilerManager::onCompileFinished(const QString& filename)
{
    QMutexLocker locker(&mCompileMutex);
    mCompiler=nullptr;
    emit compileFinished(filename, false);
}

void CompilerManager::onRunnerTerminated()
{
    QMutexLocker locker(&mRunnerMutex);
    mRunner=nullptr;
    mTempFileOwner=nullptr;
}

void CompilerManager::onRunnerPausing()
{
    QMutexLocker locker(&mRunnerMutex);
    disconnect(mRunner, &Runner::finished, this ,&CompilerManager::onRunnerTerminated);
    connect(this, &CompilerManager::signalStopAllRunners, mRunner, &Runner::stop);
    if (mMainWindow) {
        disconnect(mRunner, &Runner::finished, mMainWindow ,&MainWindow::onRunFinished);
        disconnect(mRunner, &Runner::runErrorOccurred, mMainWindow ,&MainWindow::onRunErrorOccured);
    }
    mRunner=nullptr;
    mTempFileOwner=nullptr;
}

void CompilerManager::onCompileIssue(PCompileIssue issue)
{
    if (issue->type == CompileIssueType::Error)
        mCompileErrorCount++;
    mCompileIssueCount++;
}

void CompilerManager::onSyntaxCheckFinished(const QString& filename)
{
    QMutexLocker locker(&mBackgroundSyntaxCheckMutex);
    mBackgroundSyntaxChecker=nullptr;
    emit compileFinished(filename, true);
}

void CompilerManager::onSyntaxCheckIssue(PCompileIssue issue)
{
    if (issue->type == CompileIssueType::Error)
        mSyntaxCheckErrorCount++;
    if (issue->type == CompileIssueType::Error ||
            issue->type == CompileIssueType::Warning)
        mSyntaxCheckIssueCount++;
}

ProjectCompiler *CompilerManager::createProjectCompiler(std::shared_ptr<Project> project)
{
#ifdef ENABLE_SDCC
    if (project->options().type==ProjectType::MicroController)
        return new SDCCProjectCompiler(project);
    else
#endif
        return new ProjectCompiler(project);
}

PCppParser CompilerManager::getParserForFile(const QString &filename)
{
    Editor* editor = mMainWindow->editorManager()->getOpenedEditor(filename);
    if (editor)
        return editor->parser();
    return PCppParser();
}

MainWindow *CompilerManager::mainWindow() const
{
    return mMainWindow;
}

#ifdef __EMSCRIPTEN__
void CompilerManager::dbsWasmSetStdinOverride(const QString &text)
{
    mDbsWasmStdinOverride = text;
    mDbsWasmStdinOverrideSet = true;
}
#endif

void CompilerManager::setMainWindow(MainWindow *newMainWindow)
{
    mMainWindow = newMainWindow;
}

int CompilerManager::syntaxCheckIssueCount() const
{
    return mSyntaxCheckIssueCount;
}

int CompilerManager::compileIssueCount() const
{
    return mCompileIssueCount;
}

int CompilerManager::syntaxCheckErrorCount() const
{
    return mSyntaxCheckErrorCount;
}

int CompilerManager::compileErrorCount() const
{
    return mCompileErrorCount;
}

CompileError::CompileError(const QString &reason):BaseError(reason)
{

}

