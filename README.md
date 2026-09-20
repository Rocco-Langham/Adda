# Adda

A very small programming language with as little punctuation as I could get away with.

```adda
name = Rocco
age = 30

print Hello {name}, you are {age}

if age > 18
    print You are an adult
end
```

No quotes around ordinary text. No semicolons. No brackets around conditions.
Blocks close with `end`, and indentation is yours to do as you like — Adda
ignores it.

Adda is a tree-walking interpreter written in C99, in about 2,600 lines.

## The one rule

Letting text go unquoted creates a real question: in `x = 2 + 3`, is that the
number `5` or the text `"2 + 3"`? Adda answers it the same way every time:

> **Operators mean maths. Braces mean "the value of".**

```adda
name = Rocco            # no operator, so this is text
age  = 30               # a number on its own
next = age + 1          # spaced operator, so this is maths -> 31
print Hello {name}      # braces pull a value into text
```

An operator only counts when it has **a space on both sides**. That is what
keeps ordinary writing intact:

```adda
date  = 2024-01-15      # one word, so text  (not 2024 minus 1 minus 15)
who   = Mary-Jane       # text
off   = 50% off         # text
code  = 007             # text, and it keeps its leading zeros
maths = 5 - 3           # spaced, so this really is 2
```

If a piece of text has to contain an operator, quote it:

```adda
title = "Chapter 1 - Intro"
```

Reading input works the same way — no quotes around the question:

```adda
name = ask What is your name?
age  = ask How old are you?
print Next year you will be {age + 1}
```

An answer becomes a number under exactly the rule source literals use, so `30`
is something you can add to, while `007` keeps its zeros.

A bare word that happens to name a variable means that variable, so copying
values reads the way you would expect:

```adda
total = count           # the value of count
name  = Rocco           # just the word, since nothing is called Rocco
word  = "count"         # the word, even though count exists
```

## Build and run

You need a C compiler. On Windows, MSYS2's MinGW gcc works:

```bash
winget install -e --id MSYS2.MSYS2
```

Then, from the repository:

```bash
./build.sh
```

That produces `./adda`. Run a program with:

```bash
./adda examples/hello.adda
```

Other flags: `--tokens` shows how the lexer split your source, and `--stats`
reports how much memory the run used.

## Typing at it

Run `./adda` with no file and you get a prompt. A line on its own shows you its
value, and a block keeps reading until you type `end`:

```
$ ./adda
Adda - type :help for help, :quit to leave.
adda> 2 + 2
4
adda> name = Rocco
adda> name
Rocco
adda> if 2 > 1
  ...     print yes, {name}
  ... end
yes, Rocco
```

Anything you define stays defined, so you can build up a program a piece at a
time. A mistake reports itself and the session carries on:

```
adda> print {totl}
typed:7: 'totl' is not defined - did you mean 'total'?
       7 | print {totl}
         | ^
adda> print {total}
10
```

| Command | What it does |
|---|---|
| `:vars` | Show everything you have defined |
| `:help` | A short reminder of the above |
| `:quit` | Leave — Ctrl-D does the same |

## The window

`./build.sh` also produces `adda-gui`, a small window with a code box, an
output box and an input line. Put it next to `adda.exe` and double-click it.

Press **Run** (or Ctrl+Enter) and the program starts in the background while
output appears as it happens. When it reaches an `ask`, type the answer into
the input line and press Enter:

```
Code:     name = ask What is your name?
          print Hello, {name}

Output:   What is your name? Rocco
          Hello, Rocco

Input:    [                    ] [Send]
```

**Stop** ends a program that is waiting or looping. The input line is only
active while something is running. **F11** goes full screen.

The cog in the top right picks the look — **Follow Windows**, **Light**,
**Dark** or the original **Beige** — and remembers it between sessions. On
Follow Windows it switches with the system as you change it.

## The whole language

```adda
# comments start with #

name = Rocco                    # text
age = 30                        # number
ok = true                       # true / false / nothing
next = age + 1                  # + - * / %

print Hello {name}              # print takes the same rule as the right of '='
who = ask What is your name?    # reads a line from whoever runs the program

if age > 18                     # is, is not, < > <= >=, and, or, not
    print Adult
else if age > 12
    print Teenager
else
    print Child
end

while age < 40
    age = age + 1
end

define greet with person        # functions live at the top level
    print Hello {person}
    return true
end

call greet with Rocco

nums = list 10, 20, 30          # lists count from 1
print {item 1 of nums}
item 1 of nums = 99
add 40 to nums
remove item 2 of nums
print {length of nums}
print {has 40 of nums}

for each n in nums
    print {n}
end

person = map                    # maps keep the order you add keys in
name of person = Rocco
age of person = 30
print {name of person}
print {has "age" of person}

for each key in person
    print {key} is {item key of person}
end
```

There is a fuller walkthrough in [docs/adda.md](docs/adda.md), and runnable
programs in [examples/](examples/).

## Tests

```bash
./tests/run.sh
```

Every test runs a `.adda` file and compares stdout, stderr and the exit code
against a `.expected` file. `./tests/run.sh --accept` rewrites those files after
an intentional change. `tests/errors/` does the same for error messages, so a
diagnostic cannot quietly get worse.

## Sharp edges

These are deliberate, and each has a way around it:

| What | Why | What to write instead |
|---|---|---|
| `range = 1 - 10` is maths | The spaces make it maths | `1-10`, or `"1 - 10"` |
| `if x>5` is an error | No spaces, so it is one word | `if x > 5` |
| `length`, `item`, `list`, `map`, `call`, `has` cannot be variable names | They start a value | `total_length`, `first_item` |
| `note = press # for menu` loses the tail | `#` starts a comment | `note = "press # for menu"` |
| `x = -y` is text | `-` has no space after it | `x = 0 - y` |
| A misspelled word in a comparison becomes text | Bare words fall back to text | Adda suggests the nearest name when it can |

Memory grows in a long loop that keeps building text: Adda allocates from an
arena and frees it all at exit, with no collector yet. Fine for scripts,
something to fix before writing a game loop.

## Layout

| Path | What is in it |
|---|---|
| `src/adda.h` | Every shared type: values, objects, tokens, AST nodes |
| `src/lexer.c` | Tokens, and the space-around-operators rule |
| `src/parser.c` | The text-vs-maths decision, and the whole grammar |
| `src/interp.c` | Scopes, evaluation, control flow |
| `src/repl.c` | The interactive prompt: continuation lines, error recovery |
| `src/value.c`, `src/map.c` | Text, lists, and the ordered hash map |
| `src/error.c` | Messages with a line, the source line and a caret |
| `src/arena.c` | The bump allocator |
