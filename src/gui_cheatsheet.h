/* Cheat sheet entries for the GUI.
 *
 * One row per thing someone might want to do, with the shortest snippet that
 * does it. The search box matches on all three fields, so the wording here is
 * what makes something findable - "repeat", "loop" and "again" should all lead
 * to the while loop.
 *
 * Snippets use \r\n because they are inserted straight into an EDIT control. */
#ifndef ADDA_GUI_CHEATSHEET_H
#define ADDA_GUI_CHEATSHEET_H

typedef struct {
    const char *title;
    const char *about;     /* one line, and extra words to search on */
    const char *snippet;
} Cheat;

static const Cheat CHEATS[] = {

/* ---- the basics ---------------------------------------------------- */
{ "Show something on screen",
  "print writes the rest of the line. Text needs no quotes. say, write, output",
  "print Hello, world" },

{ "Put a value inside text",
  "Square brackets around a name mean its value. show variable, insert, interpolate",
  "print Hello, [name]" },

{ "Make a variable",
  "A variable is always written in square brackets. A bare word is text, a number is a number. set, assign, store",
  "[name] = Rocco\r\n[age] = 30" },

{ "Maths inside text",
  "Braces hold a sum; brackets name the variables in it. calculate, insert",
  "print Next year you will be {[age] + 1}" },

{ "Copy a value",
  "Brackets on the right mean the value; a bare word would just be text. duplicate",
  "[total] = [count]" },

{ "Write a comment",
  "Anything after # is ignored. note, remark",
  "# this line is a note to yourself" },

/* ---- maths --------------------------------------------------------- */
{ "Do maths",
  "Operators need a space on BOTH sides. add, subtract, multiply, divide, plus",
  "[next] = [age] + 1\r\n[half] = [age] / 2\r\n[left] = 10 % 3" },

{ "Text that contains a dash or a slash",
  "Without spaces it stays text, so dates and names are safe. hyphen, minus",
  "[date] = 2024-01-15\r\n[who] = Mary-Jane" },

{ "Text with an operator in it",
  "Quotes force text when it would otherwise be maths. escape, literal",
  "[title] = \"Chapter 1 - Intro\"" },

{ "Negate a number",
  "A lone minus needs a space, so take it away from zero instead. negative",
  "[opposite] = 0 - [x]" },

/* ---- asking -------------------------------------------------------- */
{ "Ask a question",
  "Reads one line from whoever runs the program. input, read, prompt, entry",
  "[name] = ask What is your name?\r\nprint Hello, [name]" },

{ "Ask for a number",
  "An answer that looks like a number becomes one. input number",
  "[age] = ask How old are you?\r\nprint Next year you will be {[age] + 1}" },

{ "Read a line with no question",
  "ask on its own just waits for a line. input",
  "[said] = ask" },

/* ---- choosing ------------------------------------------------------ */
{ "Do something only sometimes",
  "if runs a block when a test is true. condition, when, branch",
  "if score > 10\r\n    print You win\r\nend" },

{ "Choose between two things",
  "else covers everything the if did not. otherwise",
  "if [age] > 17\r\n    print Adult\r\nelse\r\n    print Child\r\nend" },

{ "Choose between many things",
  "else if chains as many tests as you like. elif, switch, case",
  "if [n] > 10\r\n    print big\r\nelse if [n] > 5\r\n    print middle\r\nelse\r\n    print small\r\nend" },

{ "Check if two things are the same",
  "is compares. equals, equal, same, matches",
  "if [name] is Rocco\r\n    print Hello again\r\nend" },

{ "Check something is NOT the same",
  "is not, or !=. different, unequal",
  "if [name] is not Bob\r\n    print You are not Bob\r\nend" },

{ "Check a range",
  "Comparisons chain the way they read. between, within",
  "if 1 < x < 10\r\n    print In range\r\nend" },

{ "Combine two tests",
  "and, or, not. both, either, negate",
  "if [age] > 18 and [name] is Rocco\r\n    print Both\r\nend" },

/* ---- repeating ----------------------------------------------------- */
{ "Repeat while something is true",
  "while runs until its test stops being true. loop, again, repeat",
  "[count] = 0\r\nwhile [count] < 5\r\n    print [count]\r\n    [count] = [count] + 1\r\nend" },

{ "Go through every item",
  "for each walks a list or a map. loop, iterate, foreach",
  "for each [n] in [nums]\r\n    print [n]\r\nend" },

/* ---- lists --------------------------------------------------------- */
{ "Make a list",
  "Commas separate the items. array, collection",
  "[nums] = list 10, 20, 30" },

{ "Make an empty list",
  "list on its own. empty array",
  "[scores] = list" },

{ "Get an item out of a list",
  "Lists count from 1, not 0. index, element, position",
  "print {item 1 of [nums]}" },

{ "Change an item in a list",
  "Put the item on the left of the =. replace, set index",
  "item 1 of [nums] = 99" },

{ "Add to a list",
  "add ... to. append, push, insert",
  "add 40 to [nums]" },

{ "Take something out of a list",
  "remove item N of. delete, drop",
  "remove item 2 of [nums]" },

{ "How many are there?",
  "length of works on lists, maps and text. size, count, len",
  "print {length of [nums]}" },

{ "Is it in there?",
  "has ... of asks. contains, includes, member",
  "print {has 40 of [nums]}" },

/* ---- maps ---------------------------------------------------------- */
{ "Make a map",
  "A map holds values under names. dictionary, object, record, lookup",
  "[person] = map" },

{ "Put something in a map",
  "The key is the word you write. set key, store",
  "name of [person] = Rocco\r\nage of [person] = 30" },

{ "Read something out of a map",
  "key of map. get, lookup",
  "print {name of [person]}" },

{ "Use a key held in a variable",
  "item takes the key as a value rather than a word. dynamic key",
  "[k] = name\r\nprint {item [k] of [person]}" },

{ "Check a map has a key",
  "has with quotes around a literal key. contains",
  "print {has \"age\" of [person]}" },

{ "Go through a map",
  "Keys come back in the order you added them. iterate, keys",
  "for each [key] in [person]\r\n    print [key] is {item [key] of [person]}\r\nend" },

{ "Wait before doing something",
  "Waits this many milliseconds (1000 is a second), then runs the lines under it. pause, sleep, wait, timer, later",
  "delay - 1500\r\n    print hello\r\n    print hi\r\ndelay end" },

{ "Open a blank window",
  "Opens a window with this title. Everything printed after it appears in the middle of the window. app, application, window, gui, screen",
  "openApplication My App\r\nprint Hello from my app" },

{ "Draw a rounded box in the window",
  "After openApplication. Each 3px line says how far the box stops from those edges. shape, rectangle, panel, card",
  "insert rounded box\r\n3px top,right,left" },

{ "Draw a square box in the window",
  "Like a rounded box with sharp corners. shape, rectangle, square",
  "insert box\r\n20px top,bottom\r\n40px left,right" },

/* ---- functions ----------------------------------------------------- */
{ "Make a function",
  "define ... with names the inputs, each in square brackets. procedure, method, subroutine",
  "define greet with [person]\r\n    print Hello [person]\r\nend" },

{ "Use a function",
  "call it by name. invoke, run",
  "call greet with Rocco" },

{ "Give an answer back",
  "return hands a value to whoever called. result, output",
  "define add_up with [a], [b]\r\n    return [a] + [b]\r\nend\r\n\r\nprint {call add_up with 2, 3}" },

{ "A function that calls itself",
  "Recursion works, and functions can be defined below where they are used.",
  "define factorial with [n]\r\n    if [n] <= 1\r\n        return 1\r\n    end\r\n    return [n] * call factorial with [n] - 1\r\nend" },

/* ---- odds and ends ------------------------------------------------- */
{ "True, false and nothing",
  "The three fixed values. boolean, null, nil, empty",
  "[ok] = true\r\n[done] = false\r\n[result] = nothing" },

{ "Print a literal brace",
  "Double it up. curly, escape",
  "print These are braces: {{ }}" },
};

#define CHEAT_COUNT ((int)(sizeof(CHEATS) / sizeof(CHEATS[0])))

#endif /* ADDA_GUI_CHEATSHEET_H */
