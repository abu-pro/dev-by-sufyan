// Dev by Sufyan v1.4.0 feature pack (implemented for MainWindow).
//
//  Part 1  Quick auto-save (30 s, named files) + crash-recovery drafts
//  Part 2  Compiler errors explained in simple words (offline list)
//  Part 3  Update checker (GitHub releases of abu-pro/dev-by-sufyan)
//  Part 7  Run All & Compare (room admin, runs locally on this PC)
//  Part 8  Save Code as Photo (PNG in the active colour scheme)
//  Part 9  Flowchart from code (simplified subset parser)
//
// The room-side pieces (live cursors, admin line point, side-by-side)
// live in mainwindow_rooms.cpp / roomclient; this file holds the rest.
#include "../mainwindow.h"
#include "ui_mainwindow.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "../editor.h"
#include "../editormanager.h"
#include "../settings.h"
#include "dbsfeatures.h"
#include "roomclient.h"
#include "roomspanel.h"
#include "welcomepage.h"
#include "qsynedit/qsynedit.h"

namespace {

// The one place the running version is named for the update checker;
// the welcome page shows the same number (see setupWelcomePage).
const char *const DBS_VERSION = "1.4.0";
const char *const DBS_QUICKSAVE_KEY = "dbs/quickSaveEnabled";

bool quickSaveEnabled()
{
    return QSettings().value(DBS_QUICKSAVE_KEY, true).toBool();
}

QString draftsFilePath()
{
    QString dir = QStandardPaths::writableLocation(
                QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + "/dbs-drafts.json";
}

// ---------------------------------------------------------------------------
// Part 2 — the simple-words error dictionary
// ---------------------------------------------------------------------------

struct ErrorExplanation {
    const char *pattern; // regular expression, matched case-insensitively
    const char *title;
    const char *what;
    const char *fix;
};

const ErrorExplanation ERROR_DICTIONARY[] = {
    { "was not declared in this scope|undeclared identifier|use of undeclared",
      "A name the compiler does not know",
      "You used a variable or function name that the compiler has never seen. Either it is a typing mistake, or the name was never created, or the header file / line that creates it is missing.",
      "Check the spelling first. If it is a variable, declare it before this line (for example: int x;). If it is a library function, add its #include at the top of the file." },
    { "expected ';'",
      "A semicolon (;) is missing",
      "Almost every statement in C/C++ must end with a semicolon. The compiler reached this point and did not find one.",
      "Look at the END of the line just ABOVE the reported line — that is where the ; is usually missing. Add it there." },
    { "expected '}'|expected ']'|expected ')'",
      "A closing bracket is missing",
      "Something was opened with { or ( or [ but never closed, so the compiler ran out of code while still waiting for the closing bracket.",
      "Count the opening and closing brackets in the function above this line. Every { needs a }. The mistake is usually in the lines above, not on the reported line." },
    { "expected '{'",
      "An opening brace { is missing",
      "After things like if (...), while (...) or a function header, the block of code must start with {.",
      "Add the missing { right after the if / while / for / function header line." },
    { "expected primary-expression|expected expression",
      "Something is missing in the expression",
      "The compiler expected a value or variable here but found something else — often an empty bracket pair like ( ), or an operator with nothing after it.",
      "Look at this spot in the line: there is probably a missing value, an extra comma, or an unfinished expression. Complete it or remove the extra symbol." },
    { "expected unqualified-id|expected declaration",
      "The code here does not make sense to the compiler",
      "This line is written in a way C/C++ cannot start a statement with. Very often the REAL mistake is on the line above (for example a forgotten ; or }) and this error is only its shadow.",
      "First fix the error ABOVE this one in the list. If this line is alone: check for stray symbols, a keyword used as a variable name, or text that is not code at all." },
    { "expected initializer",
      "A variable starts without a proper value setup",
      "When a variable is declared, what follows must be a valid start: = value, (value), {value}, a comma or a semicolon.",
      "Check the declaration on this line — something unexpected comes right after the variable name." },
    { "no match for 'operator|invalid operands",
      "These two things cannot be used together like that",
      "You tried an operation (like +, <<, ==) between values that do not fit together — for example adding a number to a string object, or printing something cout cannot print.",
      "Check the types on both sides of the operator. Convert one side (for example with (double)x or std::to_string) so the two sides match." },
    { "cannot convert|invalid conversion|could not convert",
      "Wrong type of value",
      "A value of one type was given where a different type is needed — like passing text to a function that wants a number.",
      "Look at what this line passes or assigns, and what the other side expects. Change the value's type, or change the variable/function to the matching type." },
    { "incompatible types",
      "The two sides have types that do not fit",
      "An assignment or argument mixes types that cannot be mixed directly (for example a pointer and a number).",
      "Make both sides the same kind of value, or convert one of them on purpose." },
    { "undefined reference|undefined symbol",
      "A function is promised but never written (linker error)",
      "Your code CALLS a function (or uses a variable) that is declared, but its actual code was never compiled into the program. The compiler was fine with each file — the LINKER cannot find the function's body.",
      "Three usual causes: 1) you wrote the function's declaration but forgot to write its body; 2) the .cpp file with the body is not part of the project; 3) a spelling/case difference between the call and the definition (printData vs PrintData)." },
    { "ld returned 1|collect2(.exe)?: error",
      "The linker failed",
      "Compiling finished, but the final step — joining everything into the .exe — failed. The real reason is written in the message just ABOVE this one in the list.",
      "Scroll up in the message list and fix the first linker message above this line (usually an 'undefined reference' or a 'multiple definition')." },
    { "multiple definition",
      "The same thing is written twice",
      "A function or global variable is defined in more than one place, and the linker cannot choose. This often happens when a function body is written in a header file that many files include.",
      "Keep only ONE definition. In headers, keep declarations only; put the actual bodies in a single .cpp file." },
    { "cannot find -l",
      "A library file is missing",
      "The program asks to link a library (-l something) that is not installed or not in the library folders.",
      "Check the library name in the project/linker settings, or install the library. For normal class programs, remove library names you do not really need." },
    { "no such file or directory",
      "A file was not found",
      "The compiler was told to open a file that does not exist at that place — a misspelled #include, a missing project file, or a wrong folder.",
      "Check the exact spelling of the file name (also capital letters). Make sure the file really exists in the project folder." },
    { "fatal error: .* no such file",
      "A header file is missing",
      "An #include names a header that cannot be found — usually a typing mistake, or a library header that is not installed.",
      "Check the spelling inside the < > or quotes. Standard ones look like <iostream>, <cmath>, <string>. Your own headers must sit in the same folder (use \"name.h\")." },
    { "\\bcin\\b|\\bcout\\b|\\bendl\\b",
      "cout / cin needs <iostream>",
      "cout, cin and endl live in the <iostream> header and in the std namespace. Without them the compiler does not know these names.",
      "Add #include <iostream> at the top, and either write std::cout or add the line: using namespace std;" },
    { "\\bprintf\\b|\\bscanf\\b",
      "printf / scanf needs <cstdio>",
      "In C++, printf and scanf are declared in the <cstdio> header (in C: <stdio.h>).",
      "Add #include <cstdio> (or <stdio.h> in a C file) at the top." },
    { "\\bstrlen\\b|\\bstrcpy\\b|\\bstrcmp\\b|\\bstrcat\\b|\\bmemcpy\\b",
      "String functions need <cstring>",
      "strlen, strcpy, strcmp and friends are declared in the <cstring> header (C: <string.h>).",
      "Add #include <cstring> at the top of the file." },
    { "\\bmalloc\\b|\\bfree\\b|\\brand\\b|\\bexit\\b|\\babs\\b",
      "These functions need <cstdlib>",
      "malloc, free, rand, exit and similar helpers are declared in <cstdlib> (C: <stdlib.h>).",
      "Add #include <cstdlib> at the top of the file." },
    { "\\bsqrt\\b|\\bpow\\b|\\bsin\\b|\\bcos\\b|\\bfabs\\b|\\bfloor\\b|\\bceil\\b",
      "Math functions need <cmath>",
      "sqrt, pow, sin, cos and the other math functions are declared in <cmath> (C: <math.h>).",
      "Add #include <cmath> at the top of the file." },
    { "\\bvector\\b",
      "vector needs <vector>",
      "std::vector is declared in the <vector> header.",
      "Add #include <vector> at the top, and write std::vector<...> or add using namespace std;" },
    { "\\bstring\\b",
      "string needs <string>",
      "std::string is declared in the <string> header.",
      "Add #include <string> at the top, and write std::string or add using namespace std;" },
    { "redefinition|redeclared",
      "The same name is created twice",
      "A variable, function or class with this name already exists in the same area of the code. C/C++ allows only one.",
      "Rename one of them, or delete the duplicate declaration. If it is a variable, remember you declare it once (int x;) and later just use it (x = 5;)." },
    { "cannot be used as a function",
      "That name is not a function",
      "You called something with ( ) as if it were a function, but it is a variable (or something else).",
      "Check the name: you probably have a variable with the same name as the function you meant to call. Rename the variable." },
    { "subscripted value|invalid types .*for array subscript",
      "Square brackets used on something that is not an array",
      "You wrote name[index] but 'name' is not an array (or the index is not a whole number).",
      "Check that the variable really is an array (declared like int a[10];) and that the index is an integer." },
    { "lvalue required",
      "You cannot assign a value to this",
      "The left side of = must be something that can HOLD a value — a variable. Here the left side is a value or a calculation.",
      "Swap the sides: it should be variable = value. Also check for the classic mistake: writing (a + b) = something." },
    { "assignment of read-only|read-only variable|read-only location",
      "You tried to change something marked const",
      "A variable declared with const cannot be changed after its first value — and the code tries to change it here.",
      "Remove the const if the value really must change, or stop assigning to it (this includes ++ and --)." },
    { "invalid preprocessing|stray '#'|extra tokens at end of #",
      "A problem on a # line",
      "Lines starting with # are instructions for the compiler itself (#include, #define). Something on this line is not written the way those instructions must be.",
      "Check this # line for typing mistakes. #include needs <file> or \"file\" right after it; a #define needs a name and a value." },
    { "missing terminating ' character|missing terminating \" character|unterminated",
      "A quote was never closed",
      "Text in \"double quotes\" or a letter in 'single quotes' must end with the same quote on the same line. One is missing here.",
      "Find the text on this line and close the quote. Remember: 'x' is ONE letter; \"x\" is text — do not mix the two." },
    { "multi-character character constant",
      "Too many letters between single quotes",
      "Single quotes hold exactly ONE character, like 'a'. Text with more letters needs double quotes.",
      "Change 'hello' to \"hello\" (double quotes) if it is text." },
    { "control reaches end of non-void|no return statement",
      "A function can finish without returning a value",
      "This function promises to give back a value (its type is not void), but some path through it reaches the end without a return.",
      "Add a return statement so EVERY way through the function returns a value — also after if/else branches. (This is a warning, but the missing value can crash or misbehave.)" },
    { "unused variable",
      "A variable is declared but never used (just a warning)",
      "You created this variable and then never read it. The program still works — this is only a hint, often a sign of a forgotten step.",
      "Either use the variable where you meant to, or delete its declaration. Safe to ignore while learning." },
    { "comparison between signed and unsigned|sign-compare",
      "Comparing a signed and an unsigned number (warning)",
      "One side of the comparison can hold negative numbers, the other cannot. For very large or negative values the comparison can surprise you.",
      "Make both sides the same type — for loop counters compared with .size(), use std::size_t or int consistently." },
    { "expects (an )?argument of type|format '%",
      "printf format and value do not match",
      "In printf, each % code must match the type of the value after it: %d for int, %f for float/double, %c for char, %s for text.",
      "Compare each % in the format text with its value, in order, and fix the code or the value so the types match." },
    { "implicit declaration of function",
      "A function is used before the compiler knows it (C)",
      "In C, a function must be declared (or defined) ABOVE the place where it is first called. Here the call comes first.",
      "Move the function's definition above main, or write its declaration (name, return type, parameters + ;) above the call. Also check the #include for library functions." },
    { "unknown type name",
      "That type name does not exist",
      "You used a word as a type (like a variable's kind) that the compiler does not know — a typo, a missing #include, or a class you forgot to define above.",
      "Check the spelling. If it is your own class/struct, its definition must come before this line. If it is from a library, add its #include." },
    { "has no member named",
      "That member does not exist in the class/struct",
      "You wrote object.member, but the class or struct of 'object' has no member with that name — a typo, or the member is in a different class.",
      "Open the class/struct definition and check the exact member names and spelling." },
    { "no matching function for call",
      "No version of the function fits this call",
      "The function exists, but none of its versions accepts the arguments you passed — wrong number of arguments, or wrong types.",
      "Compare your call with the function's declaration: count the arguments and check each type, in order." },
    { "request for member .* in .* which is of non-class type",
      "The dot (.) is used on something that is not an object",
      "You wrote value.member, but 'value' is a plain number/text/pointer, not an object with members. For pointers the arrow -> is used instead.",
      "If it is a pointer to an object, write ptr->member. Otherwise check why this variable is not the object you expected." },
    { "a function-definition is not allowed here",
      "A function is written inside another function",
      "C/C++ does not allow writing a whole function inside another function. A closing brace } is probably missing above, so the compiler thinks you are still inside the previous function.",
      "Check the function above: count its { and }. Most likely one } is missing at its end." },
    { "at end of input|expected '}' at end",
      "The file ends in the middle of something",
      "The code finishes while a { block is still open. Somewhere above, a } is missing.",
      "Go through the file and count opening { and closing } — they must be equal. Check functions and loops one by one." },
    { "duplicate case value|case label",
      "A problem with a switch case",
      "Two cases in the same switch have the same value, or a case is written outside a switch.",
      "Give each case a different value, and make sure all cases sit inside the switch's { }." },
    { "break statement not within|continue statement not within",
      "break / continue used outside a loop or switch",
      "break only works inside a loop or switch; continue only inside a loop. Here there is none around it — often because a } above closed the loop too early.",
      "Check the braces above this line: the loop probably ends earlier than you think." },
    { "excess elements in .* initializer|too many initializers",
      "Too many values for the array size",
      "You declared an array of a fixed size but listed more starting values than fit.",
      "Count the values: they must not be more than the size in [ ]. Or remove the size and let the compiler count: int a[] = {...};" },
    { "void value not ignored",
      "A function that returns nothing is used as a value",
      "The function you called is declared void — it gives nothing back — but the code tries to use its result in a calculation or assignment.",
      "Call the function on its own line without using a result, or change the function to return the value you need." },
    { "forbids comparison between pointer and integer|comparison between pointer",
      "Comparing a pointer with a number",
      "One side of the comparison is a memory address (pointer/array name), the other is a plain number — comparing them directly is almost never what you mean.",
      "You probably want the VALUE the pointer points to: write *ptr, or array[index], in the comparison." },
    { "invalid use of void",
      "A void value is being used as data",
      "Something of type void (which means 'nothing') is used where real data is needed.",
      "Check the function call here: a void function gives nothing back, so it cannot stand in an expression." },
    { "ambiguous|call of overloaded .* is ambiguous",
      "Two function versions fit equally well",
      "More than one version of the function could accept your arguments, and the compiler refuses to guess.",
      "Make the arguments exactly one version's types — for example pass 2.0 instead of 2 for a double parameter, or convert with (int)x." },
    { "aggregate .* has incomplete type|invalid use of incomplete type|incomplete type",
      "The compiler has only heard the name, not the definition",
      "A class/struct is used here, but at this point the compiler has only seen its name announced (or just an #include is missing) — it does not know its members or size.",
      "Make sure the full class/struct definition (or its header) comes BEFORE this use. For library types, add the right #include." },
    { "invalid initialization of reference|cannot bind",
      "A reference cannot be attached like this",
      "A reference (type&) must be attached to a real variable of the matching type — not to a plain value or a different type.",
      "Pass a variable of the exact type, or change the parameter to take the value by copy (without &)." },
    { "'main' must return|return type of 'main'",
      "main must be written as int main()",
      "The main function has a rule no other function has: it must return an int.",
      "Write: int main() { ... return 0; }" },
    { "forbids declaration .* with no type",
      "A declaration is missing its type",
      "Something is declared without saying its type — old compilers guessed int, modern C++ does not.",
      "Write the type in front, for example: int count = 0;" },
    { "array must be initialized|invalid initializer",
      "The starting values of an array are written wrongly",
      "Array values at declaration must be listed inside { }.",
      "Write them like: int a[3] = {1, 2, 3};" },
    { "jump to case label|crosses initialization",
      "A switch case jumps over a variable's birth",
      "Execution can jump into a case past the point where a variable is declared with a starting value, which C++ forbids.",
      "Put the case's code inside its own { } block, or declare the variable before the switch." },
    { "statement .* has no effect|no effect",
      "This line does nothing (warning)",
      "The statement computes something and throws it away. The classic cause: writing == (compare) where = (assign) was meant, or a ; right after if (...).",
      "Check this line: if you meant to store a value, use a single =. If you meant to compare, the comparison result must be used (in an if, for example)." },
    { "suggest parentheses|parentheses around",
      "The order of operations may not be what you think (warning)",
      "Operators like &&, ||, = and comparisons mix in a fixed order that often surprises beginners.",
      "Add ( ) around the parts so the order you mean is the order that happens." },
    { "divides? by zero|division by zero",
      "Division by zero",
      "A division (or %) has zero on the right side — with a constant zero the compiler warns; with a variable it can crash at run time.",
      "Check the dividing value before dividing (if (b != 0)) and fix where it becomes zero." },
};

const ErrorExplanation *explainError(const QString &description)
{
    for (const ErrorExplanation &entry : ERROR_DICTIONARY) {
        QRegularExpression re(QString::fromLatin1(entry.pattern),
                              QRegularExpression::CaseInsensitiveOption);
        if (re.match(description).hasMatch())
            return &entry;
    }
    return nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
// Part 2 — explanation dialog
// ---------------------------------------------------------------------------

void dbsShowIssueExplanation(QWidget *parent, const PCompileIssue &issue)
{
    if (!issue)
        return;
    const ErrorExplanation *found = explainError(issue->description);
    auto *dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QObject::tr("Error in simple words"));
    dialog->resize(520, 380);
    auto *layout = new QVBoxLayout(dialog);

    auto *title = new QLabel(found ? QString::fromLatin1(found->title)
                                   : QObject::tr("Let us read this message together"));
    title->setStyleSheet("font-size: 14pt; font-weight: bold;");
    title->setWordWrap(true);
    layout->addWidget(title);

    auto *said = new QLabel(QObject::tr("The compiler said:"));
    said->setStyleSheet("font-weight: bold; margin-top: 8px;");
    layout->addWidget(said);
    auto *raw = new QLabel(issue->description);
    raw->setWordWrap(true);
    raw->setStyleSheet("font-family: monospace; color: #c0392b;");
    layout->addWidget(raw);
    if (issue->line > 0)
        layout->addWidget(new QLabel(QObject::tr("It points at line %1 of %2.")
                                     .arg(issue->line)
                                     .arg(QFileInfo(issue->filename).fileName())));

    auto *whatHead = new QLabel(QObject::tr("What it means:"));
    whatHead->setStyleSheet("font-weight: bold; margin-top: 8px;");
    layout->addWidget(whatHead);
    auto *what = new QLabel(found ? QString::fromLatin1(found->what)
                                  : QObject::tr("This exact message is not in the built-in list yet, but do not worry — most compiler messages follow the same pattern: the first error is the real one, and later errors are often just its shadow."));
    what->setWordWrap(true);
    layout->addWidget(what);

    auto *fixHead = new QLabel(QObject::tr("How to fix it:"));
    fixHead->setStyleSheet("font-weight: bold; margin-top: 8px;");
    layout->addWidget(fixHead);
    auto *fix = new QLabel(found ? QString::fromLatin1(found->fix)
                                 : QObject::tr("1) Always fix the FIRST error in the list first, then compile again — half the list often disappears. 2) Read the reported line AND the line above it; missing ; and } almost always hide one line earlier. 3) Compare the spelling of names letter by letter."));
    fix->setWordWrap(true);
    layout->addWidget(fix);

    layout->addStretch();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::close);
    layout->addWidget(buttons);
    dialog->show();
}

void MainWindow::dbsExplainIssueAt(const QModelIndex &index)
{
    if (!index.isValid())
        return;
    PCompileIssue issue = ui->tableIssues->issue(index);
    if (issue)
        dbsShowIssueExplanation(this, issue);
}

// ---------------------------------------------------------------------------
// Part 3 — update checker helpers
// ---------------------------------------------------------------------------

namespace {

QVector<int> versionParts(const QString &version)
{
    QVector<int> parts;
    for (const QString &piece : version.split('.')) {
        bool ok = false;
        int value = piece.trimmed().toInt(&ok);
        parts.append(ok ? value : 0);
    }
    return parts;
}

bool isNewerVersion(const QString &candidate, const QString &current)
{
    QVector<int> a = versionParts(candidate);
    QVector<int> b = versionParts(current);
    int n = qMax(a.size(), b.size());
    for (int i = 0; i < n; ++i) {
        int av = i < a.size() ? a[i] : 0;
        int bv = i < b.size() ? b[i] : 0;
        if (av != bv)
            return av > bv;
    }
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// setup: timers, menus, startup checks
// ---------------------------------------------------------------------------

void MainWindow::setupDbsFeatures()
{
    // Tools menu: code photo + flowchart (v1.4.0 Parts 8 and 9).
    ui->menuTools->addSeparator();
    ui->menuTools->addAction(tr("Save Code as Photo..."),
                             this, &MainWindow::dbsSaveCodePhoto);
    ui->menuTools->addAction(tr("Flowchart from Code..."),
                             this, &MainWindow::dbsShowFlowchart);
    // Help menu: manual update check (Part 3).
    ui->menuHelp->addSeparator();
    QAction *updateAction = ui->menuHelp->addAction(tr("Check for Updates"));
    connect(updateAction, &QAction::triggered, this,
            [this]() { dbsCheckForUpdates(true); });

    // Part 1: the 30-second quick-save + drafts timer. The upstream
    // minute-based auto-save in the settings stays untouched; this one
    // is on by default and can be switched off in Settings > Editor >
    // Auto Save (the Dev by Sufyan checkbox there).
    mDbsSaveTimer = new QTimer(this);
    mDbsSaveTimer->setInterval(30000);
    connect(mDbsSaveTimer, &QTimer::timeout,
            this, &MainWindow::dbsSaveTick);
    mDbsSaveTimer->start();
    // Save when the app loses focus too — the moment a crash or a
    // power cut is most likely to be preceded by switching away.
    connect(qApp, &QApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState state) {
        if (state != Qt::ApplicationActive)
            dbsSaveTick();
    });

    // Startup follow-ups, delayed so the Home tab is painted first and
    // neither check can block or cover it.
    QTimer::singleShot(1500, this, [this]() { dbsCheckRecovery(); });
    QTimer::singleShot(6000, this, [this]() { dbsCheckForUpdates(false); });

    // Headless verification hooks (used by the sandbox screenshot rig;
    // harmless in production: they only act when the variables exist).
    QString photoPath = qEnvironmentVariable("DBS_TEST_PHOTO");
    if (!photoPath.isEmpty()) {
        QTimer::singleShot(2500, this, [this, photoPath]() {
            Editor *editor = mEditorManager->getEditor();
            if (editor)
                dbsSaveCodePhotoTo(editor, 2, photoPath);
        });
    }
    if (!qEnvironmentVariable("DBS_TEST_FLOWCHART").isEmpty()) {
        QTimer::singleShot(2500, this, [this]() { dbsShowFlowchart(); });
    }
    if (!qEnvironmentVariable("DBS_TEST_UPDATE").isEmpty()) {
        QTimer::singleShot(2500, this, [this]() { dbsCheckForUpdates(true); });
    }
    if (!qEnvironmentVariable("DBS_TEST_EXPLAIN").isEmpty()) {
        QTimer::singleShot(2500, this, [this]() {
            auto issue = std::make_shared<CompileIssue>();
            issue->filename = "main.cpp";
            issue->line = 7;
            issue->column = 5;
            issue->description = "'total' was not declared in this scope";
            issue->type = CompileIssueType::Error;
            dbsShowIssueExplanation(this, issue);
        });
    }
    if (!qEnvironmentVariable("DBS_TEST_RUNALL").isEmpty()) {
        QTimer::singleShot(2500, this, [this]() {
            QVector<RunAllJob> jobs;
            jobs.append({"Abu (you)", "cpp",
                         "#include <iostream>\nusing namespace std;\n"
                         "int main(){int n=0;cin>>n;cout<<n*2<<endl;}\n"});
            jobs.append({"Broken Member", "cpp",
                         "int main() { cout << \"hi\" }\n"});
            jobs.append({"Python Member", "python",
                         "print('hello from python')\n"});
            RunAllCompilers compilers;
            PCompilerSet set = pSettings->compilerSets().defaultSet();
            if (set) {
                compilers.cppCompiler = set->cppCompiler();
                compilers.cCompiler = set->CCompiler();
            }
            if (compilers.cppCompiler.isEmpty())
                compilers.cppCompiler = QStandardPaths::findExecutable("g++");
            if (compilers.cCompiler.isEmpty())
                compilers.cCompiler = QStandardPaths::findExecutable("gcc");
            compilers.pythonExe = QStandardPaths::findExecutable("python3");
            auto *dialog = new RunAllDialog(jobs, compilers, this);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->show();
        });
    }
}

// ---------------------------------------------------------------------------
// Part 1 — quick save + recovery drafts
// ---------------------------------------------------------------------------

void MainWindow::dbsSaveTick()
{
    if (mQuitting || !quickSaveEnabled())
        return;
    QSet<Editor *> memberEditors;
    for (auto it = mMemberEditors.constBegin();
         it != mMemberEditors.constEnd(); ++it)
        if (it.value())
            memberEditors.insert(it.value().data());

    QJsonArray drafts;
    int count = mEditorManager->pageCount();
    for (int i = 0; i < count; ++i) {
        Editor *editor = (*mEditorManager)[i];
        if (!editor || memberEditors.contains(editor))
            continue;
        const QString filename = editor->filename();
        const bool realFile = QFileInfo(filename).isAbsolute();
        // Named files: save straight to disk, quietly.
        if (realFile && editor->modified()) {
            try {
                editor->save();
            } catch (...) {
                // Auto-save must never throw a dialog at the user; the
                // draft below still protects the work.
            }
        }
        // Everything still unsaved (untitled buffers, or a save that
        // failed) becomes a recovery draft.
        if (editor->modified()) {
            QJsonObject entry;
            entry["filename"] = filename;
            entry["title"] = realFile ? QFileInfo(filename).fileName()
                                      : editor->caption();
            entry["savedAt"] = QDateTime::currentMSecsSinceEpoch();
            entry["code"] = editor->text();
            drafts.append(entry);
        }
    }
    QJsonObject root;
    root["drafts"] = drafts;
    QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Compact);
    static QByteArray lastPayload;
    if (payload == lastPayload)
        return;
    lastPayload = payload;
    QFile file(draftsFilePath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(payload);
}

void MainWindow::dbsCheckRecovery()
{
    if (!quickSaveEnabled())
        return;
    QFile file(draftsFilePath());
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
        return;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    QJsonArray entries = doc.object().value("drafts").toArray();

    struct Draft { QString filename, title, code; qint64 savedAt; };
    QVector<Draft> pending;
    for (const QJsonValue &value : entries) {
        QJsonObject entry = value.toObject();
        Draft draft{entry["filename"].toString(), entry["title"].toString(),
                    entry["code"].toString(),
                    (qint64)entry["savedAt"].toDouble()};
        if (draft.code.isEmpty())
            continue;
        if (QFileInfo(draft.filename).isAbsolute()
                && QFile::exists(draft.filename)) {
            // Already saved to disk in the meantime? Then no recovery.
            QFile disk(draft.filename);
            if (disk.open(QIODevice::ReadOnly)
                    && QString::fromUtf8(disk.readAll()) == draft.code)
                continue;
        }
        pending.append(draft);
    }
    if (pending.isEmpty()) {
        QFile::remove(draftsFilePath());
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Recover unsaved work?"));
    auto *layout = new QVBoxLayout(&dialog);
    QStringList lines;
    for (const Draft &draft : pending)
        lines << tr("%1 — last saved %2")
                 .arg(draft.title.isEmpty() ? draft.filename : draft.title,
                      QDateTime::fromMSecsSinceEpoch(draft.savedAt)
                      .toString("yyyy-MM-dd hh:mm"));
    layout->addWidget(new QLabel(
                tr("Dev by Sufyan kept drafts of work that was not saved "
                   "before the app closed:\n\n%1\n\nRestore them now?")
                .arg(lines.join("\n"))));
    auto *buttons = new QDialogButtonBox(&dialog);
    QPushButton *restoreBtn = buttons->addButton(
                tr("Restore"), QDialogButtonBox::AcceptRole);
    buttons->addButton(tr("Discard drafts"), QDialogButtonBox::DestructiveRole);
    buttons->addButton(tr("Later"), QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);
    bool discard = false;
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::clicked, &dialog,
            [&discard, &dialog, buttons](QAbstractButton *button) {
        // DestructiveRole buttons emit neither accepted nor rejected;
        // treat Discard as an accept with the discard flag set.
        if (buttons->buttonRole(button) == QDialogButtonBox::DestructiveRole) {
            discard = true;
            dialog.accept();
        }
    });
    Q_UNUSED(restoreBtn);
    if (dialog.exec() != QDialog::Accepted)
        return; // "Later" (reject) keeps the drafts for next launch.
    QFile::remove(draftsFilePath());
    if (discard)
        return;
    QString recoveredDir = QStandardPaths::writableLocation(
                QStandardPaths::AppDataLocation) + "/recovered";
    QDir().mkpath(recoveredDir);
    for (const Draft &draft : pending) {
        if (QFileInfo(draft.filename).isAbsolute()
                && QFile::exists(draft.filename)) {
            if (Editor *editor = openFile(draft.filename)) {
                editor->setContent(draft.code);
                editor->setModified(true);
            }
        } else {
            QString name = draft.title.isEmpty() ? QString("recovered")
                                                 : draft.title;
            name.replace(QRegularExpression("[^A-Za-z0-9_.-]"), "_");
            if (!name.contains('.'))
                name += ".cpp";
            QString path = recoveredDir + "/" + name;
            int n = 2;
            while (QFile::exists(path))
                path = recoveredDir + "/"
                        + QString::number(n++) + "_" + name;
            QFile out(path);
            if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                out.write(draft.code.toUtf8());
                out.close();
                openFile(path);
            }
        }
    }
    statusBar()->showMessage(
                tr("Recovered %1 unsaved draft(s).").arg(pending.size()), 8000);
}

// ---------------------------------------------------------------------------
// Part 3 — update checker
// ---------------------------------------------------------------------------

void MainWindow::dbsCheckForUpdates(bool manual)
{
    auto *net = new QNetworkAccessManager(this);
    QNetworkRequest request{
        QUrl("https://api.github.com/repos/abu-pro/dev-by-sufyan/releases/latest")};
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QString("DevBySufyan/%1").arg(DBS_VERSION));
    request.setRawHeader("Accept", "application/vnd.github+json");
    QNetworkReply *reply = net->get(request);
    auto *timeout = new QTimer(reply);
    timeout->setSingleShot(true);
    timeout->setInterval(20000);
    connect(timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
    timeout->start();
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, net, manual]() {
        reply->deleteLater();
        net->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (manual)
                QMessageBox::warning(
                            this, tr("Check for Updates"),
                            tr("Could not check for updates. Please check "
                               "your internet connection and try again."));
            return; // automatic checks stay silent when offline
        }
        QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        QString tag = root.value("tag_name").toString();
        QString latest = tag;
        if (latest.startsWith('v') || latest.startsWith('V'))
            latest = latest.mid(1);
        if (latest.isEmpty() || !isNewerVersion(latest, DBS_VERSION)) {
            if (manual)
                QMessageBox::information(
                            this, tr("Check for Updates"),
                            tr("You are up to date — Dev by Sufyan v%1 is "
                               "the latest version.").arg(DBS_VERSION));
            return;
        }
        QString downloadUrl = root.value("html_url").toString();
        for (const QJsonValue &value : root.value("assets").toArray()) {
            QJsonObject asset = value.toObject();
            if (asset.value("name").toString() == "DevBySufyan-Setup.exe") {
                downloadUrl = asset.value("browser_download_url").toString();
                break;
            }
        }
        if (mWelcomePage)
            mWelcomePage->setUpdateNotice(
                        tr("Update available: v%1 — "
                           "<a style=\"color:#7ec3ff\" href=\"%2\">Download</a>")
                        .arg(latest, downloadUrl));
        if (!manual && mUpdateNoticeShown)
            return; // one automatic notice per session is enough
        mUpdateNoticeShown = true;
        QMessageBox box(this);
        box.setWindowTitle(tr("Update available"));
        box.setText(tr("Dev by Sufyan v%1 is available (you have v%2).")
                    .arg(latest, DBS_VERSION));
        box.setInformativeText(tr("Download the new setup and run it — "
                                  "your settings and files stay as they are."));
        QPushButton *downloadBtn = box.addButton(tr("Download"),
                                                 QMessageBox::AcceptRole);
        box.addButton(tr("Later"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() == downloadBtn)
            QDesktopServices::openUrl(QUrl(downloadUrl));
    });
}

// ---------------------------------------------------------------------------
// Part 7 — Run All & Compare
// ---------------------------------------------------------------------------

namespace {

struct RunAllState {
    QVector<RunAllJob> jobs;
    RunAllCompilers compilers;
    QVector<QString> statuses;
    QVector<QString> outputs;
    int index = 0;
    bool cancelled = false;
    bool timedOut = false;
    bool overflow = false;
    QProcess *process = nullptr;
    QTimer *timer = nullptr;
    QString buffer;
};

QString previewOf(const QString &text)
{
    QString oneLine = text;
    oneLine.replace('\n', ' ');
    if (oneLine.size() > 110)
        oneLine = oneLine.left(110) + "…";
    return oneLine;
}

} // namespace

RunAllDialog::RunAllDialog(const QVector<RunAllJob> &jobs,
                           const RunAllCompilers &compilers,
                           QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Run All & Compare"));
    resize(760, 560);
    auto state = std::make_shared<RunAllState>();
    state->jobs = jobs;
    state->compilers = compilers;
    state->statuses = QVector<QString>(jobs.size(), tr("Waiting"));
    state->outputs = QVector<QString>(jobs.size());

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(
                tr("The same input is given to every member's program. "
                   "Everything runs on this PC, one program at a time.")));
    auto *input = new QPlainTextEdit;
    input->setPlaceholderText(tr("Input (stdin) for all programs — "
                                 "leave empty if the programs take no input"));
    input->setMaximumHeight(84);
    layout->addWidget(input);

    auto *table = new QTableWidget(jobs.size(), 3);
    table->setHorizontalHeaderLabels({tr("Member"), tr("Result"), tr("Output")});
    table->horizontalHeader()->setStretchLastSection(true);
    table->setColumnWidth(0, 170);
    table->setColumnWidth(1, 150);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    for (int i = 0; i < jobs.size(); ++i) {
        table->setItem(i, 0, new QTableWidgetItem(jobs[i].name));
        table->setItem(i, 1, new QTableWidgetItem(tr("Waiting")));
        table->setItem(i, 2, new QTableWidgetItem());
    }
    layout->addWidget(table);

    auto *detail = new QPlainTextEdit;
    detail->setReadOnly(true);
    detail->setPlaceholderText(tr("Click a row to see the full output here."));
    layout->addWidget(detail);

    auto *progress = new QLabel;
    layout->addWidget(progress);
    auto *buttons = new QHBoxLayout;
    auto *runButton = new QPushButton(tr("Run All"));
    auto *cancelButton = new QPushButton(tr("Cancel"));
    cancelButton->setEnabled(false);
    auto *closeButton = new QPushButton(tr("Close"));
    buttons->addWidget(runButton);
    buttons->addWidget(cancelButton);
    buttons->addStretch();
    buttons->addWidget(closeButton);
    layout->addLayout(buttons);

    connect(table, &QTableWidget::cellClicked, this,
            [state, detail](int row, int) {
        if (row >= 0 && row < state->outputs.size())
            detail->setPlainText(state->outputs[row]);
    });
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(this, &QDialog::finished, this, [state](int) {
        state->cancelled = true;
        if (state->process)
            state->process->kill();
    });

    std::shared_ptr<std::function<void()>> runNext =
            std::make_shared<std::function<void()>>();
    auto finishJob = [state, table, detail, progress, runButton,
                      cancelButton, runNext](const QString &status,
                                             const QString &output) {
        int i = state->index;
        state->statuses[i] = status;
        state->outputs[i] = output;
        table->item(i, 1)->setText(status);
        table->item(i, 2)->setText(previewOf(output));
        detail->setPlainText(output);
        state->index++;
        (*runNext)();
    };

    *runNext = [this, state, input, table, progress, runButton,
                cancelButton, finishJob]() {
        if (state->cancelled || state->index >= state->jobs.size()) {
            if (state->cancelled) {
                for (int i = state->index; i < state->jobs.size(); ++i) {
                    state->statuses[i] = tr("Cancelled");
                    table->item(i, 1)->setText(tr("Cancelled"));
                }
            }
            progress->setText(state->cancelled ? tr("Cancelled.")
                                               : tr("Done — %1 programs ran.")
                                                 .arg(state->jobs.size()));
            runButton->setEnabled(true);
            cancelButton->setEnabled(false);
            return;
        }
        const RunAllJob job = state->jobs[state->index];
        progress->setText(tr("Running %1 of %2 — %3…")
                          .arg(state->index + 1).arg(state->jobs.size())
                          .arg(job.name));
        table->item(state->index, 1)->setText(tr("Running…"));

        QString dirPath = QDir::tempPath()
                + QString("/dbs-runall-%1-%2")
                .arg(QCoreApplication::applicationPid())
                .arg(state->index);
        QDir().mkpath(dirPath);
        QString srcName = job.language == "python" ? "main.py"
                        : job.language == "c"    ? "main.c"
                                                 : "main.cpp";
        QFile src(dirPath + "/" + srcName);
        if (!src.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            finishJob(tr("Could not write temp file"), QString());
            return;
        }
        src.write(job.code.toUtf8());
        src.close();

        auto startRun = [this, state, input, dirPath, finishJob]() {
            QString program;
            QStringList args;
            if (state->jobs[state->index].language == "python") {
                program = state->compilers.pythonExe;
                args << dirPath + "/main.py";
            } else {
                program = dirPath + "/prog";
#ifdef Q_OS_WIN
                program += ".exe";
#endif
            }
            if (program.isEmpty()) {
                finishJob(tr("No Python found on this PC"), QString());
                return;
            }
            auto *proc = new QProcess(this);
            state->process = proc;
            state->buffer.clear();
            state->timedOut = false;
            state->overflow = false;
            proc->setWorkingDirectory(dirPath);
            auto *timer = new QTimer(proc);
            timer->setSingleShot(true);
            timer->setInterval(5000);
            state->timer = timer;
            connect(timer, &QTimer::timeout, proc, [state]() {
                state->timedOut = true;
                if (state->process)
                    state->process->kill();
            });
            auto drain = [state, proc]() {
                state->buffer += QString::fromUtf8(
                            proc->readAllStandardOutput());
                state->buffer += QString::fromUtf8(
                            proc->readAllStandardError());
                if (state->buffer.size() >= 256 * 1024 && !state->overflow) {
                    state->overflow = true;
                    proc->kill();
                }
            };
            connect(proc, &QProcess::readyReadStandardOutput, proc, drain);
            connect(proc, &QProcess::readyReadStandardError, proc, drain);
            connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), proc,
                    [state, finishJob, proc, drain](int exitCode,
                                                    QProcess::ExitStatus) {
                drain();
                QString status;
                if (state->overflow)
                    status = tr("Too much output — stopped");
                else if (state->timedOut)
                    status = tr("Timed out (5 s)");
                else if (exitCode == 0)
                    status = tr("OK");
                else
                    status = tr("Error (exit %1)").arg(exitCode);
                QString out = state->buffer;
                if (out.isEmpty())
                    out = tr("(no output)");
                state->process = nullptr;
                proc->deleteLater();
                finishJob(status, out);
            });
            proc->start(program, args);
            if (!proc->waitForStarted(3000)) {
                state->process = nullptr;
                proc->deleteLater();
                finishJob(tr("Could not start the program"), QString());
                return;
            }
            timer->start();
            proc->write(input->toPlainText().toUtf8());
            proc->closeWriteChannel();
        };

        if (job.language == "python") {
            startRun();
            return;
        }
        QString compiler = job.language == "c" ? state->compilers.cCompiler
                                               : state->compilers.cppCompiler;
        if (compiler.isEmpty()) {
            finishJob(tr("No compiler found"), QString());
            return;
        }
        auto *compileProc = new QProcess(this);
        state->process = compileProc;
        compileProc->setWorkingDirectory(dirPath);
        QString exePath = dirPath + "/prog";
#ifdef Q_OS_WIN
        exePath += ".exe";
#endif
        QStringList args{"-O2",
                         job.language == "c" ? "-std=c17" : "-std=c++17",
                         "-o", exePath, dirPath + "/" + srcName};
        auto *ctimer = new QTimer(compileProc);
        ctimer->setSingleShot(true);
        ctimer->setInterval(20000);
        connect(ctimer, &QTimer::timeout, compileProc, [compileProc]() {
            compileProc->kill();
        });
        connect(compileProc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), compileProc,
                [state, finishJob, compileProc, startRun](int exitCode,
                                                          QProcess::ExitStatus) {
            QString err = QString::fromUtf8(compileProc->readAllStandardError());
            QString out = QString::fromUtf8(compileProc->readAllStandardOutput());
            state->process = nullptr;
            compileProc->deleteLater();
            if (state->cancelled)
                return;
            if (exitCode != 0) {
                finishJob(tr("Compile error"),
                          (err.isEmpty() ? out : err));
                return;
            }
            startRun();
        });
        compileProc->start(compiler, args);
        if (!compileProc->waitForStarted(3000)) {
            state->process = nullptr;
            compileProc->deleteLater();
            finishJob(tr("Compiler could not start"), QString());
            return;
        }
        ctimer->start();
    };

    connect(runButton, &QPushButton::clicked, this,
            [state, runButton, cancelButton, progress]() {
        state->cancelled = false;
        state->index = 0;
        runButton->setEnabled(false);
        cancelButton->setEnabled(true);
        progress->setText(tr("Starting…"));
        // reset the table
        // (the run driver is invoked through the button's own chain)
    });
    // The driver itself is kicked off from a tiny indirection so the
    // state machine above stays the single entry point.
    connect(runButton, &QPushButton::clicked, this,
            [runNext]() { (*runNext)(); });
    connect(cancelButton, &QPushButton::clicked, this, [state]() {
        state->cancelled = true;
        if (state->process)
            state->process->kill();
    });
}

void MainWindow::dbsRunAllCompare()
{
    if (!mRoomClient || !mRoomClient->isInRoom()) {
        statusBar()->showMessage(
                    tr("Join a room first — Run All compares the room "
                       "members' code."), 5000);
        return;
    }
    if (!mRoomClient->isAdmin()) {
        QMessageBox::information(
                    this, tr("Run All & Compare"),
                    tr("Only the room admin can run everyone's code."));
        return;
    }
    QVector<RunAllJob> jobs;
    for (const RoomMemberInfo &member : mRoomMembers) {
        RunAllJob job;
        job.name = member.name;
        job.language = member.doc.language;
        if (Editor *editor = roomEditorForTarget(member.id))
            job.code = editor->text();
        else
            job.code = member.doc.code;
        if (member.id == mRoomClient->selfId())
            job.language = mMyRoomLanguage;
        jobs.append(job);
    }
    if (jobs.isEmpty())
        return;
    RunAllCompilers compilers;
    PCompilerSet set = pSettings->compilerSets().defaultSet();
    if (set) {
        compilers.cppCompiler = set->cppCompiler();
        compilers.cCompiler = set->CCompiler();
    }
    if (compilers.cppCompiler.isEmpty())
        compilers.cppCompiler = QStandardPaths::findExecutable("g++");
    if (compilers.cCompiler.isEmpty())
        compilers.cCompiler = QStandardPaths::findExecutable("gcc");
    compilers.pythonExe = QStandardPaths::findExecutable("python3");
    if (compilers.pythonExe.isEmpty())
        compilers.pythonExe = QStandardPaths::findExecutable("python");
    RunAllDialog dialog(jobs, compilers, this);
    dialog.exec();
}

// ---------------------------------------------------------------------------
// Part 8 — Save Code as Photo
// ---------------------------------------------------------------------------

void MainWindow::dbsSaveCodePhotoTo(Editor *editor, int scale,
                                    const QString &path)
{
    if (!editor)
        return;
    // Render the file in a throwaway QSynEdit wearing the same syntaxer,
    // font and background as the live editor, laid out at full size.
    auto *photo = new QSynedit::QSynEdit(this);
    photo->move(-100000, -100000);
    photo->setFont(editor->font());
    if (editor->syntaxer())
        photo->setSyntaxer(std::const_pointer_cast<QSynedit::Syntaxer>(
                               editor->syntaxer()));
    // Wear the live editor's whole colour set, not just the background:
    // a bare QSynEdit defaults to a garish blue active line and a light
    // gutter that would ruin the photo.
    photo->setBackgroundColor(editor->backgroundColor());
    photo->setForegroundColor(editor->foregroundColor());
    photo->setActiveLineColor(editor->activeLineColor());
    photo->setSelectedForeground(editor->selectedForeground());
    photo->setSelectedBackground(editor->selectedBackground());
    photo->gutter().setTextColor(editor->gutter().textColor());
    photo->gutter().setColor(editor->gutter().color());
    photo->gutter().setActiveLineTextColor(
                editor->gutter().activeLineTextColor());
    photo->codeFolding().folderBarLinesColor =
            editor->codeFolding().folderBarLinesColor;
    photo->codeFolding().indentGuidesColor =
            editor->codeFolding().indentGuidesColor;
    photo->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    photo->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    photo->setContent(editor->text());
    photo->setReadOnly(true);
    int lines = qMax(1, photo->lineCount());
    int longest = 0;
    const QString allText = editor->text();
    for (const QString &line : allText.split('\n'))
        longest = qMax(longest, line.size());
    int textWidth = photo->gutterWidth()
            + (longest + 4) * qMax(1, photo->charWidth());
    int textHeight = (lines + 1) * qMax(1, photo->textHeight()) + 8;
    // Safety caps so a huge file cannot allocate a monstrous image.
    textWidth = qMin(textWidth, 6000);
    textHeight = qMin(textHeight, 12000);
    photo->resize(textWidth, textHeight);
    photo->show();
    QPixmap shot = photo->grab();
    photo->hide();
    photo->deleteLater();

    const int pad = 28 * scale;
    const int footerH = 30 * scale;
    QPixmap canvas(shot.width() * scale + pad * 2,
                   shot.height() * scale + pad * 2 + footerH);
    canvas.fill(editor->backgroundColor());
    QPainter painter(&canvas);
    painter.scale(scale, scale);
    painter.drawPixmap(pad / scale, pad / scale, shot);
    painter.setPen(QColor(150, 155, 170));
    QFont footerFont(editor->font());
    footerFont.setPointSizeF(qMax(8.0, footerFont.pointSizeF() - 1.0));
    painter.setFont(footerFont);
    painter.drawText(QRect(pad / scale,
                           (canvas.height() / scale) - footerH / scale - 4,
                           shot.width(), footerH / scale),
                     Qt::AlignRight | Qt::AlignVCenter,
                     tr("Dev by Sufyan"));
    painter.end();
    if (!canvas.save(path, "PNG"))
        statusBar()->showMessage(tr("Could not save the photo to %1.")
                                 .arg(path), 6000);
}

void MainWindow::dbsSaveCodePhoto()
{
    Editor *editor = mEditorManager->getEditor();
    if (!editor) {
        statusBar()->showMessage(tr("Open a file first — the photo shows "
                                    "the current file."), 5000);
        return;
    }
    QDialog scaleDialog(this);
    scaleDialog.setWindowTitle(tr("Save Code as Photo"));
    auto *layout = new QVBoxLayout(&scaleDialog);
    layout->addWidget(new QLabel(tr("Photo size:")));
    auto *oneX = new QRadioButton(tr("Normal (1x)"));
    auto *twoX = new QRadioButton(tr("Big and sharp (2x) — best for sharing"));
    twoX->setChecked(true);
    layout->addWidget(oneX);
    layout->addWidget(twoX);
    auto *buttons = new QDialogButtonBox(
                QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &scaleDialog);
    connect(buttons, &QDialogButtonBox::accepted, &scaleDialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &scaleDialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (scaleDialog.exec() != QDialog::Accepted)
        return;
    int scale = oneX->isChecked() ? 1 : 2;
    QString defaultName = QFileInfo(editor->filename()).completeBaseName();
    if (defaultName.isEmpty())
        defaultName = "code";
    QString path = QFileDialog::getSaveFileName(
                this, tr("Save Code as Photo"), defaultName + "-photo.png",
                tr("PNG images (*.png)"));
    if (path.isEmpty())
        return;
    dbsSaveCodePhotoTo(editor, scale, path);
    statusBar()->showMessage(tr("Saved a photo of the code to %1.")
                             .arg(QFileInfo(path).fileName()), 5000);
}

// ---------------------------------------------------------------------------
// Part 9 — Flowchart from code (simplified subset)
// ---------------------------------------------------------------------------

namespace {

// --- tokenizer: split source into statements and control elements -------
struct FcEl {
    enum Type { Statement, CtrlIf, CtrlWhile, CtrlFor, DoBegin, ElseMark,
                BraceOpen, BraceClose } type;
    QString text;
};

QVector<FcEl> fcTokenize(const QString &code)
{
    QVector<FcEl> out;
    QString buf;
    auto flushStatement = [&]() {
        QString s = buf.trimmed();
        buf.clear();
        if (s.isEmpty())
            return;
        if (s.startsWith("using "))
            return; // using namespace std; — noise in a flowchart
        out.append({FcEl::Statement, s});
    };
    int i = 0;
    const int n = code.size();
    bool lineStart = true; // for preprocessor lines
    while (i < n) {
        QChar c = code[i];
        // comments
        if (c == '/' && i + 1 < n && code[i + 1] == '/') {
            while (i < n && code[i] != '\n')
                ++i;
            continue;
        }
        if (c == '/' && i + 1 < n && code[i + 1] == '*') {
            i += 2;
            while (i + 1 < n && !(code[i] == '*' && code[i + 1] == '/'))
                ++i;
            i += 2;
            continue;
        }
        // preprocessor line: drop it whole
        if (c == '#' && lineStart) {
            while (i < n && code[i] != '\n')
                ++i;
            continue;
        }
        if (c == '\n') {
            buf += ' ';
            lineStart = true;
            ++i;
            continue;
        }
        if (!c.isSpace())
            lineStart = false;
        // string / char literals: copy verbatim, never structural
        if (c == '"' || c == '\'') {
            QChar quote = c;
            buf += c;
            ++i;
            while (i < n && code[i] != quote) {
                if (code[i] == '\\' && i + 1 < n) {
                    buf += code[i];
                    buf += code[i + 1];
                    i += 2;
                } else {
                    buf += code[i];
                    ++i;
                }
            }
            if (i < n) {
                buf += code[i];
                ++i;
            }
            continue;
        }
        // a word: maybe a control keyword (only at a clean boundary)
        if ((c.isLetter() || c == '_') && buf.trimmed().isEmpty()) {
            int j = i;
            while (j < n && (code[j].isLetterOrNumber() || code[j] == '_'))
                ++j;
            QString word = code.mid(i, j - i);
            if (word == "if" || word == "while" || word == "for") {
                int k = j;
                while (k < n && code[k].isSpace())
                    ++k;
                if (k < n && code[k] == '(') {
                    // consume the balanced ( ... ) as the condition
                    int depth = 0;
                    int m = k;
                    QString cond;
                    while (m < n) {
                        QChar ch = code[m];
                        if (ch == '(') {
                            if (depth > 0)
                                cond += ch;
                            ++depth;
                        } else if (ch == ')') {
                            --depth;
                            if (depth == 0) {
                                ++m;
                                break;
                            }
                            cond += ch;
                        } else {
                            cond += ch;
                        }
                        ++m;
                    }
                    FcEl::Type type = word == "if" ? FcEl::CtrlIf
                                      : word == "while" ? FcEl::CtrlWhile
                                                        : FcEl::CtrlFor;
                    out.append({type, cond.trimmed()});
                    i = m;
                    continue;
                }
            } else if (word == "do") {
                out.append({FcEl::DoBegin, QString()});
                i = j;
                continue;
            } else if (word == "else") {
                out.append({FcEl::ElseMark, QString()});
                i = j;
                continue;
            }
            buf += word;
            i = j;
            continue;
        }
        if (c == ';') {
            flushStatement();
            ++i;
            continue;
        }
        if (c == '{') {
            flushStatement();
            out.append({FcEl::BraceOpen, QString()});
            ++i;
            continue;
        }
        if (c == '}') {
            flushStatement();
            out.append({FcEl::BraceClose, QString()});
            ++i;
            continue;
        }
        buf += c;
        ++i;
    }
    flushStatement();
    return out;
}

// --- parser: elements -> flowchart items ---------------------------------
struct FcItem {
    enum Kind { Simple, If, Loop, DoLoop } kind = Simple;
    enum SimpleKind { Process, Input, Output, Return } simpleKind = Process;
    QString text;
    bool hasElse = false;
    QVector<FcItem> thenItems, elseItems, bodyItems;
};

struct FcParser {
    QVector<FcEl> els;
    int pos = 0;

    static FcItem classify(const QString &raw)
    {
        FcItem item;
        item.text = raw;
        static const QRegularExpression reIn("\\bcin\\s*>>");
        static const QRegularExpression reOut("\\bcout\\s*<<");
        if (reIn.match(raw).hasMatch())
            item.simpleKind = FcItem::Input;
        else if (reOut.match(raw).hasMatch())
            item.simpleKind = FcItem::Output;
        else if (raw.startsWith("return"))
            item.simpleKind = FcItem::Return;
        return item;
    }

    void parseBranch(QVector<FcItem> &out)
    {
        if (pos >= els.size())
            return;
        if (els[pos].type == FcEl::BraceOpen) {
            ++pos;
            out = parseSeq();
        } else if (els[pos].type != FcEl::BraceClose
                   && els[pos].type != FcEl::ElseMark) {
            out.append(parseOne());
        }
    }

    FcItem parseOne()
    {
        const FcEl el = els[pos++];
        FcItem item;
        switch (el.type) {
        case FcEl::Statement:
            return classify(el.text);
        case FcEl::CtrlIf: {
            item.kind = FcItem::If;
            item.text = el.text;
            parseBranch(item.thenItems);
            if (pos < els.size() && els[pos].type == FcEl::ElseMark) {
                ++pos;
                item.hasElse = true;
                if (pos < els.size() && els[pos].type == FcEl::CtrlIf)
                    item.elseItems.append(parseOne());
                else
                    parseBranch(item.elseItems);
            }
            return item;
        }
        case FcEl::CtrlWhile:
        case FcEl::CtrlFor: {
            item.kind = FcItem::Loop;
            item.text = (el.type == FcEl::CtrlFor ? "for: " : "") + el.text;
            parseBranch(item.bodyItems);
            return item;
        }
        case FcEl::DoBegin: {
            item.kind = FcItem::DoLoop;
            parseBranch(item.bodyItems);
            if (pos < els.size() && els[pos].type == FcEl::CtrlWhile) {
                item.text = els[pos].text;
                ++pos;
            }
            return item;
        }
        default:
            item.text = el.text;
            return item;
        }
    }

    QVector<FcItem> parseSeq()
    {
        QVector<FcItem> items;
        while (pos < els.size()) {
            const FcEl &el = els[pos];
            if (el.type == FcEl::BraceClose) {
                ++pos;
                break;
            }
            if (el.type == FcEl::BraceOpen) {
                // A bare block (function body, plain { }): its contents
                // join the sequence in place — simplified on purpose.
                ++pos;
                items += parseSeq();
                continue;
            }
            if (el.type == FcEl::ElseMark) {
                ++pos; // stray else; its if already consumed it normally
                continue;
            }
            items.append(parseOne());
        }
        return items;
    }
};

// --- drawing ---------------------------------------------------------------
const int FC_GAP = 26;
const int FC_MAXW = 460;

class FlowchartWidget : public QWidget
{
public:
    explicit FlowchartWidget(QVector<FcItem> items, QWidget *parent = nullptr)
        : QWidget(parent), mItems(std::move(items))
    {
        QFont f = font();
        f.setPointSize(10);
        setFont(f);
        mSize = measureSeq(mItems);
        mSize += QSize(120, 150); // margins + Start/End stadiums
        setFixedSize(mSize);
        setStyleSheet("background: white;");
    }

    QSize contentSize() const { return mSize; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(20, 32, 46), 1.4));
        int cx = width() / 2;
        int y = 24;
        drawStadium(p, cx, y, "Start");
        y += 34;
        drawArrow(p, cx, y, cx, y + FC_GAP);
        y += FC_GAP;
        drawSeq(p, mItems, cx - measureSeq(mItems).width() / 2, y);
        y += measureSeq(mItems).height();
        drawArrow(p, cx, y, cx, y + FC_GAP);
        y += FC_GAP;
        drawStadium(p, cx, y, "End");
    }

private:
    QVector<FcItem> mItems;
    QSize mSize;

    int textHeightFor(const QString &text, int w) const
    {
        QFontMetrics fm(font());
        QRect r = fm.boundingRect(QRect(0, 0, qMax(40, w), 0),
                                  Qt::TextWordWrap, text);
        return r.height();
    }
    int textWidthFor(const QString &text) const
    {
        return QFontMetrics(font()).horizontalAdvance(text);
    }

    QSize measureSimple(const FcItem &item) const
    {
        int inner = FC_MAXW - 48;
        int w = qMin(FC_MAXW, textWidthFor(item.text) + 48);
        w = qMax(w, 180);
        int h = textHeightFor(item.text, inner) + 18;
        if (item.simpleKind == FcItem::Input
                || item.simpleKind == FcItem::Output)
            w += 18;
        return {w, h};
    }
    QSize measureDiamond(const QString &text) const
    {
        int tw = qMin(240, textWidthFor(text));
        int w = tw + 90;
        int h = textHeightFor(text, tw) + 52;
        return {qMax(w, 170), qMax(h, 74)};
    }
    QSize measureSeq(const QVector<FcItem> &items) const
    {
        int w = 0, h = 0;
        for (const FcItem &item : items) {
            QSize s = measure(item);
            w = qMax(w, s.width());
            h += s.height();
        }
        if (items.size() > 1)
            h += FC_GAP * (items.size() - 1);
        return {w, h};
    }
    QSize measure(const FcItem &item) const
    {
        switch (item.kind) {
        case FcItem::Simple:
            return measureSimple(item);
        case FcItem::If: {
            QSize d = measureDiamond(item.text);
            QSize t = measureSeq(item.thenItems);
            QSize e = item.hasElse ? measureSeq(item.elseItems) : QSize(0, 0);
            int w = item.hasElse
                    ? qMax(d.width(), t.width() + 34 + e.width())
                    : qMax(d.width(), t.width());
            int h = d.height() + FC_GAP + qMax(t.height(), e.height()) + FC_GAP;
            return {w, h};
        }
        case FcItem::Loop: {
            QSize d = measureDiamond(item.text);
            QSize b = measureSeq(item.bodyItems);
            int w = qMax(d.width(), b.width()) + 30;
            int h = d.height() + FC_GAP + b.height() + 14;
            return {w, h};
        }
        case FcItem::DoLoop: {
            QSize d = measureDiamond(item.text);
            QSize b = measureSeq(item.bodyItems);
            int w = qMax(d.width(), b.width()) + 30;
            int h = b.height() + FC_GAP + d.height() + 14;
            return {w, h};
        }
        }
        return {180, 40};
    }

    void drawArrow(QPainter &p, int x1, int y1, int x2, int y2) const
    {
        p.drawLine(x1, y1, x2, y2);
        const int s = 5;
        if (y2 > y1 && x1 == x2) { // downwards
            p.drawLine(x2, y2, x2 - s, y2 - s * 2);
            p.drawLine(x2, y2, x2 + s, y2 - s * 2);
        } else if (x2 > x1 && y1 == y2) { // rightwards
            p.drawLine(x2, y2, x2 - s * 2, y2 - s);
            p.drawLine(x2, y2, x2 - s * 2, y2 + s);
        } else if (x2 < x1 && y1 == y2) { // leftwards
            p.drawLine(x2, y2, x2 + s * 2, y2 - s);
            p.drawLine(x2, y2, x2 + s * 2, y2 + s);
        } else if (y2 < y1 && x1 == x2) { // upwards
            p.drawLine(x2, y2, x2 - s, y2 + s * 2);
            p.drawLine(x2, y2, x2 + s, y2 + s * 2);
        }
    }
    void drawLabel(QPainter &p, const QString &text, int x, int y) const
    {
        QFont f = p.font();
        f.setPointSize(8);
        p.save();
        p.setFont(f);
        p.drawText(x, y, text);
        p.restore();
    }
    void drawTextIn(QPainter &p, const QRect &rect, const QString &text) const
    {
        p.drawText(rect, Qt::AlignCenter | Qt::TextWordWrap, text);
    }
    void drawStadium(QPainter &p, int cx, int y, const QString &text) const
    {
        QRect r(cx - 55, y, 110, 34);
        p.setBrush(QColor(230, 244, 234));
        p.drawRoundedRect(r, 17, 17);
        drawTextIn(p, r, text);
    }
    void drawSimple(QPainter &p, const FcItem &item, int x, int y) const
    {
        QSize s = measureSimple(item);
        QRect r(x, y, s.width(), s.height());
        switch (item.simpleKind) {
        case FcItem::Input:
        case FcItem::Output: {
            p.setBrush(QColor(234, 243, 255));
            const int slant = 14;
            QPolygon poly;
            poly << QPoint(r.left() + slant, r.top()) << r.topRight()
                 << QPoint(r.right() - slant, r.bottom()) << r.bottomLeft();
            p.drawPolygon(poly);
            drawTextIn(p, r.adjusted(16, 2, -16, -2), item.text);
            break;
        }
        case FcItem::Return:
            p.setBrush(QColor(230, 244, 234));
            p.drawRoundedRect(r, 8, 8);
            drawTextIn(p, r.adjusted(8, 2, -8, -2), item.text);
            break;
        default:
            p.setBrush(QColor(255, 255, 255));
            p.drawRect(r);
            drawTextIn(p, r.adjusted(8, 2, -8, -2), item.text);
            break;
        }
    }
    void drawDiamond(QPainter &p, const FcItem &item, int cx, int y) const
    {
        QSize s = measureDiamond(item.text);
        QPolygon poly;
        poly << QPoint(cx, y) << QPoint(cx + s.width() / 2, y + s.height() / 2)
             << QPoint(cx, y + s.height())
             << QPoint(cx - s.width() / 2, y + s.height() / 2);
        p.setBrush(QColor(255, 247, 224));
        p.drawPolygon(poly);
        QRect textRect(cx - s.width() / 4 - 18, y + 8,
                       s.width() / 2 + 36, s.height() - 16);
        drawTextIn(p, textRect, item.text);
    }

    void drawSeq(QPainter &p, const QVector<FcItem> &items, int x, int y) const
    {
        QSize total = measureSeq(items);
        int cy = y;
        for (int i = 0; i < items.size(); ++i) {
            QSize s = measure(items[i]);
            draw(p, items[i], x + (total.width() - s.width()) / 2, cy);
            cy += s.height();
            if (i + 1 < items.size()) {
                int cx = x + total.width() / 2;
                // entry x of the NEXT item may differ; use column center
                drawArrow(p, cx, cy, cx, cy + FC_GAP);
                cy += FC_GAP;
            }
        }
    }

    void draw(QPainter &p, const FcItem &item, int x, int y) const
    {
        QSize s = measure(item);
        int cx = x + s.width() / 2;
        switch (item.kind) {
        case FcItem::Simple:
            drawSimple(p, item, x, y);
            break;
        case FcItem::If: {
            QSize d = measureDiamond(item.text);
            drawDiamond(p, item, cx, y);
            int branchesY = y + d.height() + FC_GAP;
            QSize t = measureSeq(item.thenItems);
            QSize e = item.hasElse ? measureSeq(item.elseItems) : QSize(0, 0);
            int branchH = qMax(t.height(), e.height());
            int bottomY = branchesY + branchH + FC_GAP;
            if (item.hasElse) {
                int thenCx = x + t.width() / 2;
                int elseCx = x + s.width() - e.width() / 2;
                int midY = y + d.height() / 2;
                // left vertex -> then column
                p.drawLine(cx - d.width() / 2, midY, thenCx, midY);
                drawArrow(p, thenCx, midY, thenCx, branchesY);
                drawLabel(p, "Yes", thenCx + 6, midY - 6);
                drawSeq(p, item.thenItems, x, branchesY);
                // right vertex -> else column
                p.drawLine(cx + d.width() / 2, midY, elseCx, midY);
                drawArrow(p, elseCx, midY, elseCx, branchesY);
                drawLabel(p, "No", elseCx + 6, midY - 6);
                drawSeq(p, item.elseItems, x + s.width() - e.width(), branchesY);
                // branches -> merge
                p.drawLine(thenCx, branchesY + t.height(), thenCx, bottomY - FC_GAP);
                p.drawLine(elseCx, branchesY + e.height(), elseCx, bottomY - FC_GAP);
                p.drawLine(thenCx, bottomY - FC_GAP, elseCx, bottomY - FC_GAP);
                drawArrow(p, cx, bottomY - FC_GAP, cx, bottomY);
            } else {
                drawLabel(p, "Yes", cx + 8, y + d.height() + 16);
                drawSeq(p, item.thenItems, cx - t.width() / 2, branchesY);
                // No path: right vertex down the right side to the merge
                int noX = x + s.width() - 2;
                p.drawLine(cx + d.width() / 2, y + d.height() / 2, noX, y + d.height() / 2);
                p.drawLine(noX, y + d.height() / 2, noX, bottomY - FC_GAP);
                drawLabel(p, "No", noX - 24, y + d.height() / 2 - 6);
                p.drawLine(cx, branchesY + t.height(), cx, bottomY - FC_GAP);
                p.drawLine(cx, bottomY - FC_GAP, noX, bottomY - FC_GAP);
                drawArrow(p, cx, bottomY - FC_GAP, cx, bottomY);
            }
            break;
        }
        case FcItem::Loop: {
            QSize d = measureDiamond(item.text);
            int dcx = cx + 15; // leave a channel on the left for the back edge
            drawDiamond(p, item, dcx, y);
            QSize b = measureSeq(item.bodyItems);
            int bodyY = y + d.height() + FC_GAP;
            drawLabel(p, "Yes", dcx + 8, y + d.height() + 16);
            drawSeq(p, item.bodyItems, dcx - b.width() / 2, bodyY);
            // back edge: body bottom -> left channel -> diamond left vertex
            int backX = x + 4;
            int bodyBottom = bodyY + b.height();
            p.drawLine(dcx, bodyBottom, dcx, bodyBottom + 10);
            p.drawLine(dcx, bodyBottom + 10, backX, bodyBottom + 10);
            p.drawLine(backX, bodyBottom + 10, backX, y + d.height() / 2);
            drawArrow(p, backX, y + d.height() / 2, dcx - d.width() / 2, y + d.height() / 2);
            // No exit: right vertex down to below the body
            drawLabel(p, "No", dcx + d.width() / 2 + 6, y + d.height() / 2 - 6);
            break;
        }
        case FcItem::DoLoop: {
            QSize b = measureSeq(item.bodyItems);
            int dcx = cx + 15;
            drawSeq(p, item.bodyItems, dcx - b.width() / 2, y);
            QSize d = measureDiamond(item.text);
            int dy = y + b.height() + FC_GAP;
            drawDiamond(p, item, dcx, dy);
            // repeat edge: diamond left vertex -> left channel -> body top
            int backX = x + 4;
            p.drawLine(dcx - d.width() / 2, dy + d.height() / 2, backX, dy + d.height() / 2);
            p.drawLine(backX, dy + d.height() / 2, backX, y - 6);
            drawArrow(p, backX, y - 6, dcx - b.width() / 2, y - 6);
            p.drawLine(backX, y - 6, dcx, y - 6);
            drawLabel(p, "again", backX + 6, dy + d.height() / 2 - 6);
            break;
        }
        }
    }
};

} // namespace

FlowchartDialog::FlowchartDialog(const QString &code, const QString &title,
                                 QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Flowchart — %1").arg(title));
    resize(860, 720);
    FcParser parser;
    parser.els = fcTokenize(code);
    QVector<FcItem> items = parser.parseSeq();
    auto *chart = new FlowchartWidget(items, this);

    auto *layout = new QVBoxLayout(this);
    auto *scroll = new QScrollArea;
    scroll->setWidget(chart);
    scroll->setWidgetResizable(false);
    scroll->setAlignment(Qt::AlignHCenter);
    layout->addWidget(scroll);
    auto *row = new QHBoxLayout;
    auto *note = new QLabel(
                tr("Simplified flowchart: sequence, if/else, loops and "
                   "input/output. Unusual code appears as plain steps."));
    note->setWordWrap(true);
    row->addWidget(note, 1);
    auto *saveButton = new QPushButton(tr("Save as PNG"));
    auto *closeButton = new QPushButton(tr("Close"));
    row->addWidget(saveButton);
    row->addWidget(closeButton);
    layout->addLayout(row);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(saveButton, &QPushButton::clicked, this, [this, chart]() {
        QString path = QFileDialog::getSaveFileName(
                    this, tr("Save flowchart"), "flowchart.png",
                    tr("PNG images (*.png)"));
        if (path.isEmpty())
            return;
        // Render at 2x for a crisp image.
        QSize size = chart->contentSize();
        QPixmap pixmap(size * 2);
        pixmap.fill(Qt::white);
        QPainter painter(&pixmap);
        painter.scale(2, 2);
        chart->render(&painter);
        painter.end();
        pixmap.save(path, "PNG");
    });
}

void MainWindow::dbsShowFlowchart()
{
    Editor *editor = mEditorManager->getEditor();
    if (!editor) {
        statusBar()->showMessage(tr("Open a file first — the flowchart is "
                                    "drawn from the current file."), 5000);
        return;
    }
    auto *dialog = new FlowchartDialog(
                editor->text(), QFileInfo(editor->filename()).fileName(), this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}
