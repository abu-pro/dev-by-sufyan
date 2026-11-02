/*
 * Copyright (C) 2026 Abu Sufyan
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
#ifndef ROOMS_DBSFEATURES_H
#define ROOMS_DBSFEATURES_H

// Dev by Sufyan v1.4.0 feature pack — standalone pieces used by the
// MainWindow methods implemented in dbsfeatures.cpp:
//  - the "Explain in simple words" dialog for compiler messages
//  - RunAllDialog: admin runs every room member's code on one input
//  - FlowchartDialog: a simplified flowchart drawn from C/C++ code
// The dialog classes deliberately use lambda connections only, so they
// need no moc pass of their own.

#include <QDialog>
#include <QString>
#include <QVector>

#include "../common.h"

class QWidget;

// Shows a modeless explanation window for one compiler issue.
// ---------------------------------------------------------------------------
// v1.5.1 "Fix Lagao": a deterministic one-click fix computed for one issue.
// Filled by dbsComputeIssueFix() from the dictionary entry + the live file
// text; the dialog shows it BEFORE anything is applied, and applying goes
// through the editor's normal undo (Ctrl+Z reverses it).
// ---------------------------------------------------------------------------
struct DbsFixAction {
    bool valid = false;        // false = no safe one-click fix for this issue
    bool insertLine = false;   // true: insert newLineText as a new line at
                               // index line0 (== line count appends at EOF);
                               // false: replace line line0 with newLineText
    int line0 = -1;            // 0-based target line (editor numbering)
    QString expectedOldLine;   // replace fixes: the exact current line the
                               // fix was computed from (re-validated at apply)
    QString newLineText;       // the resulting line (replace) or the new
                               // line's text (insert)
    QString summary;           // one plain sentence for the user
};

// v1.5.1: shows the explanation, plus — when `fix` is valid — the exact
// line that will result and the apply buttons. applyFix(false) applies
// only; applyFix(true) applies and starts the Fix & Recheck recompile.
// The callback returns true when the edit really happened.
void dbsShowIssueExplanation(QWidget *parent, const PCompileIssue &issue,
                             const DbsFixAction &fix,
                             const std::function<bool(bool)> &applyFix);
// v1.5.1: the searchable error dictionary window (Help menu / Issues menu).
void dbsShowErrorDictionary(QWidget *parent);

struct RunAllJob {
    QString name;     // member display name
    QString language; // "cpp" | "c" | "python"
    QString code;
};

struct RunAllCompilers {
    QString cppCompiler; // program path/name for C++ (empty = not found)
    QString cCompiler;
    QString pythonExe;
};

// Admin-only "Run All & Compare": runs every job locally, one after
// another, with the same stdin for all, and tabulates the results.
class RunAllDialog : public QDialog
{
public:
    RunAllDialog(const QVector<RunAllJob> &jobs,
                 const RunAllCompilers &compilers,
                 QWidget *parent = nullptr);
};

// Draws a simplified flowchart for the given C/C++ source in a
// scrollable window; can save the drawing as a PNG image.
class FlowchartDialog : public QDialog
{
public:
    explicit FlowchartDialog(const QString &code,
                             const QString &title,
                             QWidget *parent = nullptr);
};

#endif // ROOMS_DBSFEATURES_H
