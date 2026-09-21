/* Cheat sheet entries for the GUI.
 *
 * One row per thing someone might want to do, with the shortest snippet that
 * does it. The search box matches on all three fields, so the wording here is
 * what makes something findable - "repeat", "loop" and "again" should all lead
 * to the while loop.
 *
 * The first page is a list of topics; each topic is its own page, which starts
 * with a link back. A search from the topics page looks through every topic.
 *
 * Snippets use \r\n because they are inserted straight into an EDIT control. */
#ifndef ADDA_GUI_CHEATSHEET_H
#define ADDA_GUI_CHEATSHEET_H

typedef struct {
    const char *title;
    const char *about;     /* one line, and extra words to search on */
    const char *snippet;   /* NULL: a link, to page `page` */
    int         page;
} Cheat;

/* ---- the topics page ------------------------------------------------ */
static const Cheat CHEAT_TOPICS[] = {
{ "The basics ->",
  "Printing, variables, comments",
  NULL, 1 },

{ "Maths ->",
  "Sums, and text that looks like maths",
  NULL, 2 },

{ "Asking questions ->",
  "Reading what someone types",
  NULL, 3 },

{ "Making choices ->",
  "if, else, and comparing things",
  NULL, 4 },

{ "Repeating and waiting ->",
  "Loops, and pausing with delay",
  NULL, 5 },

{ "Lists ->",
  "Making, changing and searching lists",
  NULL, 6 },

{ "Maps ->",
  "Values stored under names",
  NULL, 7 },

{ "Functions ->",
  "Naming a piece of code to use again",
  NULL, 8 },

{ "Windows ->",
  "Opening a window of your own",
  NULL, 9 },

{ "Shapes ->",
  "Every shape you can draw with insert",
  NULL, 10 },

{ "Odds and ends ->",
  "True, false, nothing, and literal braces",
  NULL, 11 },
};

/* ---- the basics -------------------------------------------------------- */
static const Cheat CHEAT_THE_BASICS[] = {
{ "<- Back to all topics",
  "Click to go back to the list of topics.",
  NULL, 0 },

{ "Show something on screen",
  "print writes the rest of the line. Text needs no quotes. say, write, output",
  "print Hello, world", 0 },

{ "Put a value inside text",
  "Square brackets around a name mean its value. show variable, insert, interpolate",
  "print Hello, [name]", 0 },

{ "Make a variable",
  "A variable is always written in square brackets. A bare word is text, a number is a number. set, assign, store",
  "[name] = Rocco\r\n[age] = 30", 0 },

{ "Maths inside text",
  "Braces hold a sum; brackets name the variables in it. calculate, insert",
  "print Next year you will be {[age] + 1}", 0 },

{ "Copy a value",
  "Brackets on the right mean the value; a bare word would just be text. duplicate",
  "[total] = [count]", 0 },

{ "Write a comment",
  "Anything after # is ignored. note, remark",
  "# this line is a note to yourself", 0 },
};

/* ---- maths ------------------------------------------------------------- */
static const Cheat CHEAT_MATHS[] = {
{ "<- Back to all topics",
  "Click to go back to the list of topics.",
  NULL, 0 },

{ "Do maths",
  "Operators need a space on BOTH sides. add, subtract, multiply, divide, plus",
  "[next] = [age] + 1\r\n[half] = [age] / 2\r\n[left] = 10 % 3", 0 },

{ "Text that contains a dash or a slash",
  "Without spaces it stays text, so dates and names are safe. hyphen, minus",
  "[date] = 2024-01-15\r\n[who] = Mary-Jane", 0 },

{ "Text with an operator in it",
  "Quotes force text when it would otherwise be maths. escape, literal",
  "[title] = \"Chapter 1 - Intro\"", 0 },

{ "Negate a number",
  "A lone minus needs a space, so take it away from zero instead. negative",
  "[opposite] = 0 - [x]", 0 },
};

/* ---- asking questions -------------------------------------------------- */
static const Cheat CHEAT_ASKING[] = {
{ "<- Back to all topics",
  "Click to go back to the list of topics.",
  NULL, 0 },

{ "Ask a question",
  "Reads one line from whoever runs the program. input, read, prompt, entry",
  "[name] = ask What is your name?\r\nprint Hello, [name]", 0 },

{ "Ask for a number",
  "An answer that looks like a number becomes one. input number",
  "[age] = ask How old are you?\r\nprint Next year you will be {[age] + 1}", 0 },

{ "Read a line with no question",
  "ask on its own just waits for a line. input",
  "[said] = ask", 0 },
};

/* ---- making choices ---------------------------------------------------- */
static const Cheat CHEAT_CHOOSING[] = {
{ "<- Back to all topics",
  "Click to go back to the list of topics.",
  NULL, 0 },

{ "Do something only sometimes",
  "if runs a block when a test is true. condition, when, branch",
  "if score > 10\r\n    print You win\r\nend", 0 },

{ "Choose between two things",
  "else covers everything the if did not. otherwise",
  "if [age] > 17\r\n    print Adult\r\nelse\r\n    print Child\r\nend", 0 },

{ "Choose between many things",
  "else if chains as many tests as you like. elif, switch, case",
  "if [n] > 10\r\n    print big\r\nelse if [n] > 5\r\n    print middle\r\nelse\r\n    print small\r\nend", 0 },

{ "Check if two things are the same",
  "is compares. equals, equal, same, matches",
  "if [name] is Rocco\r\n    print Hello again\r\nend", 0 },

{ "Check something is NOT the same",
  "is not, or !=. different, unequal",
  "if [name] is not Bob\r\n    print You are not Bob\r\nend", 0 },

{ "Check a range",
  "Comparisons chain the way they read. between, within",
  "if 1 < x < 10\r\n    print In range\r\nend", 0 },

{ "Combine two tests",
  "and, or, not. both, either, negate",
  "if [age] > 18 and [name] is Rocco\r\n    print Both\r\nend", 0 },
};

/* ---- repeating and waiting --------------------------------------------- */
static const Cheat CHEAT_REPEATING[] = {
{ "<- Back to all topics",
  "Click to go back to the list of topics.",
  NULL, 0 },

{ "Repeat while something is true",
  "while runs until its test stops being true. loop, again, repeat",
  "[count] = 0\r\nwhile [count] < 5\r\n    print [count]\r\n    [count] = [count] + 1\r\nend", 0 },

{ "Go through every item",
  "for each walks a list or a map. loop, iterate, foreach",
  "for each [n] in [nums]\r\n    print [n]\r\nend", 0 },

{ "Wait before doing something",
  "Waits this many milliseconds (1000 is a second), then runs the lines under it. pause, sleep, wait, timer, later",
  "delay - 1500\r\n    print hello\r\n    print hi\r\ndelay end", 0 },
};

/* ---- lists ------------------------------------------------------------- */
static const Cheat CHEAT_LISTS[] = {
{ "<- Back to all topics",
  "Click to go back to the list of topics.",
  NULL, 0 },

{ "Make a list",
  "Commas separate the items. array, collection",
  "[nums] = list 10, 20, 30", 0 },

{ "Make an empty list",
  "list on its own. empty array",
  "[scores] = list", 0 },

{ "Get an item out of a list",
  "Lists count from 1, not 0. index, element, position",
  "print {item 1 of [nums]}", 0 },

{ "Change an item in a list",
  "Put the item on the left of the =. replace, set index",
  "item 1 of [nums] = 99", 0 },

{ "Add to a list",
  "add ... to. append, push, insert",
  "add 40 to [nums]", 0 },

{ "Take something out of a list",
  "remove item N of. delete, drop",
  "remove item 2 of [nums]", 0 },

{ "How many are there?",
  "length of works on lists, maps and text. size, count, len",
  "print {length of [nums]}", 0 },

{ "Is it in there?",
  "has ... of asks. contains, includes, member",
  "print {has 40 of [nums]}", 0 },
};

/* ---- maps -------------------------------------------------------------- */
static const Cheat CHEAT_MAPS[] = {
{ "<- Back to all topics",
  "Click to go back to the list of topics.",
  NULL, 0 },

{ "Make a map",
  "A map holds values under names. dictionary, object, record, lookup",
  "[person] = map", 0 },

{ "Put something in a map",
  "The key is the word you write. set key, store",
  "name of [person] = Rocco\r\nage of [person] = 30", 0 },

{ "Read something out of a map",
  "key of map. get, lookup",
  "print {name of [person]}", 0 },

{ "Use a key held in a variable",
  "item takes the key as a value rather than a word. dynamic key",
  "[k] = name\r\nprint {item [k] of [person]}", 0 },

{ "Check a map has a key",
  "has with quotes around a literal key. contains",
  "print {has \"age\" of [person]}", 0 },

{ "Go through a map",
  "Keys come back in the order you added them. iterate, keys",
  "for each [key] in [person]\r\n    print [key] is {item [key] of [person]}\r\nend", 0 },
};

/* ---- functions --------------------------------------------------------- */
static const Cheat CHEAT_FUNCTIONS[] = {
{ "<- Back to all topics",
  "Click to go back to the list of topics.",
  NULL, 0 },

{ "Make a function",
  "define ... with names the inputs, each in square brackets. procedure, method, subroutine",
  "define greet with [person]\r\n    print Hello [person]\r\nend", 0 },

{ "Use a function",
  "call it by name. invoke, run",
  "call greet with Rocco", 0 },

{ "Give an answer back",
  "return hands a value to whoever called. result, output",
  "define add_up with [a], [b]\r\n    return [a] + [b]\r\nend\r\n\r\nprint {call add_up with 2, 3}", 0 },

{ "A function that calls itself",
  "Recursion works, and functions can be defined below where they are used.",
  "define factorial with [n]\r\n    if [n] <= 1\r\n        return 1\r\n    end\r\n    return [n] * call factorial with [n] - 1\r\nend", 0 },
};

/* ---- windows ----------------------------------------------------------- */
static const Cheat CHEAT_WINDOWS[] = {
{ "<- Back to all topics",
  "Click to go back to the list of topics.",
  NULL, 0 },

{ "Open a blank window",
  "Opens a window with this title. Everything printed after it appears in the middle of the window. app, application, window, gui, screen",
  "openApplication My App\r\nprint Hello from my app", 0 },
};

/* ---- shapes ------------------------------------------------------------ */
static const Cheat CHEAT_SHAPES[] = {
{ "<- Back to all topics",
  "Click to go back to the list of topics.",
  NULL, 0 },

{ "Box",
  "Square corners. Each 20px line says how far it stops from those edges. rectangle, square",
  "insert box\r\n20px top,left", 0 },

{ "Rounded box",
  "A box with soft corners. panel, card, rectangle",
  "insert rounded box\r\n3px top,right,left", 0 },

{ "Pill",
  "A box whose ends are fully round. capsule, button, lozenge",
  "insert pill\r\n20px left,right\r\n40px bottom", 0 },

{ "Set a shape's height or width",
  "A size line after the distances. Size wins; the edges say where it sits. tall, wide, size, strip, bar",
  "insert box\r\n15px top,left,right\r\nheight = 25px", 0 },

{ "Circle",
  "The biggest circle that fits its space; 100px across on its own. round, dot, ball",
  "insert circle", 0 },

{ "Oval",
  "Stretches to fill its space. ellipse, egg",
  "insert oval\r\n40px left,right", 0 },

{ "Triangle",
  "Pointing up. arrow, pyramid",
  "insert triangle\r\n20px top,right", 0 },

{ "Diamond",
  "A square on its point. rhombus",
  "insert diamond\r\n20px bottom,left", 0 },

{ "Hexagon",
  "Six sides, flat top and bottom. honeycomb, tile",
  "insert hexagon", 0 },

{ "Star",
  "Five points, one straight up. favourite, rating",
  "insert star\r\n20px top,right", 0 },

{ "Line",
  "From one corner of its space to the other; flat when only a top is given. rule, divider, separator",
  "insert line\r\n20px left,right\r\n60px top", 0 },
};

/* ---- odds and ends ----------------------------------------------------- */
static const Cheat CHEAT_ODDS[] = {
{ "<- Back to all topics",
  "Click to go back to the list of topics.",
  NULL, 0 },

{ "True, false and nothing",
  "The three fixed values. boolean, null, nil, empty",
  "[ok] = true\r\n[done] = false\r\n[result] = nothing", 0 },

{ "Print a literal brace",
  "Double it up. curly, escape",
  "print These are braces: {{ }}", 0 },
};

#define CHEAT_N(a) ((int)(sizeof(a) / sizeof((a)[0])))

static const Cheat *const CHEAT_PAGES[] = {
    CHEAT_TOPICS, CHEAT_THE_BASICS, CHEAT_MATHS, CHEAT_ASKING, CHEAT_CHOOSING, CHEAT_REPEATING, CHEAT_LISTS, CHEAT_MAPS, CHEAT_FUNCTIONS, CHEAT_WINDOWS, CHEAT_SHAPES, CHEAT_ODDS
};
static const int CHEAT_PAGE_SIZES[] = {
    CHEAT_N(CHEAT_TOPICS), CHEAT_N(CHEAT_THE_BASICS), CHEAT_N(CHEAT_MATHS), CHEAT_N(CHEAT_ASKING), CHEAT_N(CHEAT_CHOOSING), CHEAT_N(CHEAT_REPEATING), CHEAT_N(CHEAT_LISTS), CHEAT_N(CHEAT_MAPS), CHEAT_N(CHEAT_FUNCTIONS), CHEAT_N(CHEAT_WINDOWS), CHEAT_N(CHEAT_SHAPES), CHEAT_N(CHEAT_ODDS)
};
#define CHEAT_PAGE_COUNT CHEAT_N(CHEAT_PAGES)
/* room for every entry at once, which a search of all topics can need */
#define CHEAT_MAX (CHEAT_N(CHEAT_TOPICS) + CHEAT_N(CHEAT_THE_BASICS) + CHEAT_N(CHEAT_MATHS) + CHEAT_N(CHEAT_ASKING) + CHEAT_N(CHEAT_CHOOSING) + CHEAT_N(CHEAT_REPEATING) + CHEAT_N(CHEAT_LISTS) + CHEAT_N(CHEAT_MAPS) + CHEAT_N(CHEAT_FUNCTIONS) + CHEAT_N(CHEAT_WINDOWS) + CHEAT_N(CHEAT_SHAPES) + CHEAT_N(CHEAT_ODDS))

/* which page the cheat sheet is showing: 0 is the list of topics */
static int g_cheatPage;

#endif /* ADDA_GUI_CHEATSHEET_H */
