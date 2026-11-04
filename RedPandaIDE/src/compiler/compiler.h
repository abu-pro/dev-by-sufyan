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
#ifndef COMPILER_H
#define COMPILER_H

#include <QThread>
#include "../settings.h"
#include "../common.h"
#include "../parser/cppparser.h"
#include "../utils/file.h"

class Project;
#ifdef __EMSCRIPTEN__
struct DbsOnlineRunResult; // defined in compiler/onlinebackend.h
#endif
class Compiler : public QThread
{
    Q_OBJECT
public:
    enum class TargetType {
        Invalid,
        cttNone,
        File,
        Project,
        StdIn
    };
    Compiler(const QString& filename, bool onlyCheckSyntax);
    Compiler(const Compiler&)=delete;
    Compiler& operator=(const Compiler&)=delete;

    bool isRebuild() const;
    void setRebuild(bool isRebuild);

    const std::shared_ptr<Project> &project() const;
    void setProject(const std::shared_ptr<Project> &newProject);

    const PCppParser &parserForFile() const;
    void setParserForFile(const PCppParser &newParserForFile);

signals:
    void compileStarted();
    void compileFinished(const QString& filename);
    void compileOutput(const QString& msg);
    void compileIssue(PCompileIssue issue);
    void compileErrorOccured(const QString& reason);
public slots:
    void stopCompile();

protected:
    void run() override;
    void processOutput(QString& line);
    virtual QString getFileNameFromOutputLine(QString &line);
    virtual int getLineNumberFromOutputLine(QString &line);
    virtual int getColunmnFromOutputLine(QString &line);
    virtual CompileIssueType getIssueTypeFromOutputLine(QString &line);

protected:
    virtual PCompilerSet compilerSet();
    virtual bool prepareForCompile() = 0;
    virtual QByteArray pipedText();
    virtual bool prepareForRebuild() = 0;
    virtual bool beforeRunExtraCommand(int idx);
    virtual QStringList getCharsetArgument(const QByteArray& encoding, FileType fileType, bool onlyCheckSyntax);
    virtual QStringList getCppGccImportStdSources(bool checkSyntax);
    virtual QStringList getCCompileArguments(bool checkSyntax);
    virtual QStringList getCppCompileArguments(bool checkSyntax);
    virtual QStringList getCIncludeArguments();
    virtual QStringList getProjectIncludeArguments();
    virtual QStringList getCppIncludeArguments();
    virtual QStringList getLibraryArguments(FileType fileType);
    virtual QStringList parseFileIncludesForAutolink(
            const QString& filename,
            QSet<QString>& parsedFiles);
    virtual bool parseForceUTF8ForAutolink(
            const QString& filename,
            QSet<QString>& parsedFiles);
    void log(const QString& msg);
    void error(const QString& msg);
#ifdef __EMSCRIPTEN__
    // Web build only: compile (or syntax-check) the current file
    // through the online /api/run backend instead of a local compiler
    // process. Called from run(); implemented in compiler.cpp.
    void runOnlineCompile();
    // Shared front half: validates the file, logs the header and
    // extracts the source text (editor buffer or saved file). Throws
    // CompileError for projects and unsupported file types.
    void onlineCompilePrologue(QString &language, QString &code);
    // Shared back half of both online compile flavours: consumes one
    // backend result (diagnostics -> issues, result log, placeholder
    // executable). Returns false WITHOUT signalling when the request
    // itself failed, so the caller can report the network error in
    // its own flavour (throw on a worker thread / signal directly on
    // the main thread).
    bool applyOnlineCompileResult(const DbsOnlineRunResult &result,
                                  const QString &language);
#ifdef DBS_WASM_ST
    // Part 6, single-thread build: asynchronous variant — the front
    // half runs in run() on the main thread and starts the request;
    // the continuation applies the result and only then emits
    // compileFinished (QThread::finished fires at run() return in
    // this build and must not be treated as completion).
    void runOnlineCompileAsync();
    void finishOnlineCompile(const DbsOnlineRunResult &result,
                             const QString &language);
#endif // DBS_WASM_ST
#endif // __EMSCRIPTEN__
    void runCommand(const QString& cmd, const QStringList& arguments, const QString& workingDir, const QByteArray& inputText=QByteArray(), const QString& outputFile=QString());
    QString escapeCommandForLog(const QString &cmd, const QStringList &arguments);

protected:
    bool mOnlyCheckSyntax;
    QString mCompiler;
    QStringList mArguments;
    QList<QString> mExtraCompilersList;
    QList<QStringList> mExtraArgumentsList;
    QList<QString> mExtraOutputFilesList;
    QString mOutputFile;
    int mErrorCount;
    int mWarningCount;
    PCompileIssue mLastIssue;
    QString mFilename;
    QString mDirectory;
    bool mRebuild;
    std::shared_ptr<Project> mProject;
    bool mSetLANG;
    PCppParser mParserForFile;
    bool mForceEnglishOutput;

private:
    bool mStop;
};


#endif // COMPILER_H
