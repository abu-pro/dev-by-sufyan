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
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QProgressDialog>
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
#include "../compiler/compilermanager.h"
#include "../settings.h"
#include "dbsfeatures.h"
#include "roomclient.h"
#include "roomspanel.h"
#include "welcomepage.h"
#include "qsynedit/qsynedit.h"

namespace {

// The one place the running version is named for the update checker;
// the welcome page shows the same number (see setupWelcomePage).
const char *const DBS_VERSION = "1.5.1";
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
    // v1.5.1: must sit before the plain "incompatible types" entry —
    // pointer mix-ups (missing &, extra *) deserve their own words.
    { "incompatible pointer",
      "A memory address and a different kind of address are mixed (C)",
      "In C, an int* (address of an int) and a char* (address of text) are different kinds of address. You gave one where the other was needed — often a missing & or an extra * in a scanf/printf call, or an array handed over the wrong way.",
      "Check the variable's declared type against what the function wants. For scanf of a number you must give the address: scanf(\"%d\", &x); For an array/text you give the name itself: scanf(\"%s\", name);" },
    // ---- Python errors (v1.5.0) — the messages Python itself prints.
    { "'return' outside function|'break' outside loop|'continue' not properly in loop|cannot assign to literal|cannot assign to function call",
      "This statement is standing in the wrong place (Python)",
      "Some statements only work in their own home: return only inside a def, break/continue only inside a loop. And a value can only be stored INTO a variable — you cannot assign to a plain number or to a function call.",
      "Move the statement inside the right block, or store the result into a variable name first (total = ...). If you wanted to COMPARE two things, remember: = stores, == compares." },
    // ---- Python specifics (v1.5.1) — these must sit BEFORE the plain
    // "SyntaxError" entry below, or that one would swallow them all.
    { "SyntaxError: expected ':'",
      "A colon (:) is missing at the end of the line (Python)",
      "Lines that open a block — if, elif, else, for, while, def, class, try, except, with — must end with a colon. Python read this line and the colon was not there.",
      "Add ':' at the very end of that line. Nothing should come after the colon on the same line." },
    { "Missing parentheses in call to",
      "print needs round brackets (Python)",
      "In today's Python, print is a function, so the values go inside round brackets: print(x). Writing print x is the old Python 2 style and is not allowed any more.",
      "Put the values inside brackets: print(\"Hello\"), print(total)." },
    { "invalid character",
      "A strange character is hiding in the code (Python)",
      "The code contains a character Python does not accept — very often 'smart quotes' copied from a document or a chat, or another invisible symbol. The message even shows which character it is, like (U+201C).",
      "Retype the punctuation on that line yourself with the keyboard: normal quotes and apostrophes, never the curly ones from Word or WhatsApp." },
    { "was never closed|unexpected EOF while parsing",
      "A bracket was opened but never closed (Python)",
      "Somewhere above, a ( or [ or { was opened and its closing partner never came. Python reached the end of the file still waiting for it — the message points at the bracket that was left open.",
      "Find the bracket the message points at and count its partners: every ( needs a ), every [ a ], every { a }. Add the missing closing bracket at the right place." },
    { "leading zeros in decimal",
      "A number starts with a zero (Python)",
      "Writing a number like 007 or 0123 is not allowed in Python — it once meant a different number system (octal) and caused silent mistakes, so Python refuses it.",
      "Remove the starting zero: write 7 or 123. If the zeros were part of a code or text (like '007'), put it in quotes so it is text." },
    { "global declaration",
      "The global statement is standing too late (Python)",
      "Inside a function you wrote 'global x', but x was already used or assigned above that line in the same function. Python wants the global promise before the name is touched.",
      "Move the 'global x' line to the very top of the function, before any line that uses x." },
    { "duplicate argument",
      "The same parameter name is written twice (Python)",
      "In the def line, one parameter name appears two times, like def f(a, a): — Python cannot tell which one a value belongs to.",
      "Give each parameter its own different name in the def line, and use the matching names inside the function." },
    { "non-default argument follows default",
      "A normal parameter is standing after a defaulted one (Python)",
      "Once a parameter gets a default value in the def line, every parameter after it must also have a default. A plain parameter after a defaulted one leaves Python unsure which value goes where.",
      "Either give the later parameter a default too, or move the defaulted parameters to the end of the def line." },
    { "argument follows keyword argument|keyword argument repeated",
      "The call mixes values in the wrong order (Python)",
      "In a function call, plain values (positional) must come first; values written as name=value come after. Or the same name=value was given twice in one call.",
      "Put the plain values first and the name=value ones after, and give each name only once: f(10, b=20)." },
    { "starred expression|can't use starred",
      "The * is used in a place Python does not allow",
      "A * before a value (unpacking) only works in special places: in a call like f(*items), inside a list like [*a, *b], or on the left side of =. Standing alone in an expression it has no meaning.",
      "Use the * only where unpacking makes sense, or remove it. If you meant multiplication, something must stand on both sides: a * b." },
    { "cannot assign to expression|cannot assign to operator",
      "The left side of = cannot hold a value (Python)",
      "The left side of = must be a name (or a place in a list/dictionary). Here it is a calculation or expression — Python cannot store a result INTO a calculation.",
      "Store into a variable name first: result = a + b. And if you meant to compare two things, use == instead of =." },
    { "free variable",
      "An inner function uses a name from outside too early (Python)",
      "A function inside another function reads a name from the outer function, but the outer function only gives that name its value after the inner one runs — or the inner function assigns to it without saying 'nonlocal'.",
      "Give the outer name its value before the inner function is called. If the inner function must change it, write 'nonlocal name' at the top of the inner function." },
    { "'yield' outside|'await' outside",
      "yield / await used outside their home (Python)",
      "yield only works inside a generator function (a def), and await only inside an async def. Used anywhere else, Python stops immediately.",
      "Move the yield inside a def, or the await inside an 'async def'. If you only wanted to pause the program, time.sleep(seconds) is the plain way." },
    { "SyntaxError",
      "Python cannot understand this line (SyntaxError)",
      "Python read your code and one line does not follow Python's grammar — a missing colon after if/for/def, a missing or extra bracket, or '=' used where '==' was meant are the usual causes. The ^ marker in the message points at the spot.",
      "Look where the ^ points, and also at the line ABOVE it. Check: does the if/for/while/def line end with ':'? Are all brackets closed? Did you write = (store) where you meant == (compare)?" },
    { "IndentationError|TabError|unexpected indent|expected an indented block|unindent does not match",
      "The spaces at the line start are wrong (Python indentation)",
      "In Python the spaces at the start of a line are part of the code: lines inside if/for/def must be pushed in by the same amount, and lines outside must line up. Here the spacing does not match the block around it — or tabs and spaces are mixed.",
      "Make every line of the same block start with exactly the same spaces (4 is usual). Do not mix Tab and spaces. The line after a line ending with ':' must be indented one level more." },
    { "NameError",
      "Python does not know this name (NameError)",
      "You used a variable or function name that was never created — a typing mistake, a name used before its first value, or a function from a module you forgot to import.",
      "Check the spelling and capital letters (Python is case-sensitive: Total and total are different names). Make sure the name gets a value ABOVE this line. Watch out: the logical words are True, False and None — with a capital first letter." },
    { "UnboundLocalError|referenced before assignment|cannot access local variable",
      "The variable is used before it gets its value (Python)",
      "Inside a function, this variable is read before any line gives it a value. Python also treats a name as belonging to the function as soon as it is assigned anywhere in the function — so reading an outside variable with the same name and assigning to it later also triggers this.",
      "Give the variable a starting value at the top of the function. If you really mean the variable from outside the function and also change it inside, write 'global name' at the top of the function." },
    { "object is not callable",
      "You called something that is not a function (Python)",
      "You wrote name(...) but 'name' is not a function — often a variable with the same name is hiding the function (for example a variable called list, sum or print), or ( ) was put after a plain value by mistake.",
      "Rename your variable so it does not cover a built-in name like list, str, sum, max or print. Remember: square brackets [ ] take an item out of a list; round brackets ( ) call a function." },
    { "unsupported operand|not supported between instances",
      "These two values cannot be combined like that (Python)",
      "The operation mixes types that do not fit — the classic case is adding a number to text ('5' + 3) or comparing text with a number. Python never converts silently here.",
      "Make both sides the same kind first: int(x) or float(x) turns text into a number; str(x) turns a number into text. Text from input() is always text — convert it with int() before doing maths." },
    { "can only concatenate",
      "Text can only be joined with text (Python)",
      "With +, Python joins text to text only. One side here is a number (or another type), and Python refuses to guess the conversion.",
      "Turn the number into text first: 'Total: ' + str(total). Or use an f-string, which does it for you: f'Total: {total}'." },
    { "object is not subscriptable",
      "Square brackets used on something without items (Python)",
      "You wrote value[index], but 'value' is a single number, None or similar — it has no items inside to pick from. A common cause: a function that returns nothing (None) was stored and then indexed.",
      "Check what the variable really holds (print it). Indexing with [ ] works on lists, strings, tuples and dictionaries. If the value came from a function, make the function return what you expected." },
    { "indices must be integers|slice indices must be integers",
      "The index inside [ ] must be a whole number (Python)",
      "For lists and strings, the position inside the square brackets must be an integer. Here it is text or a decimal number — very often a value that came from input() and was never converted.",
      "Convert the index with int(...). If it came from input(), remember input() always gives text." },
    { "takes \\d+ positional argument|takes no arguments|missing \\d+ required positional argument|got an unexpected keyword argument|got multiple values for argument",
      "The function call has the wrong arguments (Python)",
      "The call does not match the function's def line: too many values, too few, or a keyword the function does not have. When the message says it 'takes 2 but 3 were given', the hidden extra is often 'self' being counted — or a method called on the class instead of on an object.",
      "Compare the call with the def line, argument by argument, in order. For methods, call them on an object (obj.method(...)) so 'self' is filled in automatically." },
    // ---- More Python specifics (v1.5.1) — these must sit BEFORE the
    // plain "TypeError" and "ValueError" entries below, or those
    // generic entries would swallow them.
    { "not enough values to unpack|too many values to unpack|cannot unpack",
      "The two sides of = do not have the same count (Python)",
      "With a line like a, b = values, the number of names on the left must match the number of values on the right. Here they do not — too few values came, too many came, or the right side cannot be unpacked at all.",
      "Print or check the right side first to see how many values it really has, then make the left side match it exactly." },
    { "math domain error",
      "That maths is not possible with these numbers (Python)",
      "The math module was asked for something undefined for real numbers — like the square root of a negative number, or the log of zero or a negative number.",
      "Check the value before the call: math.sqrt needs 0 or more, math.log needs more than 0. Use abs() if the sign was not meant to matter." },
    { "substring not found",
      "The text you searched for is not inside (Python)",
      "str.index() was asked to find a piece of text that is not there. index() complains loudly when it cannot find it — its calmer brother find() returns -1 instead.",
      "Use find() when 'not found' is a normal possibility, or check first: if piece in text: ... (spelling and capital letters must match exactly)." },
    { "not in list",
      "That value is not in the list (Python)",
      "list.remove(x) or list.index(x) was given a value the list does not contain. Maybe the spelling or capital letters differ, or the value was never added.",
      "Check membership before removing or searching: if x in items: items.remove(x). Print the list once to see what is really inside." },
    { "is not iterable",
      "That value cannot be looped over (Python)",
      "A for loop, an unpacking, or a function like list()/sum() was given a single plain value (like a number or None) where a collection — list, text, range — was expected. A single number has no items to walk through.",
      "Check what the variable really holds. To loop a counted number of times use range(n); to loop over the digits/characters of a value, turn it into text first: str(value)." },
    { "has no len\\(\\)",
      "len() was used on something with no length (Python)",
      "len() only works on things with items or characters: text, list, tuple, dictionary. A plain number or None has no length.",
      "If you want the number of digits of a number, turn it into text first: len(str(number)). Otherwise remove the len() call." },
    { "multiply sequence",
      "Text or a list can only be repeated by a whole number (Python)",
      "In Python, 'ab' * 3 repeats text — but the multiplier must be a whole number. Here it was text or a decimal, so Python does not know how many times to repeat.",
      "Make the count a whole number: int(count). If you wanted normal maths multiplication, both sides must be numbers, not text." },
    { "bad operand type",
      "That sign cannot be used on this kind of value (Python)",
      "A sign like - or ~ was put in front of a value it does not work on — for example -'text'. Minus needs a number.",
      "Turn the value into a number first if that is what you meant: -int(text). Otherwise remove the sign." },
    { "unhashable type",
      "A list cannot be a dictionary key or set member (Python)",
      "Dictionary keys and set members must be unchangeable (hashable). A list can change, so it is not allowed as a key — that is what 'unhashable' means here.",
      "Use a tuple instead of a list for the key: (1, 2) instead of [1, 2]. Or use a text or number that names the thing." },
    { "does not support item assignment",
      "That value cannot be changed piece by piece (Python)",
      "You tried to change one item inside something unchangeable — like text (str) or a tuple: text[0] = 'A' is not allowed, because text never changes in place.",
      "Build a new value instead: new_text = 'A' + text[1:]. For things you need to change often, use a list instead of a tuple or text." },
    { "cannot be interpreted as an integer",
      "A whole number was needed here (Python)",
      "Something like range(), a list index, or a size asked for a whole number, and a decimal (float) or other value was given.",
      "Round it or cut the decimal part: int(value). If a whole number was expected, check the calculation that produced the decimal." },
    { "bytes-like object is required",
      "Bytes were needed, but text was given (Python)",
      "This place (often a file opened with 'b', or bytes work) needs raw bytes, and normal text (str) was given. Python 3 keeps text and bytes strictly separate.",
      "Turn text into bytes with text.encode(), or open the file in text mode (without 'b') if you only handle text." },
    { "ord\\(\\) expected",
      "ord() wants exactly one character (Python)",
      "ord() gives the code number of ONE single character. It was given text that is empty or longer than one character.",
      "Give ord() one character only — for a longer text, loop over it: for ch in text: print(ord(ch))." },
    { "expected str instance",
      "join() can only join pieces of text (Python)",
      "Something like ' '.join(items) needs every item to be text. One of the items is a number or another type, and join refuses to guess how to write it.",
      "Turn every piece into text first: ' '.join(str(x) for x in items), or make the values text when you create them." },
    { "unsupported format character",
      "A wrong letter after % in a format text (Python)",
      "In old-style text formatting ('%d' % value), only certain letters may follow %: %d number, %s anything, %f decimal. Another letter was used, and Python does not know it.",
      "Use %d for whole numbers, %f for decimals, %s for anything — or switch to an f-string: f\"value is {value}\"." },
    { "not all arguments converted",
      "Too many values for the % places in the text (Python)",
      "In old-style formatting ('%d and %d' % values), the number of % places in the text must match the number of values exactly. Here extra values were left over at the end.",
      "Count the % places in the text and give exactly that many values, or add the missing %d/%s places for the extra values." },
    { "update sequence",
      "The dictionary was fed values in the wrong shape (Python)",
      "dict(...) or dict.update(...) expects pairs like (key, value). One of the items it received is not a pair of exactly two things.",
      "Give pairs: dict([('a', 1), ('b', 2)]) — or build the dictionary step by step: d[key] = value." },
    { "exceptions must derive|exceptions must be classes or instances",
      "raise was given something that is not an error (Python)",
      "After raise you may only put an exception — a built-in one like ValueError('message'), or your own class built from Exception. A number, text, or normal class cannot be raised.",
      "Raise a real exception: raise ValueError('what went wrong')." },
    { "not enough arguments for format",
      "Too few values for the % places in the text (Python)",
      "In old-style formatting, the text has more % places than the values given, so one %d/%s has nothing to show.",
      "Give one value for every % place in the text — with brackets around the values when there are several: '%d and %d' % (a, b)." },
    { "TypeError",
      "A value of the wrong type was used (Python TypeError)",
      "Python was given a value of a kind it cannot use in this place — like len() of a number, sorting a mix of text and numbers, or trying to change something that cannot be changed (a tuple).",
      "Print the value and its type with type(x) to see what you really have. Then convert it (int, float, str, list) or change the code to match the type." },
    { "invalid literal for int|invalid literal for float|could not convert string to float",
      "Text that is not a number was turned into a number (Python)",
      "int(...) or float(...) received text that does not look like a number — letters, an empty string, or decimal text like '3.5' given to int().",
      "Check what the text really contains (print it with print(repr(x)) to see hidden spaces). For decimal text use float(...) first, or clean the input before converting." },
    { "ValueError",
      "The value is the right kind, but not allowed (Python ValueError)",
      "The type was fine, but the value itself is not acceptable here — for example unpacking a list with too few items into too many names, or a maths domain error like the square root of a negative number.",
      "Read the message after 'ValueError:' — it names the exact problem. Print the value just before this line and check why it is outside what the function allows." },
    { "IndexError|index out of range",
      "That position does not exist in the list (Python IndexError)",
      "The index is bigger than the last position (or too far negative). Remember: a list with 5 items has positions 0 to 4 — position 5 is already outside.",
      "Check the length with len(my_list) and compare your index. Loops usually want range(len(my_list)). An empty list has no valid position at all." },
    { "KeyError",
      "That key is not in the dictionary (Python KeyError)",
      "You asked the dictionary for a key it does not contain — a spelling difference, different capital letters, or the key was never added.",
      "Print the dictionary (or its keys with my_dict.keys()) and compare the exact spelling. If the key may be missing on purpose, use my_dict.get(key, default) instead of my_dict[key]." },
    { "AttributeError",
      "That thing does not have this member (Python AttributeError)",
      "You wrote object.something, but the object has no member with that name — a typo, or the object is not the type you think (for example None, because a function returned nothing).",
      "Check the spelling, and print type(object) to be sure what it is. If it is None, the function that produced it needs a return statement." },
    { "ZeroDivisionError",
      "Division by zero (Python)",
      "A /, // or % had zero on the right side, and Python stops the program instead of guessing an answer.",
      "Check the dividing value before dividing (if b != 0: ...) and find out why it became zero — often an empty input or a counter that never started." },
    { "ModuleNotFoundError|No module named|ImportError|cannot import name",
      "A module could not be imported (Python)",
      "Python cannot find the module in the import line — its name is misspelled, it is not installed, or your own file has the same name as a library module and hides it (a file called random.py or math.py does exactly this). 'cannot import name' means the module WAS found but that name is not inside it.",
      "Check the spelling. Rename your own file if it matches a library name (and delete the __pycache__ folder next to it). Install outside libraries with: pip install name. For 'cannot import name', check the exact name inside the module." },
    { "FileNotFoundError",
      "Python cannot find that file",
      "The program tried to open a file that is not at the given place. Python looks relative to the folder the program RUNS from — which is not always the folder the .py file sits in.",
      "Check the file name spelling and the folder. Give the full path, or run the program from the folder where the file lives. In Windows paths use forward slashes or double backslashes: 'C:/data/file.txt'." },
    { "PermissionError",
      "The file is busy or protected (Python PermissionError)",
      "Python was not allowed to open or write the file — very often because the file is still open in another program (like Excel), or the folder is read-only.",
      "Close the file in the other program, then run again. If you are writing a file, choose a folder your programs may write to (your Documents folder, for example)." },
    { "RecursionError|maximum recursion depth",
      "The function keeps calling itself forever (Python)",
      "A function calls itself again and again without ever reaching its stopping case, until Python stops it to protect the program.",
      "Check the base case: there must be a condition that RETURNS without calling again, and every call must move one step closer to it." },
    { "AssertionError",
      "An assert check failed (Python)",
      "An assert line tested something and it was False, so the program stopped on purpose. This is a checking tool, not a crash: it tells you a value was not what the code expected.",
      "Print the values used in the assert just above the failing line and find why the expectation is wrong — either the value or the expectation needs fixing." },
    // ---- Last Python additions (v1.5.1): these error classes have no
    // generic entry above them, so they are safe at the end of the
    // Python block.
    { "changed size during iteration|dictionary keys changed during iteration",
      "The list/dictionary changed while looping over it (Python)",
      "You added or removed items from the very list/dictionary your for loop was walking through. Python loses its place, so it stops the program instead of guessing.",
      "Loop over a copy: for x in list(items): ... and change the original inside. Or collect the changes in a new list and apply them after the loop." },
    { "EOFError|EOF when reading a line",
      "The input ended sooner than expected (Python)",
      "input() was called but there was no more input to read — for example the program asked for more lines than the given test input has, or the input stream was closed.",
      "Check how many times the program asks for input against how many lines of input it gets. If the input may end early, catch it with try/except EOFError." },
    { "IsADirectoryError|NotADirectoryError|Is a directory",
      "A folder was treated as a file (Python)",
      "The path points to a folder (directory), but the code tried to open or read it as a file — or the other way round: a file was used where a folder was expected.",
      "Check the path: print it once and make sure it ends at the file name, not at its folder." },
    { "UnicodeDecodeError|codec can't decode|codec cannot decode",
      "The file's letters could not be read as that encoding (Python)",
      "The file was saved in one character encoding (the way letters are stored as bytes), and Python tried to read it as another. Some byte in the file is not valid in the expected encoding.",
      "Open it with the encoding it was saved in, for example open(path, encoding='utf-8'), or encoding='latin-1' for old files. Re-saving the file as UTF-8 also fixes it for good." },
    { "Expecting value|Expecting ',' delimiter|Expecting property name|JSONDecodeError",
      "The JSON text is written wrongly (Python)",
      "json.loads()/json.load() read text that is not valid JSON — often a plain word instead of JSON, an empty reply, single quotes instead of double quotes, or a missing comma between items.",
      "Print the raw text first and look at it: JSON uses double quotes for names and text, commas between items, and no trailing comma. An empty text is not JSON at all." },
    { "StopIteration",
      "next() was called but nothing is left (Python)",
      "next() asked an iterator for one more item, but it is finished — there is no next item. When this escapes outside a loop, Python shows it as an error.",
      "Give next() a safety value: next(it, None) — then check for None. Or use a normal for loop, which stops by itself." },
    { "expected ';'",
      "A semicolon (;) is missing",
      "Almost every statement in C/C++ must end with a semicolon. The compiler reached this point and did not find one.",
      "Look at the END of the line just ABOVE the reported line — that is where the ; is usually missing. Add it there." },
    { "expected '}'|expected ']'|expected '\\)'",
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
    { "WinMain",
      "main() is missing — the linker is looking for the program's start",
      "Every program needs exactly one starting point: int main(). The linker did not find it, so it complains about WinMain (the Windows GUI start point). Causes: main is missing entirely, it is misspelled (for example Main), or it sits in a file that is not part of the project.",
      "Make sure one of your files has exactly: int main() { ... return 0; } — with a small 'm'. If two files both define main, remove one of them." },
    { "vtable for|typeinfo for",
      "A class promises a function that was never written (linker error)",
      "The class declares a virtual function but its body is missing, or the .cpp file holding the class's functions is not part of the project — so the linker cannot build the class's function table (the vtable).",
      "Write the body of every virtual function you declared (even an empty one: void f() { }), or remove the declaration. If the bodies live in another .cpp file, add that file to the project." },
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
    { "fatal error: .* no such file",
      "A header file is missing",
      "An #include names a header that cannot be found — usually a typing mistake, or a library header that is not installed.",
      "Check the spelling inside the < > or quotes. Standard ones look like <iostream>, <cmath>, <string>. Your own headers must sit in the same folder (use \"name.h\")." },
    { "no such file or directory",
      "A file was not found",
      "The compiler was told to open a file that does not exist at that place — a misspelled #include, a missing project file, or a wrong folder.",
      "Check the exact spelling of the file name (also capital letters). Make sure the file really exists in the project folder." },
    { "deprecated",
      "An old, retired function is being used (warning)",
      "This function or style is from an older version of C/C++ and is kept only so old programs still compile. It still works today, but new code should use the modern replacement.",
      "You may ignore it while learning. When you have time, switch to the replacement the message suggests (for example gets -> fgets)." },
    { "ignoring return value",
      "A result was thrown away (warning)",
      "The function gives back a value that tells you something (often 'did it work?'), and the code ignores it. For scanf this usually does no harm in class programs, but for file functions it can hide a real failure.",
      "Either use the result (store it in a variable and check it), or ignore this warning while learning — the program still runs." },
    // ---- v1.5.1 early specifics: these message-shaped entries must
    // sit BEFORE the bare-word header hints right below (cin/cout,
    // printf, string...), because their messages can MENTION those
    // words — the simulation caught the hints swallowing them.
    { "implicit declaration of function",
      "A function is used before the compiler knows it (C)",
      "In C, a function must be declared (or defined) ABOVE the place where it is first called. Here the call comes first.",
      "Move the function's definition above main, or write its declaration (name, return type, parameters + ;) above the call. Also check the #include for library functions." },
    { "too few arguments for format|too many arguments for format",
      "printf has the wrong number of values (warning)",
      "The format text has a different number of % places (%d, %s, ...) than the values given after it. printf pairs them one by one in order, so a mismatch means garbage output or a crash.",
      "Count the % places in the text and give exactly that many values, in the same order. This is about printf's OWN values — a function call with the wrong count is a different error." },
    { "format not a string literal",
      "printf got its instructions from a variable (warning)",
      "printf's first argument should normally be a fixed text in quotes. Here it is a variable — if that text ever contains a %, printf will read it as an instruction and print garbage or crash.",
      "Write printf(\"%s\", text) when you want to print a variable's text as-is. Only put % codes in a fixed quoted text you control." },
    { "comparison with string literal",
      "Text compared with == instead of strcmp (warning)",
      "Two pieces of text were compared with ==. For char* text that compares the ADDRESSES (where the text lives), not the letters — so equal-looking text can still compare as different.",
      "Use strcmp(a, b) == 0 to compare C text (needs <cstring>). In C++, make them std::string and == works on the letters." },
    { "stringop-overflow|stringop-truncation|format-overflow|format-truncation|output truncated|into a region of size",
      "The text may not fit into that array (warning)",
      "A copy or printf-into-array can write more characters than the target char array can hold — including the invisible \\0 that must end every C text. Overflow writes into neighbouring memory.",
      "Make the target array bigger than the longest possible text plus 1 for the \\0, or use the n-forms (strncpy, snprintf) with the array's real size." },
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
    { "converting a string constant|string constant to",
      "Text given to a char* — that text is read-only",
      "Text written in double quotes is constant: it must not be changed through a pointer. Old C allowed a plain char* for it; C++ wants the pointer to promise not to modify the text.",
      "Write const char* p = \"hello\"; — or use std::string if the text really needs to change." },
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
    { "missing terminating ' character|missing terminating \" character|unterminated character|unterminated string",
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
    { "comparison between signed and unsigned|sign-compare|different signedness",
      "Comparing a signed and an unsigned number (warning)",
      "One side of the comparison can hold negative numbers, the other cannot. For very large or negative values the comparison can surprise you.",
      "Make both sides the same type — for loop counters compared with .size(), use std::size_t or int consistently." },
    { "expects (an )?argument of type|format '%",
      "printf format and value do not match",
      "In printf, each % code must match the type of the value after it: %d for int, %f for float/double, %c for char, %s for text.",
      "Compare each % in the format text with its value, in order, and fix the code or the value so the types match." },
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
    { "avoid ambiguous 'else'|dangling",
      "Which if does this else belong to?",
      "With nested ifs and no braces, an else belongs to the NEAREST if — which may not be the if you meant. The compiler warns that this code is easy to misread.",
      "Put { } around the if and else blocks so the pairing you mean is the pairing that happens." },
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
    // ---- More GCC / MinGW messages (v1.5.0).
    { "cannot open output file",
      "The old program is still running (the linker cannot write the .exe)",
      "The new .exe cannot be written because the old one is still open or running — Windows locks a running program file. This is the famous 'Permission denied' linker error on Windows.",
      "Close the running program's window first, then compile again. If it still fails, open Task Manager and end the leftover program process." },
    { "expected constructor, destructor, or type conversion",
      "Probably a missing ; after a class or struct",
      "The compiler met something at file scope it cannot read as a declaration. The classic cause: the class or struct just above ends with } but the ; after it is missing — class definitions must end with };",
      "Check the end of the class/struct above and make it }; — if there is no class above, look for a stray line or a forgotten ; just above the reported line." },
    { "cannot call member function",
      "A member function was called without an object",
      "Functions inside a class belong to an object. Calling ClassName::function() only works for functions marked static; normal member functions need an object in front of them.",
      "Create an object and call through it: MyClass obj; obj.function(); — or mark the function 'static' if it truly needs no object." },
    { "array subscript is not an integer",
      "The array index is not a whole number",
      "Inside array[ ] the position must be a whole number. Here it is a decimal number, a character or something else.",
      "Use an int for the index. If the index is a calculation, store it in an int variable first and check its value." },
    { "is dangerous",
      "gets() is dangerous — do not use it",
      "gets() reads text without any size limit, so long input can overflow the array and crash the program (or worse). It was removed from modern C for this reason; old books still show it.",
      "Use fgets instead: fgets(name, sizeof(name), stdin); — it can never write past the end of your array." },
    { "\\bmemset\\b",
      "memset needs <cstring>",
      "memset is declared in the <cstring> header (C: <string.h>).",
      "Add #include <cstring> at the top of the file." },
    { "set but not used",
      "A variable is given values but never read (warning)",
      "The code stores into this variable, but nothing ever reads it afterwards. Often a sign that a step is missing — for example a total that is calculated but never printed.",
      "Read the variable where you meant to use it (print it, return it, compare it), or remove it if it is truly not needed." },
    { "defined but not used",
      "Written but never used (warning)",
      "This function or variable is fully written, but the program never calls or uses it anywhere.",
      "Call it where you meant to, or delete it. Safe to ignore while learning." },
    { "without a previous",
      "An else with no if above it",
      "Every else must belong to an if that comes right before it. Here the compiler cannot find that if — usually a } above closed the if-block earlier than you think, or a ; right after if (...) cut the if off.",
      "Check the lines above: make sure the if and its { } block really end right before this else, and remove any ; sitting directly after if (...)." },
    { "too few arguments|too many arguments",
      "Wrong number of arguments in the call",
      "The function was called with more or fewer values than its declaration promises. In C the compiler is strict about this once the declaration is known.",
      "Compare the call with the function's declaration and add or remove values so both lists match, in the same order and with matching types." },
    { "conflicting types",
      "Two declarations of the function disagree",
      "The function is declared in two places with different types — often a call before any declaration made the compiler guess one type, and the real definition says another. A missing #include for a library function does this too.",
      "Write ONE declaration of the function above main (matching the definition exactly), or add the missing #include. Compare return type and parameter types character by character." },
    { "makes pointer from integer|makes integer from pointer",
      "A memory address and a number were mixed up",
      "One side of this line is a pointer (a memory address, like an array name or a variable written with *), and the other side is a plain number — the compiler converted between them, which is nearly always a bug.",
      "Decide what you mean: to work with the VALUE, write *pointer or array[i]; to store an address, the receiving variable must be a pointer type (with *). Also check function arguments — passing x where &x was needed is the classic case." },
    { "incompatible implicit declaration",
      "A library function is used without its #include",
      "The compiler has not seen this function's real declaration, so it guessed one — and the guess does not match the real function. The wrong guess can give wrong results, not just a warning.",
      "Add the function's #include at the top (for example <cstring> for strlen, <cmath> for sqrt, <cstdio> for printf)." },
    { "is not a member of",
      "That name does not exist inside this class/namespace",
      "You wrote something like std::name, but there is no member with that exact name there — a typo, the wrong header, or the name lives somewhere else (for example std::sort needs <algorithm>).",
      "Check the spelling first. Then check that the header declaring it is #included. If it is your own class, open its definition and compare the member names." },
    { "unused parameter",
      "A function parameter is never used (just a warning)",
      "The function receives this value but its body never uses it — common with argc/argv in main, or a parameter you planned to use and forgot.",
      "Use the parameter, or ignore the warning — the program works. (Removing the name but keeping the type, like 'int main(int, char**)', also silences it.)" },
    { "skipping incompatible",
      "That library file does not fit this compiler",
      "The linker found the library but skipped it: it was built for a different kind of system (for example 32-bit instead of 64-bit, or a different compiler). Right after this it usually also says 'cannot find -l...'.",
      "Use the library build that matches your compiler (same 32/64-bit). For normal class programs, remove -l entries you do not really need from the linker settings." },
    { "extra ';'",
      "An extra semicolon (harmless warning)",
      "There is a ; that is not needed — often left behind after the } of a function or class.",
      "Delete the extra ; — or ignore the warning, it changes nothing." },
    { "may be undefined",
      "This line changes and reads the same variable at once (warning)",
      "Lines like i = i++ or f(i, i++) change a variable and use its value in the same breath; the order is not promised, so different compilers can give different answers.",
      "Split it into two lines: first change the variable, then use it. 'i++; x = i;' is always clear." },
    { "comparison is always",
      "This comparison can only ever go one way (warning)",
      "The values that can reach this comparison make the answer always true or always false — for example comparing an unsigned value with < 0, or a char with a number far outside its range. The code probably does not do what you planned.",
      "Check the types and the possible ranges of both sides; the condition or one of the values is not what you think it is." },
    // ---- More GCC/MinGW C and C++ (v1.5.1) — real warning/error
    // texts the first 107 did not cover. Placed late on purpose: every
    // pattern here was checked not to be swallowed by an earlier entry.
    { "spurious trailing",
      "A % sign is left over in the format text (warning)",
      "The format text ends with a % that has no letter after it, so it is not a real format code. printf does not know what to print for it.",
      "Remove the lonely %, or complete it: %d for a number, %s for text, %% for a real percent sign." },
    { "return type defaults to",
      "The return type was not written (C)",
      "A function was declared without saying what it returns, so old C silently assumed int. Modern compilers warn or refuse, because the assumption hides mistakes.",
      "Write the return type in front of the function name: int add(...) — or void if it returns nothing." },
    { "may fall through",
      "A switch case falls into the next one (warning)",
      "This case in a switch has no break at its end, so when it finishes, the program continues straight into the NEXT case. Sometimes that is wanted — but very often it is a forgotten break.",
      "Add break; at the end of the case if the next case should not also run. If falling through is truly what you want, the warning can be ignored here." },
    { "subscript has type",
      "The array index is a char (warning)",
      "The value inside [ ] is a char. That works (a char is a small number), but it is a classic trap: '2' the character is not the number 2, and negative chars can index outside the array.",
      "Use a normal int as the index. If the char came from input, convert it first: index = ch - '0' for a digit character." },
    { "will always evaluate",
      "That address check is always true (warning)",
      "The code tests the address of something (like &x or an array name) as if it could be false/empty. An address of a real variable is never zero, so the test always passes and hides the check you meant to write.",
      "Test the VALUE instead of the address: if (x != 0) — or, for a pointer you received, test the pointer itself: if (p != NULL)." },
    { "will return size of",
      "sizeof of a pointer, not of the array (warning)",
      "sizeof was used on an array that arrived as a function parameter. A parameter is really just a pointer, so sizeof gives the pointer's size (8 or 4), not the array's real length.",
      "Pass the length as an extra parameter, or use the array in the same function where it was created — sizeof only knows the true size there." },
    { "may change value|may alter its value",
      "The value may change when its type changes (warning)",
      "A value is being squeezed into another type (for example int into char, or double into int) and it may not survive unchanged: big numbers can wrap around or lose their fraction.",
      "If the change is intended, say so clearly with a cast: (char)value. Otherwise give the variable the bigger/safer type so nothing is lost." },
    { "overflow in conversion",
      "The number is too big for the new type (warning)",
      "A fixed number was converted into a type that cannot hold it — for example 300 into a char (which holds at most 255). The stored value wraps around and becomes something else entirely.",
      "Use a type that fits the value (int instead of char), or choose a value inside the target type's range." },
    { "implicitly truncated",
      "A big number was cut down to fit (warning)",
      "A large constant was stored into a smaller unsigned type, so its top part was silently cut off. The variable does not hold the number you wrote.",
      "Use a bigger type for the variable, or a smaller value that really fits." },
    { "narrowing conversion",
      "A value is squeezed into a smaller type",
      "Inside { } starting values, C++ protects you: a value that might not fit into the smaller type is an error, not a silent cut. For example char c = {300}; can never hold 300.",
      "Give the variable a type that fits (int), or put a value inside the smaller type's range." },
    { "'return' with a value",
      "A void function tries to return a value",
      "The function is declared void (returns nothing), but a return line hands back a value. The caller could never receive it, so this is an error.",
      "Either change the function's return type from void to the type you return, or write plain return; with no value." },
    { "mixed declarations",
      "Declarations standing after code (old C style, warning)",
      "In old C, all variables of a block had to be declared at its top. Here a declaration stands after normal statements. C++ and newer C allow it; strict old-C mode only warns.",
      "Nothing is broken in C++. In strict C, move the declaration up to the top of the block, before the first statement." },
    { "no type or storage class",
      "A line outside any function looks like a declaration but has no type (C)",
      "At the top level of a C file, every line must be a declaration with a type, a function, or a # line. This line has none — very often a stray statement, a missing int, or code accidentally written outside a function.",
      "Move statements inside a function, and give declarations their type: int x; — also check the line above for a missing } that ended the function early." },
    { "extra qualification",
      "The class name is written where it does not belong",
      "Inside the class itself, a member was written with the class name in front, like Point::Point::x. The extra ClassName:: is only used for definitions OUTSIDE the class body.",
      "Remove the extra ClassName:: — inside the class just write the member's own name." },
    { "missing braces around initializer",
      "An inner { } is missing in the starting values (warning)",
      "A 2D array or a struct inside an array got its starting values in one flat list, without the inner { } groups. The compiler guesses the grouping — and may guess differently from what you meant.",
      "Write the inner groups: int a[2][3] = { {1,2,3}, {4,5,6} }; so every row/group has its own braces." },
    { "missing initializer for member|missing initializer for field",
      "A member was left without a starting value (warning)",
      "A struct/class object was created with { } values, but one member got none. That member holds whatever garbage was in memory (for plain types).",
      "Give every member a value in the { } list, or declare a default in the class: int x = 0;" },
    { "will be initialized after",
      "Members actually start in declaration order (warning)",
      "The constructor's : init list names members in a different order than the class declares them. C++ always initializes in DECLARATION order, not list order — so a member can be built from another one that is not ready yet.",
      "Reorder the init list to match the order the members are declared in the class." },
    { "used uninitialized",
      "A variable is used before it ever got a value (warning)",
      "The compiler can see a path where this variable is read before anything was stored in it. Its value is whatever garbage happened to be in that memory — different every run.",
      "Give the variable a starting value where you declare it: int x = 0; — then the warning and the randomness are gone." },
    { "cast to pointer from integer|cast from pointer to integer|loses precision",
      "A number and a memory address were cast into each other (warning)",
      "An int was forced into a pointer, or a pointer into a smaller int. On 64-bit systems an address needs 64 bits; squeezing it into a 32-bit int cuts it and the pointer becomes invalid.",
      "Keep addresses in pointer variables (or long long if you truly must store one). Normal beginner code never needs this cast — check if a * or & was forgotten instead." },
    { "does not name a type",
      "That name is not a known type",
      "The compiler met a name where a type should stand (like vector<int> v;) and it does not know that type. Most often its #include is missing — vector needs <vector>, string needs <string> — or the std:: in front is missing.",
      "Add the type's #include at the top and write std:: before standard types (std::vector), or keep 'using namespace std;' after your includes." },
    { "base operand of",
      "-> used on something that is not a pointer",
      "The -> arrow works only on a POINTER to a struct/class. Here the thing before -> is a plain object or value, not a pointer.",
      "Use the dot for a plain object: obj.member — and -> only for pointers: ptr->member. Or take the address first if a pointer was really meant." },
    { "taking address of|address of rvalue",
      "The address of a temporary value was taken",
      "You asked for the address (&) of a value that exists only for a moment — like a calculation result or a temporary object. It is destroyed immediately, so its address would point at nothing.",
      "Store the value in a named variable first, then take its address: int t = a + b; int* p = &t;" },
    { "from NULL",
      "NULL was passed where a normal value was needed",
      "NULL (nothing) was given to a place that needs a real value, like an int parameter. NULL is really just 0 wearing a pointer costume — the function will treat it as the value 0, which is almost never intended.",
      "Pass a real value instead. If a pointer was meant, the parameter's type is probably wrong — check the function's declaration." },
    { "NULL used in arithmetic",
      "NULL was used in a calculation (warning)",
      "NULL appeared in arithmetic, like p = NULL + 1. NULL means 'no address'; doing maths with it produces a meaningless address.",
      "Check the line: you probably meant to do the maths on a real pointer or number, not on NULL. Never calculate with NULL itself." },
    { "shadows",
      "A name hides another name (warning)",
      "A variable inside a block has the same name as one outside it (or as a parameter). Inside the block, ONLY the inner one is visible — the outer one is hidden, which easily confuses both you and readers about which value a line changes.",
      "Rename the inner variable. Two different things should have two different names." },
    { "unused label",
      "A label is written but never jumped to (warning)",
      "A goto label exists, but no goto anywhere uses it. Harmless — leftover from an edit, most likely.",
      "Remove the label, or add the goto that was meant to use it." },
    { "empty body",
      "An if/for has an empty body — often a stray ; (warning)",
      "The statement after if/for/while is empty. The classic cause is a ; right after the condition: if (x > 0); — that semicolon IS the whole body, and the { } block after it runs always.",
      "Remove the ; after the condition if the { } block below was meant to be the body. A truly empty body is almost never what you want." },
    { "does not guard|misleadingly indented",
      "The spaces make the code LOOK different from what it does (warning)",
      "A line is indented as if it belonged to the if/for above it, but it is not inside it — it runs always. The indentation lies about the logic.",
      "Put { } around the lines that belong to the if/for, and indent exactly the lines inside the braces." },
    { "will never be executed",
      "This code can never run (warning)",
      "The compiler proved this statement can never be reached — for example a case in a switch whose value another case already handles, or code after an endless loop.",
      "Check the logic above it. If the code was meant to run, the condition or case values above are probably wrong." },
    { "unterminated comment",
      "A /* comment was never closed",
      "A /* comment starts but its closing */ never comes, so everything after it — real code included — is treated as comment and quietly disappears from the program.",
      "Find the /* and add the closing */ where the comment was meant to end. If code below seems ignored by the compiler, this is the first place to look." },
    { "multi-line comment",
      "A comment runs into the next line (warning)",
      "A // comment ends with a backslash \\, which secretly continues the comment onto the next line — so a real line of code may be commented out without looking like it.",
      "Remove the backslash at the end of the // line, or turn the text into a /* ... */ block comment if several lines were meant." },
    { "address of local|address of stack memory",
      "The address of a local variable is sent out (warning)",
      "A function returns (or stores for later) the address of its OWN local variable. When the function ends, that variable is destroyed — the address points at dead memory and using it is undefined behaviour.",
      "Return the value itself, or store the data somewhere that outlives the function (a caller's variable via pointer parameter, or static/global storage)." },
    { "above array bounds|below array bounds|partly outside array bounds",
      "The index is outside the array — the compiler can see it (warning)",
      "The code indexes the array with a position that is definitely outside it: an array of 5 has positions 0 to 4 only. Reading/writing outside reads garbage or corrupts other variables.",
      "Keep indexes inside 0 .. size-1. In loops, the condition should be i < size, never i <= size." },
    { "uninitialized const",
      "A const was created without a value",
      "A const variable MUST get its value at birth — it can never be assigned later, so creating it empty is an error.",
      "Give the value in the same line: const int MAX = 100;" },
    { "variable length array|variably modified",
      "The array size must be a fixed number here",
      "The array's size is a variable that only exists at run time (a VLA). Standard C++ forbids it, and at file scope no language allows it. GCC accepts it as an extension in functions, but portable code cannot.",
      "Use a fixed size (const int N = 100;), or std::vector, which is made for run-time sizes." },
    { "missing binary operator before token",
      "Something is missing inside an #if condition",
      "The #if line's condition is incomplete — an operator or a value is missing, like '#if X ==' with nothing after it, or a name that was never #defined being used in a way the preprocessor cannot evaluate.",
      "Read the #if condition as a normal expression and complete it. Remember: undefined names in #if count as 0, and defined(X) tests if X exists." },
    { "#error",
      "Your own #error line stopped the build",
      "This is not a compiler complaint — it is YOUR code (or a header's code) deliberately stopping the build with #error and showing this message. It usually guards a condition like a missing #define or wrong setting.",
      "Read the message after #error: it tells you what to set or fix. The #error line itself is doing its job — fix the condition, not the messenger." },
    { "redefined",
      "A #define name is defined twice (warning)",
      "The same macro name was #defined again with a different value. The new value wins from here on, which may not be what the first definition intended.",
      "Keep one definition. If a header and your file both define it, #undef it first or remove yours." },
    { "unterminated #if|without #if",
      "The #if family lines do not match up",
      "#if / #ifdef / #ifndef blocks must close with #endif, and #else / #endif need an #if above them. Here the pairing is broken — an #endif is missing, or one stands alone.",
      "Count the pairs in the file: every #if needs exactly one #endif, with #else in between at most once. Add or remove the line the message points near." },
    { "no newline at end of file",
      "The file does not end with an empty line (warning)",
      "The file's very last line has no Enter after it. Old rules require text files to end with a newline; some tools misread the last line without it.",
      "Put the cursor at the very end of the file and press Enter once. Harmless, but now it is clean." },
    { "only allowed in C99|only allowed in C11",
      "Declaring the loop variable inside for() needs a newer C standard",
      "Writing for (int i = 0; ...) with the declaration INSIDE the for is a C99-and-later feature. This file is being compiled as old C90, where the variable must be declared before the loop.",
      "Declare the variable above the loop: int i; for (i = 0; ...) — or compile as C99/C11 (the Dev by Sufyan default for .c files already allows it)." },
    { "parameter name omitted",
      "A parameter in the declaration has no name (C)",
      "In C, a function DEFINITION must name every parameter — a bare type like (int, int) is only allowed in a declaration. (C++ allows unnamed parameters; C does not.)",
      "Give the parameter a name in the definition: int add(int a, int b) { ... }" },
    { "follows non-static declaration|follows static declaration",
      "The same function is declared two different ways",
      "One place says the function is static (private to this file) and another place says it is not. Both declarations must agree.",
      "Make the declarations match: either write static in both places, or in neither." },
    { "incomplete element type",
      "An array of an unfinished type",
      "An array was declared whose member type is only promised, not yet defined — like struct Node arr[10]; before struct Node itself is written. The compiler cannot know each member's size, so it cannot build the array.",
      "Write the full struct/class definition ABOVE the array. A pointer to an unfinished type is fine; an array of it is not." },
    { "dereferencing 'void",
      "A void* must be given a real type before use (C)",
      "A void* points at 'something' without saying what. Before you can use *p or p[i], the compiler must know the real type behind it.",
      "Cast it to the real type first: int* q = (int*)p; then use *q. Or declare the pointer with its real type from the start." },
    { "discards .* qualifier|discards qualifiers",
      "A const promise is being broken (warning)",
      "A const value (which promises 'I will not change') was handed to a place that may change it — a function taking char* getting a const char*, or a const variable assigned into a plain one. The promise would be broken silently.",
      "Make the receiving side const as well (const char* parameter), or — only if you are SURE it is safe — cast away the const deliberately. Never break the promise by accident." },
    { "shift count|shift exponent",
      "Shifting by too many bits (warning)",
      "A << or >> shift asks to move bits by more places than the type has bits (or by a negative amount). The result of such a shift is undefined — different CPUs answer differently.",
      "Keep the shift count between 0 and (bits - 1): for an int, 0 to 31. Check the variable that holds the count." },
    { "too many decimal points|exponent has no digits",
      "The number is written wrongly",
      "A number contains two decimal points (like 3.14.15) or an unfinished exponent (like 1e). The compiler cannot read it as one number.",
      "Write the number in one piece: one decimal point, and digits after e if you use it: 3.1415, 1.5e3." },
    { "invalid suffix",
      "Extra letters stuck to a number",
      "Letters were typed directly after a number where only known endings fit (like 10f in C, or 123abc). The compiler does not know the ending you used.",
      "Remove the extra letters, or use a real ending: 10 (plain), 10L (long), 10.0f (float — the dot matters), 10u (unsigned)." },
    { "unknown escape sequence",
      "An unknown backslash letter inside the text (warning)",
      "Inside \"...\" text, a backslash starts a special code: \\n newline, \\t tab, \\\\ a real backslash. Here an unknown code like \\d was used, so it prints as just the letter — probably not what you wanted.",
      "Use a known code, or write \\\\ when you really want a backslash in the output, like in a file path: \"C:\\\\folder\"." },
    { "character constant too long",
      "Too many characters between single quotes (C form)",
      "Single quotes hold exactly ONE character in C: 'a'. What you wrote has more — the compiler squeezes it into one number in an implementation-defined way.",
      "For one character use 'a'; for text use double quotes: \"abc\"." },
    { "empty character constant",
      "Nothing between the single quotes",
      "'' with nothing inside is not a character at all. The compiler has nothing to store.",
      "Put the one character you meant: ' ' (a space is a character too) or '\\0' for the zero/end character." },
    { "stray '",
      "A stray character that does not belong in code",
      "A character appeared in the program that is not part of C/C++ at all — shown as its number code like \\302. These usually sneak in when code is copied from a web page or document (invisible characters, smart quotes), or from a stray keypress.",
      "Delete the character at the spot the message names and retype that line cleanly with the keyboard." },
    { "not a structure or union",
      "Dot (.) used on something that is not a struct (C)",
      "member access with . was used on a value that is not a struct/union — like a plain int or a pointer. Only structs have members to reach with a dot.",
      "Check the variable's type. For a struct VALUE use obj.member; for a POINTER to struct use ptr->member; plain numbers have no members at all." },
    { "called object",
      "That name is not a function (C)",
      "Something was called with ( ) as if it were a function, but it is a variable or another kind of thing. Only functions and function pointers can be called.",
      "Check the name: is the function spelled right, and is this variable accidentally hiding the function's name? Rename one of them if they clash." },
    { "declared void",
      "A variable was declared as void",
      "void means 'nothing' — it can be a function's return type, but a variable cannot BE nothing, so declaring one is an error.",
      "Give the variable a real type (int, char, ...) — or, if you meant an anything-pointer, write void* (with the star)." },
    { "storage size of",
      "The size of this type is unknown here",
      "A variable of a type was created whose full definition the compiler has not seen — only its name was promised earlier. Without the definition it does not know how much memory the variable needs.",
      "Make sure the struct/class is fully defined (usually via its #include) ABOVE the place where the variable is created." },
    { "duplicate .* declaration specifier|duplicate 'const'|duplicate 'static'",
      "The same word is written twice in a declaration",
      "A declaration repeats a word like const const or static static. It changes nothing, but it is not allowed.",
      "Remove the repeated word so each specifier stands once." },
    { "no declaration matches",
      "The function definition matches no earlier declaration",
      "A member function was defined outside its class (Class::name), but the class has no declaration that matches it exactly — the name, parameters, or const-ness differ somewhere.",
      "Compare the definition with the declaration inside the class letter by letter: same name, same parameter types, same const at the end." },
    { "invalid use of 'this'|use of 'this' in non-member",
      "'this' used outside a member function",
      "'this' (the current object) exists only inside a class's member functions. In a plain function or in main there is no 'this' object.",
      "Pass the object as a parameter instead, or move the code into a member function of the class." },
    { "ISO C\\+\\+ forbids",
      "Old-style code the compiler is strict about",
      "This way of writing was allowed in old C/C++, and the modern standard forbids it. The message itself names the exact thing (converting text to char*, a variable-size array, an old-style declaration...). GCC often still compiles it with just this warning, but stricter compilers may refuse.",
      "Follow what the message names: for text given to char*, declare it const char*; for variable-size arrays, fix the size as a const or use std::vector; in general, write the modern form and the warning disappears." },
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

// ---------------------------------------------------------------------------
// v1.5.1 — "Fix Lagao": deterministic one-click fixes.
//
// The fix rules are DATA: each rule names a dictionary entry (by its
// title) and a fix KIND with an optional parameter. dbsComputeIssueFix()
// turns (issue, entry, current file text) into a DbsFixAction only when
// the fix is unambiguous for that exact message + line; otherwise no
// fix is offered and the explanation stands alone. Applying goes
// through the editor (replaceLine / setSelText), so Ctrl+Z undoes a
// fix like any other edit.
// ---------------------------------------------------------------------------

struct ErrorFixRule {
    const char *title; // dictionary entry title this rule belongs to
    const char *kind;  // semicolon | closeBracket | colon | didYouMean |
                       // undeclared | include | printParens
    const char *param; // "include" only: the header, e.g. "<iostream>"
};

const ErrorFixRule ERROR_FIX_RULES[] = {
    { "A name the compiler does not know", "undeclared", nullptr },
    { "A function is used before the compiler knows it (C)", "undeclared", nullptr },
    { "A library function is used without its #include", "undeclared", nullptr },
    { "That name is not a known type", "undeclared", nullptr },
    { "Python does not know this name (NameError)", "didYouMean", nullptr },
    { "A semicolon (;) is missing", "semicolon", nullptr },
    { "A closing bracket is missing", "closeBracket", nullptr },
    { "A colon (:) is missing at the end of the line (Python)", "colon", nullptr },
    { "print needs round brackets (Python)", "printParens", nullptr },
    { "cout / cin needs <iostream>", "include", "<iostream>" },
    { "printf / scanf needs <cstdio>", "include", "<cstdio>" },
    { "String functions need <cstring>", "include", "<cstring>" },
    { "These functions need <cstdlib>", "include", "<cstdlib>" },
    { "Math functions need <cmath>", "include", "<cmath>" },
    { "vector needs <vector>", "include", "<vector>" },
    { "string needs <string>", "include", "<string>" },
    { "memset needs <cstring>", "include", "<cstring>" },
};

const ErrorFixRule *fixRuleForEntry(const ErrorExplanation *entry)
{
    if (!entry)
        return nullptr;
    for (const ErrorFixRule &rule : ERROR_FIX_RULES)
        if (qstrcmp(entry->title, rule.title) == 0)
            return &rule;
    return nullptr;
}

// A one-line description of what a fix kind does (dictionary browser).
QString fixKindSummary(const ErrorFixRule *rule)
{
    if (!rule)
        return QString();
    const QString kind = QString::fromLatin1(rule->kind);
    if (kind == "semicolon")
        return QObject::tr("adds the missing ;");
    if (kind == "closeBracket")
        return QObject::tr("adds the missing closing bracket");
    if (kind == "colon")
        return QObject::tr("adds the missing :");
    if (kind == "didYouMean")
        return QObject::tr("uses the suggested name instead");
    if (kind == "undeclared")
        return QObject::tr("adds the missing #include, or uses the suggested name");
    if (kind == "include")
        return QObject::tr("adds %1 at the top").arg(
                    QString::fromLatin1(rule->param));
    if (kind == "printParens")
        return QObject::tr("rewrites print as print(...)");
    return QString();
}

// Library name -> the header that declares it. Used by the
// "undeclared" fix kind.
const QPair<const char *, const char *> DBS_INCLUDE_FOR_NAME[] = {
    {"cout", "<iostream>"}, {"cin", "<iostream>"}, {"endl", "<iostream>"},
    {"printf", "<cstdio>"}, {"scanf", "<cstdio>"}, {"sprintf", "<cstdio>"},
    {"snprintf", "<cstdio>"}, {"fprintf", "<cstdio>"}, {"fopen", "<cstdio>"},
    {"fclose", "<cstdio>"}, {"puts", "<cstdio>"}, {"putchar", "<cstdio>"},
    {"getchar", "<cstdio>"}, {"gets", "<cstdio>"},
    {"strlen", "<cstring>"}, {"strcpy", "<cstring>"}, {"strncpy", "<cstring>"},
    {"strcmp", "<cstring>"}, {"strcat", "<cstring>"}, {"strstr", "<cstring>"},
    {"strtok", "<cstring>"}, {"memcpy", "<cstring>"}, {"memset", "<cstring>"},
    {"memmove", "<cstring>"},
    {"malloc", "<cstdlib>"}, {"calloc", "<cstdlib>"}, {"realloc", "<cstdlib>"},
    {"free", "<cstdlib>"}, {"rand", "<cstdlib>"}, {"srand", "<cstdlib>"},
    {"exit", "<cstdlib>"}, {"atoi", "<cstdlib>"}, {"atof", "<cstdlib>"},
    {"abs", "<cstdlib>"}, {"system", "<cstdlib>"},
    {"sqrt", "<cmath>"}, {"pow", "<cmath>"}, {"sin", "<cmath>"},
    {"cos", "<cmath>"}, {"tan", "<cmath>"}, {"fabs", "<cmath>"},
    {"floor", "<cmath>"}, {"ceil", "<cmath>"}, {"log", "<cmath>"},
    {"log10", "<cmath>"}, {"exp", "<cmath>"},
    {"vector", "<vector>"}, {"string", "<string>"},
    {"sort", "<algorithm>"}, {"max", "<algorithm>"}, {"min", "<algorithm>"},
    {"swap", "<utility>"},
};

QString dbsHeaderForName(const QString &name)
{
    for (const auto &pair : DBS_INCLUDE_FOR_NAME)
        if (name == QLatin1String(pair.first))
            return QString::fromLatin1(pair.second);
    return QString();
}

// The quote characters compilers use around names: ' and ` and the
// Unicode pair (GCC's default). Built from code points.
QString dbsQuoteClass()
{
    return QStringLiteral("['") + QChar(0x2018) + QChar(0x2019)
            + QStringLiteral("`]");
}

bool dbsFileHasInclude(const QStringList &lines, const QString &header)
{
    const QString name = header.mid(1, header.size() - 2); // strip < >
    static const QRegularExpression re(
                QStringLiteral("#\\s*include\\s*[<\"]([^\">]+)[\">]"));
    for (const QString &line : lines) {
        QRegularExpressionMatch m = re.match(line);
        if (m.hasMatch()) {
            const QString found = m.captured(1);
            if (found == name || found == "bits/stdc++.h")
                return true;
        }
    }
    return false;
}

int dbsIncludeInsertIndex(const QStringList &lines)
{
    int idx = 0;
    const int limit = qMin(lines.size(), 80);
    for (int i = 0; i < limit; ++i)
        if (lines[i].trimmed().startsWith("#include"))
            idx = i + 1;
    return idx;
}

bool dbsMakeIncludeFix(const QStringList &lines, const QString &header,
                       const QString &becauseName, DbsFixAction &out)
{
    if (header.isEmpty() || dbsFileHasInclude(lines, header))
        return false;
    out.valid = true;
    out.insertLine = true;
    out.line0 = dbsIncludeInsertIndex(lines);
    out.newLineText = "#include " + header;
    out.summary = becauseName.isEmpty()
            ? QObject::tr("Add %1 near the top of the file — the name used "
                          "here lives in that header.").arg(header)
            : QObject::tr("Add %1 near the top of the file — '%2' lives in "
                          "that header.").arg(header, becauseName);
    return true;
}

QString dbsParseDidYouMean(const QString &description)
{
    QRegularExpression re("did you mean:?\\s*" + dbsQuoteClass()
                          + "([\\w:]+)" + dbsQuoteClass(),
                          QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch m = re.match(description);
    return m.hasMatch() ? m.captured(1) : QString();
}

QString dbsParseUndeclaredName(const QString &description)
{
    const QString qc = dbsQuoteClass();
    const QString name = "([A-Za-z_]\\w*)";
    const QStringList patterns = {
        qc + name + qc + "\\s+was not declared",
        qc + name + qc + "\\s+undeclared",
        "undeclared identifier\\s*" + qc + name + qc,
        "implicit declaration of function\\s*" + qc + name + qc,
        "of built-in function\\s*" + qc + name + qc,
        qc + name + qc + "\\s+in namespace\\s*" + qc + "\\w+" + qc
                + "\\s+does not name a type",
        qc + name + qc + "\\s+does not name a type",
    };
    for (const QString &pattern : patterns) {
        QRegularExpressionMatch m = QRegularExpression(pattern).match(description);
        if (m.hasMatch())
            return m.captured(1);
    }
    return QString();
}

// Replace the occurrence of `wrong` on the issue's line that sits
// closest to the reported column.
bool dbsReplaceNameOnLine(const PCompileIssue &issue, const QStringList &lines,
                          const QString &wrong, const QString &replacement,
                          const QString &summary, DbsFixAction &out)
{
    if (wrong.isEmpty() || replacement.isEmpty() || wrong == replacement)
        return false;
    const int line0 = issue->line;
    if (line0 < 0 || line0 >= lines.size())
        return false;
    const QString line = lines[line0];
    QRegularExpression re("\\b" + QRegularExpression::escape(wrong) + "\\b");
    QVector<int> starts;
    QRegularExpressionMatchIterator it = re.globalMatch(line);
    while (it.hasNext())
        starts.append(it.next().capturedStart());
    if (starts.isEmpty())
        return false;
    const int near = qMax(0, issue->column);
    int best = starts.first();
    for (int s : starts)
        if (qAbs(s - near) < qAbs(best - near))
            best = s;
    out.valid = true;
    out.insertLine = false;
    out.line0 = line0;
    out.expectedOldLine = line;
    out.newLineText = line.left(best) + replacement
            + line.mid(best + wrong.size());
    out.summary = summary;
    return true;
}

bool dbsComputeIssueFix(const PCompileIssue &issue,
                        const ErrorExplanation *entry,
                        const QStringList &lines, DbsFixAction &out)
{
    out = DbsFixAction();
    const ErrorFixRule *rule = fixRuleForEntry(entry);
    if (!rule || !issue)
        return false;
    const QString kind = QString::fromLatin1(rule->kind);
    const QString desc = issue->description;
    const int line0 = issue->line;
    auto rtrim = [](const QString &s) {
        int n = s.size();
        while (n > 0 && s[n - 1].isSpace())
            --n;
        return s.left(n);
    };

    if (kind == "include") {
        // The bare-name hints also fire on mere warnings about other
        // things; only a real error earns the one-click include.
        if (issue->type == CompileIssueType::Warning)
            return false;
        return dbsMakeIncludeFix(lines, QString::fromLatin1(rule->param),
                                 QString(), out);
    }

    if (kind == "undeclared") {
        const QString name = dbsParseUndeclaredName(desc);
        const QString header = name.isEmpty() ? QString()
                                              : dbsHeaderForName(name);
        if (!header.isEmpty())
            return dbsMakeIncludeFix(lines, header, name, out);
        const QString suggestion = dbsParseDidYouMean(desc);
        if (!suggestion.isEmpty() && !name.isEmpty())
            return dbsReplaceNameOnLine(
                        issue, lines, name, suggestion,
                        QObject::tr("Replace '%1' with '%2' on line %3 — "
                                    "that is the compiler's own suggestion.")
                            .arg(name, suggestion).arg(line0 + 1),
                        out);
        return false;
    }

    if (kind == "didYouMean") {
        const QString qc = dbsQuoteClass();
        QRegularExpression nameRe("name\\s*" + qc + "([A-Za-z_]\\w*)" + qc
                                  + "\\s+is not defined");
        QRegularExpressionMatch m = nameRe.match(desc);
        const QString wrong = m.hasMatch() ? m.captured(1) : QString();
        const QString suggestion = dbsParseDidYouMean(desc);
        if (wrong.isEmpty() || suggestion.isEmpty())
            return false;
        return dbsReplaceNameOnLine(
                    issue, lines, wrong, suggestion,
                    QObject::tr("Replace '%1' with '%2' on line %3 — that "
                                "is Python's own suggestion.")
                        .arg(wrong, suggestion).arg(line0 + 1),
                    out);
    }

    if (kind == "colon") {
        if (line0 < 0 || line0 >= lines.size())
            return false;
        const QString line = lines[line0];
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.endsWith(':'))
            return false;
        static const QStringList starters = {
            "if", "elif", "else", "for", "while", "def", "class", "try",
            "except", "finally", "with"};
        QString firstWord;
        for (QChar c : trimmed) {
            if (c.isLetter() || c == '_')
                firstWord.append(c);
            else
                break;
        }
        if (!starters.contains(firstWord))
            return false;
        out.valid = true;
        out.line0 = line0;
        out.expectedOldLine = line;
        out.newLineText = rtrim(line) + ":";
        out.summary = QObject::tr("Add the missing : at the end of "
                                  "line %1.").arg(line0 + 1);
        return true;
    }

    if (kind == "printParens") {
        if (line0 < 0 || line0 >= lines.size())
            return false;
        const QString line = lines[line0];
        QRegularExpressionMatch m =
                QRegularExpression("^([ \\t]*)print[ \\t]+(\\S.*)$").match(line);
        if (!m.hasMatch())
            return false;
        const QString rest = rtrim(m.captured(2));
        if (rest.startsWith('(') || rest.startsWith(">>"))
            return false;
        out.valid = true;
        out.line0 = line0;
        out.expectedOldLine = line;
        out.newLineText = m.captured(1) + "print(" + rest + ")";
        out.summary = QObject::tr("Put the printed values inside "
                                  "print(...) on line %1.").arg(line0 + 1);
        return true;
    }

    if (kind == "semicolon") {
        if (desc.contains("after class definition")
                || desc.contains("after struct definition")
                || desc.contains("after enum definition")) {
            // The } that closes the class/struct stands on the reported
            // line or a few lines above it.
            const int hi = qMin(line0, lines.size() - 1);
            for (int t = hi; t >= qMax(0, line0 - 4); --t) {
                const QString trimmed = lines[t].trimmed();
                if (trimmed.endsWith('}') && !trimmed.endsWith("};")) {
                    out.valid = true;
                    out.line0 = t;
                    out.expectedOldLine = lines[t];
                    out.newLineText = rtrim(lines[t]) + ";";
                    out.summary = QObject::tr(
                                "Add the missing ; right after the } on "
                                "line %1 — a class/struct definition must "
                                "end with ;").arg(t + 1);
                    return true;
                }
            }
            return false;
        }
        if (desc.contains("at end of input")) {
            int t = lines.size() - 1;
            while (t >= 0 && lines[t].trimmed().isEmpty())
                --t;
            if (t < 0)
                return false;
            const QString trimmed = lines[t].trimmed();
            if (trimmed.endsWith(';') || trimmed.endsWith('{')
                    || trimmed.endsWith('}') || trimmed.endsWith(':')
                    || trimmed.startsWith('#'))
                return false;
            out.valid = true;
            out.line0 = t;
            out.expectedOldLine = lines[t];
            out.newLineText = rtrim(lines[t]) + ";";
            out.summary = QObject::tr("Add the missing ; at the end of "
                                      "line %1.").arg(t + 1);
            return true;
        }
        if (line0 < 0 || line0 >= lines.size())
            return false;
        const QString line = lines[line0];
        int firstNonWs = 0;
        while (firstNonWs < line.size() && line[firstNonWs].isSpace())
            ++firstNonWs;
        if (firstNonWs >= line.size())
            return false;
        const int col = issue->column;
        if (col > firstNonWs && col <= line.size()) {
            // The compiler points mid-line at the token before which
            // the ';' belongs.
            if (line[col - 1] == ';')
                return false;
            out.valid = true;
            out.line0 = line0;
            out.expectedOldLine = line;
            out.newLineText = line.left(col) + ";" + line.mid(col);
            out.summary = QObject::tr(
                        "Add the missing ; on line %1, just before the "
                        "word the compiler is pointing at.").arg(line0 + 1);
            return true;
        }
        // The pointed-at word starts its line: the line ABOVE it is
        // the one missing its ';'.
        int t = line0 - 1;
        while (t >= 0 && (lines[t].trimmed().isEmpty()
                          || lines[t].trimmed().startsWith('#')))
            --t;
        if (t < 0)
            return false;
        const QString trimmed = lines[t].trimmed();
        if (trimmed.endsWith(';') || trimmed.endsWith('{')
                || trimmed.endsWith('}') || trimmed.endsWith(':')
                || trimmed.endsWith('\\'))
            return false;
        out.valid = true;
        out.line0 = t;
        out.expectedOldLine = lines[t];
        out.newLineText = rtrim(lines[t]) + ";";
        out.summary = QObject::tr("Add the missing ; at the end of "
                                  "line %1.").arg(t + 1);
        return true;
    }

    if (kind == "closeBracket") {
        const QString qc = dbsQuoteClass();
        QRegularExpressionMatch m =
                QRegularExpression("expected\\s*" + qc + "(.)" + qc).match(desc);
        if (!m.hasMatch())
            return false;
        const QChar closer = m.captured(1).at(0);
        if (closer != ')' && closer != ']' && closer != '}')
            return false;
        if (line0 < 0 || line0 >= lines.size())
            return false;
        if (desc.contains("at end of input")) {
            if (closer == '}') {
                // Offered only when exactly ONE { in the whole file is
                // unclosed — the one unambiguous case.
                int balance = 0;
                for (const QString &l : lines)
                    balance += l.count('{') - l.count('}');
                if (balance != 1)
                    return false;
                out.valid = true;
                out.insertLine = true;
                out.line0 = lines.size();
                out.newLineText = "}";
                out.summary = QObject::tr(
                            "Add the missing } as a new last line of the "
                            "file — one { above is still open.");
                return true;
            }
            const QChar opener = closer == ')' ? '(' : '[';
            const QString line = lines[line0];
            if (line.count(opener) - line.count(closer) != 1)
                return false;
            out.valid = true;
            out.line0 = line0;
            out.expectedOldLine = line;
            out.newLineText = rtrim(line) + closer;
            out.summary = QObject::tr("Add the missing %1 at the end of "
                                      "line %2.").arg(closer).arg(line0 + 1);
            return true;
        }
        QRegularExpressionMatch bm =
                QRegularExpression("before\\s*" + qc + "(.)").match(desc);
        if (!bm.hasMatch())
            return false;
        const QChar token = bm.captured(1).at(0);
        const QString line = lines[line0];
        const int col = issue->column;
        if (col < 0 || col >= line.size() || line[col] != token)
            return false;
        out.valid = true;
        out.line0 = line0;
        out.expectedOldLine = line;
        out.newLineText = line.left(col) + closer + line.mid(col);
        out.summary = QObject::tr("Add the missing %1 on line %2, just "
                                  "before '%3'.")
                .arg(closer).arg(line0 + 1).arg(token);
        return true;
    }

    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// Part 2 — explanation dialog
// ---------------------------------------------------------------------------

void dbsShowIssueExplanation(QWidget *parent, const PCompileIssue &issue,
                             const DbsFixAction &fix,
                             const std::function<bool(bool)> &applyFix)
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
    auto *fixText = new QLabel(found ? QString::fromLatin1(found->fix)
                                 : QObject::tr("1) Always fix the FIRST error in the list first, then compile again — half the list often disappears. 2) Read the reported line AND the line above it; missing ; and } almost always hide one line earlier. 3) Compare the spelling of names letter by letter."));
    fixText->setWordWrap(true);
    layout->addWidget(fixText);

    // v1.5.1 Fix Lagao: when a deterministic fix was computed, show the
    // exact line that will result BEFORE anything is applied. Nothing
    // is edited until the user clicks one of the fix buttons below.
    QLabel *fixStatus = nullptr;
    if (fix.valid) {
        auto *fixHead = new QLabel(QObject::tr("One-click fix (Fix Lagao):"));
        fixHead->setStyleSheet("font-weight: bold; margin-top: 10px;");
        layout->addWidget(fixHead);
        auto *fixSummary = new QLabel(fix.summary);
        fixSummary->setWordWrap(true);
        layout->addWidget(fixSummary);
        auto *preview = new QLabel;
        preview->setStyleSheet("font-family: monospace;");
        preview->setWordWrap(true);
        if (fix.insertLine)
            preview->setText(QObject::tr("New line %1:  %2")
                             .arg(fix.line0 + 1).arg(fix.newLineText));
        else
            preview->setText(QObject::tr("Now:  %1\nWill become:  %2")
                             .arg(fix.expectedOldLine, fix.newLineText));
        layout->addWidget(preview);
        fixStatus = new QLabel;
        fixStatus->setWordWrap(true);
        fixStatus->setStyleSheet("font-weight: bold; color: #2e7d32;");
        layout->addWidget(fixStatus);
    }

    layout->addStretch();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::close);
    if (fix.valid && applyFix) {
        auto *fixBtn = new QPushButton(QObject::tr("Fix it for me"), dialog);
        auto *recheckBtn = new QPushButton(QObject::tr("Fix && Recheck"), dialog);
        buttons->addButton(fixBtn, QDialogButtonBox::ActionRole);
        buttons->addButton(recheckBtn, QDialogButtonBox::ActionRole);
        QObject::connect(fixBtn, &QPushButton::clicked, dialog,
                         [applyFix, fixBtn, recheckBtn, fixStatus]() {
            if (applyFix(false)) {
                fixBtn->setEnabled(false);
                recheckBtn->setEnabled(false);
                fixStatus->setText(QObject::tr(
                    "Fix applied ✓ — press F9 (Compile) to check. "
                    "Ctrl+Z undoes the fix."));
            } else {
                fixStatus->setText(QObject::tr(
                    "The fix could not be applied — the line has changed "
                    "since the compile. Close this, compile again, and "
                    "double-click the fresh error."));
            }
        });
        QObject::connect(recheckBtn, &QPushButton::clicked, dialog,
                         [applyFix, dialog, fixStatus]() {
            if (applyFix(true)) {
                dialog->close();
            } else {
                fixStatus->setText(QObject::tr(
                    "The fix could not be applied — the line has changed "
                    "since the compile. Close this, compile again, and "
                    "double-click the fresh error."));
            }
        });
    }
    layout->addWidget(buttons);
    if (fix.valid)
        dialog->resize(560, 480);
    dialog->show();
}

void MainWindow::dbsExplainIssueAt(const QModelIndex &index)
{
    if (!index.isValid())
        return;
    PCompileIssue issue = ui->tableIssues->issue(index);
    if (!issue)
        return;
    // v1.5.1: try to compute a one-click fix from the live editor text.
    DbsFixAction fix;
    const ErrorExplanation *entry = explainError(issue->description);
    Editor *editor = mEditorManager->getOpenedEditor(issue->filename);
    if (!editor && !issue->filename.isEmpty()) {
        openFile(issue->filename);
        editor = mEditorManager->getOpenedEditor(issue->filename);
    }
    if (entry && editor && !editor->readOnly())
        dbsComputeIssueFix(issue, entry, editor->content(), fix);
    dbsShowIssueExplanation(this, issue, fix,
                            [this, issue, fix](bool recheck) {
        if (!dbsApplyFixToEditor(issue, fix))
            return false;
        if (recheck)
            dbsStartRecheck(issue);
        else
            statusBar()->showMessage(
                        tr("Fix applied ✓ — press F9 (Compile) to check. "
                           "(Ctrl+Z undoes the fix.)"), 10000);
        return true;
    });
}

bool MainWindow::dbsApplyFixToEditor(const PCompileIssue &issue,
                                     const DbsFixAction &fix)
{
    if (!issue || !fix.valid)
        return false;
    Editor *editor = mEditorManager->getOpenedEditor(issue->filename);
    if (!editor && !issue->filename.isEmpty()) {
        openFile(issue->filename);
        editor = mEditorManager->getOpenedEditor(issue->filename);
    }
    if (!editor || editor->readOnly())
        return false;
    if (fix.insertLine) {
        if (fix.line0 < 0 || fix.line0 > editor->lineCount())
            return false;
        if (fix.line0 < editor->lineCount()) {
            editor->setCaretXY(QSynedit::CharPos{0, fix.line0});
            editor->setSelText(fix.newLineText + "\n");
        } else {
            const int last = editor->lineCount() - 1;
            editor->setCaretXY(QSynedit::CharPos{
                                   editor->lineText(last).length(), last});
            editor->setSelText("\n" + fix.newLineText);
        }
    } else {
        if (fix.line0 < 0 || fix.line0 >= editor->lineCount())
            return false;
        // The line must still be exactly the one the fix was computed
        // from — otherwise the code has moved on and guessing would
        // be dangerous.
        if (editor->lineText(fix.line0) != fix.expectedOldLine)
            return false;
        editor->replaceLine(fix.line0, fix.newLineText);
    }
    mEditorManager->activeEditor(editor, true);
    editor->setFocus();
    return true;
}

// ---------------------------------------------------------------------------
// v1.5.1 — Fix & Recheck: after a fix is applied, recompile the same
// file and judge honestly from the fresh Issues list whether THAT
// issue (same file, same message) is still reported. An inserted
// #include shifts every line number, so the line is not compared.
// ---------------------------------------------------------------------------

void MainWindow::dbsStartRecheck(const PCompileIssue &issue)
{
    if (!ui->actionCompile->isEnabled()) {
        statusBar()->showMessage(
                    tr("Fix applied ✓ — but the recheck could not run: "
                       "Compile is not available for this file. Press F9 "
                       "(Compile) yourself."), 12000);
        return;
    }
    mDbsRecheckPending = true;
    const int id = ++mDbsRecheckId;
    mDbsRecheckFilename = QFileInfo(issue->filename).absoluteFilePath();
    mDbsRecheckDescription = issue->description;
    statusBar()->showMessage(
                tr("Fix applied ✓ — recompiling to check whether that "
                   "error is gone…"));
    QTimer::singleShot(60000, this, [this, id]() {
        if (mDbsRecheckPending && mDbsRecheckId == id) {
            mDbsRecheckPending = false;
            statusBar()->showMessage(
                        tr("Fix & Recheck: the compile did not report "
                           "back, so that error could not be rechecked. "
                           "Press F9 (Compile) yourself."), 12000);
        }
    });
    on_actionCompile_triggered();
}

void MainWindow::dbsOnRecheckCompileFinished(const QString &filename,
                                             bool isCheckSyntax)
{
    if (!mDbsRecheckPending || isCheckSyntax)
        return;
    mDbsRecheckPending = false;
    if (QFileInfo(filename).absoluteFilePath() != mDbsRecheckFilename) {
        statusBar()->showMessage(
                    tr("Fix & Recheck: a different file was compiled "
                       "(%1), so that error could not be rechecked.")
                    .arg(QFileInfo(filename).fileName()), 12000);
        return;
    }
    // Judge on the next event-loop turn, when the fresh issues have
    // all landed in the table.
    QTimer::singleShot(0, this, [this]() {
        bool stillThere = false;
        for (const PCompileIssue &it : ui->tableIssues->issues()) {
            if (it && QFileInfo(it->filename).absoluteFilePath()
                        == mDbsRecheckFilename
                    && it->description == mDbsRecheckDescription) {
                stillThere = true;
                break;
            }
        }
        statusBar()->showMessage(stillThere
                    ? tr("Fix & Recheck: that error is STILL reported "
                         "after recompiling — it needs another look.")
                    : tr("Fix & Recheck: that error is GONE ✓ — the "
                         "recompile does not report it any more."),
                    15000);
    });
}

// ---------------------------------------------------------------------------
// v1.5.1 — the searchable error dictionary window. Fully offline; it
// reads the same ERROR_DICTIONARY the explanation popup uses, so it
// can be browsed without compiling anything.
// ---------------------------------------------------------------------------

void dbsShowErrorDictionary(QWidget *parent)
{
    auto *dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QObject::tr(
                "Error Dictionary — simple words for compiler messages"));
    dialog->resize(760, 540);
    auto *layout = new QVBoxLayout(dialog);

    auto *search = new QLineEdit(dialog);
    search->setPlaceholderText(QObject::tr(
                "Type part of an error message (or paste one!), or a word "
                "like semicolon, unpack, NULL…"));
    layout->addWidget(search);

    auto *countLabel = new QLabel(dialog);
    layout->addWidget(countLabel);

    auto *list = new QListWidget(dialog);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(list, 1);

    auto *detailTitle = new QLabel(dialog);
    detailTitle->setStyleSheet("font-weight: bold; font-size: 12pt;");
    detailTitle->setWordWrap(true);
    layout->addWidget(detailTitle);
    auto *detail = new QLabel(dialog);
    detail->setWordWrap(true);
    detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(detail);

    struct DictRow {
        const ErrorExplanation *entry;
        const ErrorFixRule *rule;
    };
    QVector<DictRow> rows;
    for (const ErrorExplanation &entry : ERROR_DICTIONARY)
        rows.append({&entry, fixRuleForEntry(&entry)});

    auto refresh = [search, list, countLabel, rows]() {
        const QString query = search->text().trimmed();
        const QStringList terms = query.toLower().split(
                    QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        list->clear();
        // If the typed text IS a compiler message, the entry whose
        // pattern matches it is almost surely the one wanted — those
        // float to the top with a star.
        QVector<int> starred;
        QVector<int> plain;
        for (int i = 0; i < rows.size(); ++i) {
            const DictRow &row = rows[i];
            bool regexHit = false;
            if (!query.isEmpty()) {
                QRegularExpression re(
                            QString::fromLatin1(row.entry->pattern),
                            QRegularExpression::CaseInsensitiveOption);
                regexHit = re.isValid() && re.match(query).hasMatch();
            }
            bool textHit = terms.isEmpty();
            if (!textHit) {
                const QString hay = (QString::fromLatin1(row.entry->title)
                        + ' ' + QString::fromLatin1(row.entry->what) + ' '
                        + QString::fromLatin1(row.entry->fix) + ' '
                        + QString::fromLatin1(row.entry->pattern)).toLower();
                textHit = true;
                for (const QString &t : terms)
                    if (!hay.contains(t)) {
                        textHit = false;
                        break;
                    }
            }
            if (regexHit)
                starred.append(i);
            else if (textHit)
                plain.append(i);
        }
        auto addItem = [list](int idx, const DictRow &row, bool star) {
            QString text = QString::fromLatin1(row.entry->title);
            if (row.rule)
                text += QObject::tr("  ⚡ one-click fix");
            if (star)
                text = QStringLiteral("★ ") + text;
            auto *item = new QListWidgetItem(text, list);
            item->setData(Qt::UserRole, idx);
        };
        for (int i : starred)
            addItem(i, rows[i], true);
        for (int i : plain)
            addItem(i, rows[i], false);
        countLabel->setText(QObject::tr(
                    "Showing %1 of %2 entries. Tip: double-click any error "
                    "in the Issues list to open its own page.")
                    .arg(list->count()).arg(rows.size()));
        if (list->count() > 0)
            list->setCurrentRow(0);
    };
    QObject::connect(search, &QLineEdit::textChanged, dialog,
                     [refresh]() { refresh(); });
    QObject::connect(list, &QListWidget::currentItemChanged, dialog,
                     [detailTitle, detail, rows](QListWidgetItem *item) {
        if (!item) {
            detailTitle->clear();
            detail->clear();
            return;
        }
        const int idx = item->data(Qt::UserRole).toInt();
        if (idx < 0 || idx >= rows.size())
            return;
        const DictRow &row = rows[idx];
        detailTitle->setText(QString::fromLatin1(row.entry->title));
        QString text = QString::fromLatin1(row.entry->what)
                + "\n\n" + QObject::tr("How to fix: ")
                + QString::fromLatin1(row.entry->fix);
        if (row.rule)
            text += "\n\n" + QObject::tr("⚡ One-click fix available when "
                        "this error appears: %1.")
                        .arg(fixKindSummary(row.rule));
        else
            text += "\n\n" + QObject::tr(
                        "No one-click fix for this one — the right repair "
                        "needs your judgement.");
        detail->setText(text);
    });
    refresh();
    dialog->show();
    search->setFocus();
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
    // v1.5.1: the searchable error dictionary (same data as the
    // double-click explanation; no compile needed to browse it).
    QAction *dictAction = ui->menuHelp->addAction(tr("Error Dictionary..."));
    connect(dictAction, &QAction::triggered, this,
            [this]() { dbsShowErrorDictionary(this); });
    // v1.5.1: Fix & Recheck listens to the same compile-finished
    // signal as the normal flow; it stays silent unless a recheck is
    // pending.
    connect(mCompilerManager, &CompilerManager::compileFinished,
            this, &MainWindow::dbsOnRecheckCompileFinished);

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
            dbsShowIssueExplanation(this, issue, DbsFixAction(), nullptr);
        });
    }
    if (!qEnvironmentVariable("DBS_TEST_FIX").isEmpty()) {
        // v1.5.1: open a file whose only fault is a missing ';' before
        // 'return', then show its explanation WITH the computed fix.
        QTimer::singleShot(2500, this, [this]() {
            QString path = QDir::temp().filePath("dbs-test-fix.cpp");
            QFile f(path);
            if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                f.write("#include <iostream>\nusing namespace std;\n"
                        "int main() {\n    cout << \"Hello\" << endl\n"
                        "    return 0;\n}\n");
                f.close();
            }
            openFile(path);
            QTimer::singleShot(900, this, [this, path]() {
                auto issue = std::make_shared<CompileIssue>();
                issue->filename = path;
                issue->line = 4;    // the 'return 0;' line (0-based)
                issue->column = 4;  // 'return' starts at ch 4
                issue->description = "error: expected ';' before 'return'";
                issue->type = CompileIssueType::Error;
                DbsFixAction fix;
                const ErrorExplanation *entry =
                        explainError(issue->description);
                Editor *editor = mEditorManager->getOpenedEditor(path);
                if (entry && editor)
                    dbsComputeIssueFix(issue, entry, editor->content(), fix);
                dbsShowIssueExplanation(this, issue, fix,
                                        [this, issue, fix](bool recheck) {
                    if (!dbsApplyFixToEditor(issue, fix))
                        return false;
                    if (recheck)
                        dbsStartRecheck(issue);
                    return true;
                });
            });
        });
    }
    if (!qEnvironmentVariable("DBS_TEST_DICT").isEmpty()) {
        QTimer::singleShot(2500, this, [this]() {
            dbsShowErrorDictionary(this);
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
        bool setupAsset = false;
        for (const QJsonValue &value : root.value("assets").toArray()) {
            QJsonObject asset = value.toObject();
            if (asset.value("name").toString() == "DevBySufyan-Setup.exe") {
                downloadUrl = asset.value("browser_download_url").toString();
                setupAsset = true;
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
        box.setInformativeText(setupAsset
            ? tr("Download and install it from inside the app — no "
                 "browser needed. Your settings and files stay as "
                 "they are.")
            : tr("Download the new setup and run it — "
                 "your settings and files stay as they are."));
        QPushButton *downloadBtn = box.addButton(
                    setupAsset ? tr("Download && Install") : tr("Download"),
                    QMessageBox::AcceptRole);
        box.addButton(tr("Later"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() == downloadBtn) {
            // v1.5.0: the setup downloads and launches from inside
            // the app; the browser route is the fallback (and the
            // route for a release with no setup asset attached).
            if (setupAsset)
                dbsDownloadAndInstallUpdate(downloadUrl, latest);
            else
                QDesktopServices::openUrl(QUrl(downloadUrl));
        }
    });
}

// ---------------------------------------------------------------------------
// Part 3b — in-app update download (v1.5.0). The setup is fetched with
// a progress dialog and launched from inside the app, so an update no
// longer sends the user to the browser; any failure falls back to the
// v1.4.0 browser route.
// ---------------------------------------------------------------------------

namespace {

struct UpdateDownloadState {
    bool cancelled = false;
};

} // namespace

void MainWindow::dbsDownloadAndInstallUpdate(const QString &url,
                                             const QString &version)
{
    const QString filePath =
            QStandardPaths::writableLocation(QStandardPaths::TempLocation)
            + QStringLiteral("/DevBySufyan-Setup-v%1.exe").arg(version);
    auto *file = new QFile(filePath);
    if (!file->open(QIODevice::WriteOnly)) {
        delete file;
        QMessageBox::warning(
                    this, tr("Update download"),
                    tr("The setup could not be saved on this computer. "
                       "The download page will open in your browser "
                       "instead."));
        QDesktopServices::openUrl(QUrl(url));
        return;
    }
    auto state = std::make_shared<UpdateDownloadState>();
    auto *net = new QNetworkAccessManager(this);
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QString("DevBySufyan/%1").arg(DBS_VERSION));
    // The setup asset URL redirects to the file host; follow it.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = net->get(request);
    auto *dialog = new QProgressDialog(
                tr("Downloading Dev by Sufyan v%1...").arg(version),
                tr("Cancel"), 0, 100, this);
    dialog->setWindowTitle(tr("Update download"));
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setAutoClose(false);
    dialog->setAutoReset(false);
    dialog->setMinimumDuration(0);
    dialog->setValue(0);
    dialog->show();

    connect(dialog, &QProgressDialog::canceled, this, [state, reply]() {
        state->cancelled = true;
        reply->abort();
    });
    connect(reply, &QNetworkReply::readyRead, this, [reply, file]() {
        file->write(reply->readAll());
    });
    connect(reply, &QNetworkReply::downloadProgress, this,
            [dialog](qint64 received, qint64 total) {
        if (total > 0) {
            if (dialog->maximum() == 0)
                dialog->setMaximum(100);
            dialog->setValue(int(received * 100 / total));
        } else {
            dialog->setMaximum(0); // busy bar while the size is unknown
        }
        dialog->setLabelText(tr("Downloading... %1 MB received")
                             .arg(received / 1048576.0, 0, 'f', 1));
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, net, file, dialog, state, url, version,
             filePath]() {
        file->write(reply->readAll());
        file->close();
        const bool failed = reply->error() != QNetworkReply::NoError;
        const qint64 size = QFileInfo(filePath).size();
        dialog->close();
        dialog->deleteLater();
        reply->deleteLater();
        net->deleteLater();
        delete file;
        if (state->cancelled) {
            QFile::remove(filePath);
            return; // the user changed their mind — stay silent
        }
        if (failed || size <= 0) {
            QFile::remove(filePath);
            QMessageBox::warning(
                        this, tr("Update download"),
                        tr("The download inside the app did not finish. "
                           "The download page will open in your browser "
                           "instead."));
            QDesktopServices::openUrl(QUrl(url));
            return;
        }
        QMessageBox::information(
                        this, tr("Download complete"),
                        tr("Dev by Sufyan v%1 is ready. The app will "
                           "close now and the setup will open — your "
                           "settings and files stay as they are.")
                        .arg(version));
        if (QProcess::startDetached(filePath, QStringList())) {
            QApplication::quit();
        } else {
            QMessageBox::warning(
                        this, tr("Update download"),
                        tr("The setup was downloaded but could not be "
                           "started. You can run it yourself:\n%1")
                        .arg(filePath));
        }
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
