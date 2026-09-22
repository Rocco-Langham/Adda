/* Cheat sheet entries for the GUI.
 *
 * One row per thing someone might want to do, with a description of what the
 * example does and the shortest example that does it. Nothing is pasted into
 * the program: an entry opens its own page, to be read and typed out.
 *
 * The search box matches on the title, the about line and the example - not
 * the description, whose everyday words would match everything - so the
 * wording of those is what makes something findable - "repeat", "loop" and "again" should all lead
 * to the while loop.
 *
 * The first page is a list of topics; each topic is its own page, which starts
 * with a link back. A search from the topics page looks through every topic.
 *
 * Snippets use \r\n between lines; the Mac turns them into plain newlines. */
#ifndef ADDA_GUI_CHEATSHEET_H
#define ADDA_GUI_CHEATSHEET_H

typedef struct {
    const char *title;
    const char *about;     /* one line, and extra words to search on */
    const char *detail;    /* what the example does, for the entry's own page */
    const char *snippet;   /* the example; NULL: a link, to page `page` */
    int         page;
} Cheat;

/* ---- the topics page ------------------------------------------------ */
static const Cheat CHEAT_TOPICS[] = {
{ "The basics ->",
  "Printing, variables, comments",
  NULL,
  NULL, 1 },

{ "Maths ->",
  "Sums, and text that looks like maths",
  NULL,
  NULL, 2 },

{ "Asking questions ->",
  "Reading what someone types",
  NULL,
  NULL, 3 },

{ "Making choices ->",
  "if, else, and comparing things",
  NULL,
  NULL, 4 },

{ "Repeating and waiting ->",
  "Loops, and pausing with delay",
  NULL,
  NULL, 5 },

{ "Lists ->",
  "Making, changing and searching lists",
  NULL,
  NULL, 6 },

{ "Maps ->",
  "Values stored under names",
  NULL,
  NULL, 7 },

{ "Functions ->",
  "Naming a piece of code to use again",
  NULL,
  NULL, 8 },

{ "Windows ->",
  "Opening a window of your own",
  NULL,
  NULL, 9 },

{ "Shapes ->",
  "Every shape you can draw with insert",
  NULL,
  NULL, 10 },

{ "Fonts ->",
  "Five fonts on every Mac and every Windows PC",
  NULL,
  NULL, 12 },

{ "Odds and ends ->",
  "True, false, nothing, and literal braces",
  NULL,
  NULL, 11 },
};

/* ---- the basics -------------------------------------------------------- */
static const Cheat CHEAT_THE_BASICS[] = {
{ "<- Back to all topics",
  "Click to go back to the list of topics.",
  NULL,
  NULL, 0 },

{ "Show something on screen",
  "print writes the rest of the line. Text needs no quotes. say, write, output",
  "The word print shows the rest of its line on the screen, so this example prints Hello, world. The text needs no quotes around it, and commas are fine.",
  "print Hello, world", 0 },

{ "Put a value inside text",
  "Square brackets around a name mean its value. show variable, insert, interpolate",
  "[name] in square brackets means the value kept in the variable called name. If [name] holds Sam, this prints Hello, Sam. Set [name] on an earlier line first, or Adda stops and says name is not defined.",
  "print Hello, [name]", 0 },

{ "Make a variable",
  "A variable is always written in square brackets. A bare word is text, a number is a number. set, assign, store",
  "The first line makes a variable called [name] that holds the text Rocco. The second makes one called [age] that holds the number 30. Nothing is printed; the values are just remembered for later lines to use.",
  "[name] = Rocco\r\n[age] = 30", 0 },

{ "Maths inside text",
  "Braces hold a sum; brackets name the variables in it. calculate, insert",
  "The braces tell Adda to work out the sum inside them before printing. If [age] is 30, this prints Next year you will be 31. [age] has to be set on an earlier line.",
  "print Next year you will be {[age] + 1}", 0 },

{ "Copy a value",
  "Brackets on the right mean the value; a bare word would just be text. duplicate",
  "Takes whatever is kept in [count] and puts the same value into [total]. If [count] is 5, then [total] becomes 5 as well. Without the brackets on the right, [total] would just hold the word count.",
  "[total] = [count]", 0 },

{ "Write a comment",
  "Anything after # is ignored. note, remark",
  "Adda skips everything from the # to the end of the line, so this example does nothing when it runs. Comments are notes for you, or for anyone else reading your code.",
  "# this line is a note to yourself", 0 },
};

/* ---- maths ------------------------------------------------------------- */
static const Cheat CHEAT_MATHS[] = {
{ "<- Back to all topics",
  "Click to go back to the list of topics.",
  NULL,
  NULL, 0 },

{ "Do maths",
  "Operators need a space on BOTH sides. add, subtract, multiply, divide, plus",
  "Each line works out a sum and keeps the answer in a variable. If [age] is 30, [next] becomes 31, [half] becomes 15 and [left] becomes 1, because % gives what is left over when 10 is divided by 3. Every operator needs a space on both sides.",
  "[next] = [age] + 1\r\n[half] = [age] / 2\r\n[left] = 10 % 3", 0 },

{ "Text that contains a dash or a slash",
  "Without spaces it stays text, so dates and names are safe. hyphen, minus",
  "With no spaces around the dashes, Adda keeps these as plain text instead of taking numbers away. [date] holds the text 2024-01-15 and [who] holds Mary-Jane, exactly as typed. A slash with no spaces, as in 1/2, stays text too.",
  "[date] = 2024-01-15\r\n[who] = Mary-Jane", 0 },

{ "Text with an operator in it",
  "Quotes force text when it would otherwise be maths. escape, literal",
  "The quotes keep the whole title as text, so [title] holds Chapter 1 - Intro with the dash still in it. Without the quotes, a dash with a space on each side counts as a minus sign, and Adda would stop with an error.",
  "[title] = \"Chapter 1 - Intro\"", 0 },

{ "Negate a number",
  "A lone minus needs a space, so take it away from zero instead. negative",
  "Takes [x] away from 0, which flips its sign, and keeps the answer in [opposite]. If [x] is 5, [opposite] becomes -5. If [x] is -8, [opposite] becomes 8.",
  "[opposite] = 0 - [x]", 0 },
};

/* ---- asking questions -------------------------------------------------- */
static const Cheat CHEAT_ASKING[] = {
{ "<- Back to all topics",
  "Click to go back to the list of topics.",
  NULL,
  NULL, 0 },

{ "Ask a question",
  "Reads one line from whoever runs the program. input, read, prompt, entry",
  "The first line shows What is your name? and waits for you to type an answer, which it keeps in [name]. The second line prints Hello, followed by that answer, so typing Sam prints Hello, Sam.",
  "[name] = ask What is your name?\r\nprint Hello, [name]", 0 },

{ "Ask for a number",
  "An answer that looks like a number becomes one. input number",
  "It shows How old are you? and keeps your answer in [age]. An answer that looks like a number, such as 12, becomes a real number, so the braces can work out [age] + 1 and it prints Next year you will be 13. An answer in words, like twelve, stops with an error because + only adds numbers.",
  "[age] = ask How old are you?\r\nprint Next year you will be {[age] + 1}", 0 },

{ "Read a line with no question",
  "ask on its own just waits for a line. input",
  "With nothing after it, ask shows no question. It just waits for you to type a line, then keeps what you typed in [said]. Nothing is printed, so add print [said] on the next line if you want to see it.",
  "[said] = ask", 0 },
};

/* ---- making choices ---------------------------------------------------- */
static const Cheat CHEAT_CHOOSING[] = {
{ "<- Back to all topics",
  "Click to go back to the list of topics.",
  NULL,
  NULL, 0 },

{ "Do something only sometimes",
  "if runs a block when a test is true. condition, when, branch",
  "The word if checks a test, and the lines down to end only run when it is true. With [score] = 12 on a line above, this prints You win, and with 5 it prints nothing. Keep the square brackets: a bare word score is just text, so Adda would stop with an error.",
  "if [score] > 10\r\n    print You win\r\nend", 0 },

{ "Choose between two things",
  "else covers everything the if did not. otherwise",
  "If [age] is more than 17 it prints Adult, otherwise the line under else runs and it prints Child. So 18 gives Adult and 17 gives Child. [age] has to be set on a line above, for example [age] = 20.",
  "if [age] > 17\r\n    print Adult\r\nelse\r\n    print Child\r\nend", 0 },

{ "Choose between many things",
  "else if chains as many tests as you like. elif, switch, case",
  "The tests are tried from the top and only the first true one runs. With [n] = 12 it prints big, with 7 it prints middle, and with 5 or less it prints small, because else catches everything left over. [n] has to be set on a line above.",
  "if [n] > 10\r\n    print big\r\nelse if [n] > 5\r\n    print middle\r\nelse\r\n    print small\r\nend", 0 },

{ "Check if two things are the same",
  "is compares. equals, equal, same, matches",
  "The word is tests whether two things match. If [name] holds Rocco this prints Hello again, but any other name, even rocco with a small r, prints nothing. Set [name] on a line above first, for example [name] = Rocco.",
  "if [name] is Rocco\r\n    print Hello again\r\nend", 0 },

{ "Check something is NOT the same",
  "is not, or !=. different, unequal",
  "Here is not means different from. If [name] holds Sam this prints You are not Bob, and if it holds Bob it prints nothing. You can write != in place of is not, and [name] has to be set on a line above.",
  "if [name] is not Bob\r\n    print You are not Bob\r\nend", 0 },

{ "Check a range",
  "Comparisons chain the way they read. between, within",
  "Two comparisons can share one test: 1 < [x] < 10 is true only when [x] is more than 1 and less than 10, so 5 prints In range while 1 and 10 print nothing. Write the variable as [x], in square brackets. A bare x is just text, so Adda stops with an error.",
  "if 1 < [x] < 10\r\n    print In range\r\nend", 0 },

{ "Combine two tests",
  "and, or, not. both, either, negate",
  "The word and joins two tests, and both have to be true. With [age] = 20 and [name] = Rocco above it, this prints Both, but if [age] is 18 or less, or [name] is anything else, it prints nothing. Change the word and to the word or, and then only one test has to be true.",
  "if [age] > 18 and [name] is Rocco\r\n    print Both\r\nend", 0 },
};

/* ---- repeating and waiting --------------------------------------------- */
static const Cheat CHEAT_REPEATING[] = {
{ "<- Back to all topics",
  "Click to go back to the list of topics.",
  NULL,
  NULL, 0 },

{ "Repeat while something is true",
  "while runs until its test stops being true. loop, again, repeat",
  "[count] starts at 0. The while loop keeps running the lines inside it for as long as [count] is less than 5. Each time round it prints [count] and then adds 1 to it, so it prints 0, 1, 2, 3 and 4 and then stops.",
  "[count] = 0\r\nwhile [count] < 5\r\n    print [count]\r\n    [count] = [count] + 1\r\nend", 0 },

{ "Go through every item",
  "for each walks a list or a map. loop, iterate, foreach",
  "Runs the lines inside once for each item in the list [nums], with [n] holding that item each time. If [nums] is list 10, 20, 30 it prints 10, then 20, then 30. [nums] has to be made first, or Adda says it is not defined.",
  "for each [n] in [nums]\r\n    print [n]\r\nend", 0 },

{ "Wait before doing something",
  "Waits this many milliseconds (1000 is a second), then runs the lines under it. pause, sleep, wait, timer, later",
  "Waits 1500 milliseconds, which is one and a half seconds, then prints hello and hi one straight after the other. The lines between delay and delay end are the ones that wait. 1000 milliseconds is one second.",
  "delay - 1500\r\n    print hello\r\n    print hi\r\ndelay end", 0 },

{ "Go back to a line",
  "return (1) goes back to line 1 and carries on from there. goto, jump, again, repeat, loop, line",
  "Prints hello, asks for a name, then return (1) goes back to line 1, so it prints hello and asks again, forever - press Stop to end it. The number in brackets is the line number down the left of the editor. Put the return inside an if to go back only sometimes. If the lines it goes back over include openApplication, Adda warns you before running: the same window is used again, what is printed piles up in it, and it may never end.",
  "print hello\r\n[name] = ask what is name\r\nreturn (1)", 0 },
};

/* ---- lists ------------------------------------------------------------- */
static const Cheat CHEAT_LISTS[] = {
{ "<- Back to all topics",
  "Click to go back to the list of topics.",
  NULL,
  NULL, 0 },

{ "Make a list",
  "Commas separate the items. array, collection",
  "[nums] becomes a list holding three numbers: 10, 20 and 30. The word list starts it and a comma goes between each item. It prints nothing by itself, but print [nums] would then show [10, 20, 30].",
  "[nums] = list 10, 20, 30", 0 },

{ "Make an empty list",
  "list on its own. empty array",
  "Makes a list called [scores] with nothing in it yet, so its length is 0. This is handy when you want to fill the list up later, for example with add 10 to [scores].",
  "[scores] = list", 0 },

{ "Get an item out of a list",
  "Lists count from 1, not 0. index, element, position",
  "Prints the first item of the list [nums]. If [nums] is list 10, 20, 30 it prints 10. Lists count from 1, so item 1 is the first one and there is no item 0. The braces tell Adda to work the value out.",
  "print {item 1 of [nums]}", 0 },

{ "Change an item in a list",
  "Put the item on the left of the =. replace, set index",
  "Swaps the first item of [nums] for 99. If [nums] was list 10, 20, 30 it becomes 99, 20, 30. The list stays the same length; only item 1 changes.",
  "item 1 of [nums] = 99", 0 },

{ "Add to a list",
  "add ... to. append, push, insert",
  "Puts 40 on the end of the list [nums]. If [nums] was list 10, 20, 30 it now holds 10, 20, 30 and 40, so it is one item longer.",
  "add 40 to [nums]", 0 },

{ "Take something out of a list",
  "remove item N of. delete, drop",
  "Takes the second item out of [nums]. If [nums] was list 10, 20, 30 the 20 goes and the list becomes 10, 30. The items after it move up, so 30 is now item 2.",
  "remove item 2 of [nums]", 0 },

{ "How many are there?",
  "length of works on lists, maps and text. size, count, len",
  "Prints how many items are in [nums]. If [nums] is list 10, 20, 30 it prints 3. length of also works on a map, and on text held in a variable: with [t] = hi there, length of [t] is 8, because the space counts too.",
  "print {length of [nums]}", 0 },

{ "Is it in there?",
  "has ... of asks. contains, includes, member",
  "Asks whether 40 is one of the items in [nums] and prints true or false. If [nums] is list 10, 20, 30 it prints false, because 40 is not in it. After add 40 to [nums] it would print true.",
  "print {has 40 of [nums]}", 0 },
};

/* ---- maps -------------------------------------------------------------- */
static const Cheat CHEAT_MAPS[] = {
{ "<- Back to all topics",
  "Click to go back to the list of topics.",
  NULL,
  NULL, 0 },

{ "Make a map",
  "A map holds values under names. dictionary, object, record, lookup",
  "This makes an empty map and keeps it in the variable [person]. A map holds values under names, a bit like a form with labelled boxes, and it starts with nothing in it. Running this line on its own prints nothing.",
  "[person] = map", 0 },

{ "Put something in a map",
  "The key is the word you write. set key, store",
  "The first line stores the word Rocco under the key name in the map [person], and the second stores the number 30 under the key age. The map has to exist first, so [person] = map must come before these lines. Nothing is printed.",
  "name of [person] = Rocco\r\nage of [person] = 30", 0 },

{ "Read something out of a map",
  "key of map. get, lookup",
  "name of [person] looks up the value stored under the key name in the map [person], and the braces work it out inside the print. If name was set to Rocco, this prints Rocco. Asking for a key that is not in the map is an error.",
  "print {name of [person]}", 0 },

{ "Use a key held in a variable",
  "item takes the key as a value rather than a word. dynamic key",
  "The first line puts the word name into the variable [k]. item [k] of [person] then looks up whichever key [k] holds, so here it does the same job as name of [person]. If name was set to Rocco, this prints Rocco.",
  "[k] = name\r\nprint {item [k] of [person]}", 0 },

{ "Check a map has a key",
  "has with quotes around a literal key. contains",
  "This asks whether the map [person] has anything stored under the key age, which is written as text inside quotes. It prints true if the key is there and false if it is not.",
  "print {has \"age\" of [person]}", 0 },

{ "Go through a map",
  "Keys come back in the order you added them. iterate, keys",
  "The loop runs once for every key in the map [person], putting each key into [key] in turn, and item [key] of [person] fetches the value kept under it. With name set to Rocco and age set to 30, it prints name is Rocco and then age is 30.",
  "for each [key] in [person]\r\n    print [key] is {item [key] of [person]}\r\nend", 0 },
};

/* ---- functions --------------------------------------------------------- */
static const Cheat CHEAT_FUNCTIONS[] = {
{ "<- Back to all topics",
  "Click to go back to the list of topics.",
  NULL,
  NULL, 0 },

{ "Make a function",
  "define ... with names the inputs, each in square brackets. procedure, method, subroutine",
  "This makes a function called greet that takes one input, [person]. The line inside runs every time greet is called, printing Hello followed by whatever was handed in. Making the function prints nothing by itself, and end marks where it finishes.",
  "define greet with [person]\r\n    print Hello [person]\r\nend", 0 },

{ "Use a function",
  "call it by name. invoke, run",
  "This runs the function greet and hands it the word Rocco as its input. If greet is the function that does print Hello [person], this prints Hello Rocco. The function must be defined somewhere in the file, or Adda says there is no function called greet.",
  "call greet with Rocco", 0 },

{ "Give an answer back",
  "return hands a value to whoever called. result, output",
  "add_up takes two inputs, [a] and [b], and return sends their total back to whoever called it. The last line calls add_up with 2 and 3 inside braces, so the answer is worked out and the example prints 5.",
  "define add_up with [a], [b]\r\n    return [a] + [b]\r\nend\r\n\r\nprint {call add_up with 2, 3}", 0 },

{ "A function that calls itself",
  "Recursion works, and functions can be defined below where they are used.",
  "factorial multiplies a number by every whole number below it, down to 1. If [n] is 1 or less it returns 1, otherwise it returns [n] times the factorial of [n] - 1 by calling itself. Nothing prints until you use it: print {call factorial with 5} prints 120.",
  "define factorial with [n]\r\n    if [n] <= 1\r\n        return 1\r\n    end\r\n    return [n] * call factorial with [n] - 1\r\nend", 0 },
};

/* ---- windows ----------------------------------------------------------- */
static const Cheat CHEAT_WINDOWS[] = {
{ "<- Back to all topics",
  "Click to go back to the list of topics.",
  NULL,
  NULL, 0 },

{ "Open a blank window",
  "Opens a window with this title. Everything printed after it appears in the middle of the window. app, application, window, gui, screen",
  "The first line opens a new window with the title My App. After that, print stops writing to the console, so Hello from my app appears in the middle of the window instead. The window stays open until you close it.",
  "openApplication My App\r\nprint Hello from my app", 0 },
};

/* ---- shapes ------------------------------------------------------------ */
static const Cheat CHEAT_SHAPES[] = {
{ "<- Back to all topics",
  "Click to go back to the list of topics.",
  NULL,
  NULL, 0 },

{ "Box",
  "Square corners. Each 20px line says how far it stops from those edges. rectangle, square",
  "Draws a box with square corners, 20px in from the top and left edges of your app window. No right or bottom is given, so it gets the default size of 200px wide and 100px tall. It only works after an openApplication line.",
  "insert box\r\n20px top,left", 0 },

{ "Rounded box",
  "A box with soft corners. panel, card, rectangle",
  "Draws a box with soft corners that stops 3px from the top, right and left edges of your app window, so it runs across the top and stretches with the window. No bottom is given, so it is 100px tall. It only works after an openApplication line.",
  "insert rounded box\r\n3px top,right,left", 0 },

{ "Pill",
  "A box whose ends are fully round. capsule, button, lozenge",
  "Draws a pill, a box with fully round ends, near the bottom of your app window. It stops 20px from the left and right edges and 40px above the bottom edge, and is 100px tall because no top is given. It only works after an openApplication line.",
  "insert pill\r\n20px left,right\r\n40px bottom", 0 },

{ "Set a shape's height or width",
  "A size line after the distances. Size wins; the edges say where it sits. tall, wide, size, strip, bar",
  "After an openApplication line, this draws a box 15px in from the top, left and right edges, and height = 25px makes it exactly 25px tall. The result is a thin strip across the top of your app window. A line like width = 300px sets how wide a shape is in the same way.",
  "insert box\r\n15px top,left,right\r\nheight = 25px", 0 },

{ "Name a shape and write in it",
  "A name after ; makes a block that ends with End; text lines write in it. label, caption, words, font, writing",
  "The first line draws a rounded box called [roundedbox1] - the name is just its label. The next two place it and set its height. The Text line writes Hello in the middle of it, in Helvetica at size 15. A named shape always needs End.",
  "insert rounded box; name = [roundedbox1]\r\n15px top, right, left\r\nHeight = 15px\r\nText [roundedbox1] Hello; font helvetica; size15; location centre\r\nEnd", 0 },

{ "Put text in a corner of a shape",
  "location says where: left, centre, right, top, bottom, or two together. place, position, align, corner",
  "Draws a named box 40px in from every edge, then writes two lines in it: Welcome in its top left corner, and Page 1 in small writing in its bottom right corner.",
  "insert box; name = [panel]\r\n40px top, bottom, left, right\r\ntext [panel] Welcome; location top left\r\ntext [panel] Page 1; size 11; location bottom right\r\nend", 0 },

{ "Write in a shape later on",
  "A text line on its own writes in a shape drawn earlier. change, update, add words, later",
  "Draws a circle called [dot] and ends its block straight away. Later, the text line writes Score: followed by the value of [score] inside it, in Courier New at size 18.",
  "insert circle; name = [dot]\r\nend\r\n[score] = 10\r\ntext [dot] Score: [score]; font courier new; size 18", 0 },

{ "Make a box to type in",
  "function - input box lets the person type into a shape. input, type, text box, textbox, field, entry, enter",
  "Draws a rounded box called [input] and makes it a place to type. hello shows faintly in it until they start typing. The program waits at End until they type something and press Enter; then [input] holds what they typed, so the last line prints You said and their words. It needs openApplication first, like every shape.",
  "openApplication\r\ninsert rounded box; name = [input]\r\nprint hello\r\nfunction - input box\r\nend\r\nprint You said [input]", 0 },

{ "Ask a question in a shape",
  "An ask line inside a shape writes the question in it, and the answer is typed there. ask, question, input, type, answer",
  "Draws a box 100px from the top and 15px from the left and right. The question What is your name? is written at the top of the box, and the person types their answer under it and presses Enter. The program waits at End until they do; then [name] holds the answer, so the last line prints Hello and their name. It needs openApplication first.",
  "openApplication\r\ninsert box; name = [box1]\r\n15px right,left\r\n100px top\r\n[name] = ask What is your name?\r\nend\r\nprint Hello [name]", 0 },

{ "Circle",
  "The biggest circle that fits its space; 100px across on its own. round, dot, ball",
  "Draws a circle in your app window. No distance lines are given, so it is 100px across and sits right in the centre of the window. It only works after an openApplication line.",
  "insert circle", 0 },

{ "Oval",
  "Stretches to fill its space. ellipse, egg",
  "Draws an oval that stops 40px from the left and right edges of your app window, so it gets wider when the window does. No top or bottom is given, so it is 100px tall and centred up and down. It only works after an openApplication line.",
  "insert oval\r\n40px left,right", 0 },

{ "Triangle",
  "Pointing up. arrow, pyramid",
  "Draws a triangle pointing up, 20px in from the top and right edges, so it sits in the top right corner of your app window. No size is given, so it is 100px wide and 100px tall. It only works after an openApplication line.",
  "insert triangle\r\n20px top,right", 0 },

{ "Diamond",
  "A square on its point. rhombus",
  "Draws a diamond, a square standing on its point, 20px in from the bottom and left edges, so it sits in the bottom left corner of your app window. No size is given, so it is 100px wide and 100px tall. It only works after an openApplication line.",
  "insert diamond\r\n20px bottom,left", 0 },

{ "Hexagon",
  "Six sides, flat top and bottom. honeycomb, tile",
  "Draws a six-sided shape with a flat top and bottom in your app window. No distance lines are given, so it is 100px wide and sits right in the centre of the window. It only works after an openApplication line.",
  "insert hexagon", 0 },

{ "Star",
  "Five points, one straight up. favourite, rating",
  "Draws a five-pointed star with one point straight up. Its space is 100px wide and 100px tall and starts 20px in from the top and right edges, so the star sits in the top right corner of your app window. It only works after an openApplication line.",
  "insert star\r\n20px top,right", 0 },

{ "Line",
  "From one corner of its space to the other; flat when only a top is given. rule, divider, separator",
  "Draws a flat line across your app window, 60px down from the top edge and stopping 20px from the left and right edges. It is flat because it has a top distance but no bottom one; add 60px bottom as well and it slants from the top left of its space to the bottom right. It only works after an openApplication line.",
  "insert line\r\n20px left,right\r\n60px top", 0 },
};

/* ---- odds and ends ----------------------------------------------------- */
static const Cheat CHEAT_ODDS[] = {
{ "<- Back to all topics",
  "Click to go back to the list of topics.",
  NULL,
  NULL, 0 },

{ "True, false and nothing",
  "The three fixed values. boolean, null, nil, empty",
  "Stores Adda's three fixed values in variables: [ok] becomes true, [done] becomes false, and [result] becomes nothing, which means no value at all. It shows no output by itself, but you can use them in tests later, such as if [ok] or if [result] is nothing.",
  "[ok] = true\r\n[done] = false\r\n[result] = nothing", 0 },

{ "Print a literal brace",
  "Double it up. curly, escape",
  "Braces normally mean work this out, so a single { on its own gives an error. Typing each brace twice tells Adda you want the real character, so this prints These are braces: { } with just one of each.",
  "print These are braces: {{ }}", 0 },
};

/* ---- fonts ---------------------------------------------------------- */
/* five fonts that come with every Mac and every Windows PC, so a program
 * looks the same on both */
static const Cheat CHEAT_FONTS[] = {
{ "<- Back to all topics",
  "Click to go back to the list of topics.",
  NULL,
  NULL, 0 },

{ "Arial",
  "Plain and clear, with no little feet on the letters. sans serif, simple, normal, clean",
  "Draws a rounded box across the top of the window and writes Hello there in it, in Arial at size 24. Arial is plain and easy to read, good for everyday writing.",
  "insert rounded box; name = [sign]\r\n40px top, left, right\r\nheight = 80px\r\ntext [sign] Hello there; font arial; size 24\r\nend", 0 },

{ "Georgia",
  "Classic, like a book or a newspaper. serif, book, fancy, old style, times",
  "Draws a rounded box across the top of the window and writes Once upon a time in it, in Georgia at size 24. Georgia has small feet on its letters, like the writing in a book.",
  "insert rounded box; name = [sign]\r\n40px top, left, right\r\nheight = 80px\r\ntext [sign] Once upon a time; font georgia; size 24\r\nend", 0 },

{ "Courier New",
  "Every letter the same width, like a typewriter or code. monospace, typewriter, code, computer, fixed",
  "Draws a rounded box across the top of the window and writes Score: 100 in it, in Courier New at size 24. Every letter takes up the same space, so it looks like a typewriter or computer code.",
  "insert rounded box; name = [sign]\r\n40px top, left, right\r\nheight = 80px\r\ntext [sign] Score: 100; font courier new; size 24\r\nend", 0 },

{ "Comic Sans MS",
  "Friendly, like handwriting in a comic. handwriting, fun, playful, childish, cartoon",
  "Draws a rounded box across the top of the window and writes Have fun! in it, in Comic Sans MS at size 24. It looks like friendly handwriting, good for fun or playful programs.",
  "insert rounded box; name = [sign]\r\n40px top, left, right\r\nheight = 80px\r\ntext [sign] Have fun!; font comic sans ms; size 24\r\nend", 0 },

{ "Impact",
  "Big, bold and squashed - made for headlines. bold, heavy, headline, title, poster, loud",
  "Draws a rounded box across the top of the window and writes GAME OVER in it, in Impact at size 32. Impact is thick and tall, so it stands out, like a poster or a headline.",
  "insert rounded box; name = [sign]\r\n40px top, left, right\r\nheight = 80px\r\ntext [sign] GAME OVER; font impact; size 32\r\nend", 0 },
};

#define CHEAT_N(a) ((int)(sizeof(a) / sizeof((a)[0])))

static const Cheat *const CHEAT_PAGES[] = {
    CHEAT_TOPICS, CHEAT_THE_BASICS, CHEAT_MATHS, CHEAT_ASKING, CHEAT_CHOOSING, CHEAT_REPEATING, CHEAT_LISTS, CHEAT_MAPS, CHEAT_FUNCTIONS, CHEAT_WINDOWS, CHEAT_SHAPES, CHEAT_ODDS,
    CHEAT_FONTS
};
static const int CHEAT_PAGE_SIZES[] = {
    CHEAT_N(CHEAT_TOPICS), CHEAT_N(CHEAT_THE_BASICS), CHEAT_N(CHEAT_MATHS), CHEAT_N(CHEAT_ASKING), CHEAT_N(CHEAT_CHOOSING), CHEAT_N(CHEAT_REPEATING), CHEAT_N(CHEAT_LISTS), CHEAT_N(CHEAT_MAPS), CHEAT_N(CHEAT_FUNCTIONS), CHEAT_N(CHEAT_WINDOWS), CHEAT_N(CHEAT_SHAPES), CHEAT_N(CHEAT_ODDS),
    CHEAT_N(CHEAT_FONTS)
};
#define CHEAT_PAGE_COUNT CHEAT_N(CHEAT_PAGES)
/* room for every entry at once, which a search of all topics can need */
#define CHEAT_MAX (CHEAT_N(CHEAT_TOPICS) + CHEAT_N(CHEAT_THE_BASICS) + CHEAT_N(CHEAT_MATHS) + CHEAT_N(CHEAT_ASKING) + CHEAT_N(CHEAT_CHOOSING) + CHEAT_N(CHEAT_REPEATING) + CHEAT_N(CHEAT_LISTS) + CHEAT_N(CHEAT_MAPS) + CHEAT_N(CHEAT_FUNCTIONS) + CHEAT_N(CHEAT_WINDOWS) + CHEAT_N(CHEAT_SHAPES) + CHEAT_N(CHEAT_ODDS) + CHEAT_N(CHEAT_FONTS))

/* which page the cheat sheet is showing: 0 is the list of topics */
static int g_cheatPage;

#endif /* ADDA_GUI_CHEATSHEET_H */
