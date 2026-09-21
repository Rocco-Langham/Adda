# The Adda language

A walkthrough of everything Adda can do. Every example here is real, runnable
code — try them with `./adda yourfile.adda`.

---

## 1. Printing

```adda
print Hello, world
```

`print` takes the rest of the line. No quotes, no brackets.

```adda
print I can write commas, apostrophes and 50% off
```

## 2. Variables

```adda
[name] = Rocco
[age] = 30
```

A variable's name always goes in **square brackets**: `[name]`. That is how
you set it, and how you use it anywhere else.

A bare word - one without brackets - is always just text. A number on its own
is a number.

To put a value inside text, write its name in brackets:

```adda
print Hello [name], you are [age]
```

Square brackets mean **the variable called**. Brackets only count when there is
a single name between them, so `print [1, 2]` or `print [not a name]` prints
the brackets as text.

For a sum inside text, use braces around it, with the variables still in
brackets inside:

```adda
print Next year you will be {[age] + 1}
```

Braces mean **work this out**. The old way of writing a variable, `{name}` on
its own, is now an error that tells you to write `[name]`.

The name after `for each`, and a function's inputs, go in brackets too:

```adda
for each [n] in list 1, 2, 3
    print [n]
end

define greet with [who]
    print Hi [who]
end
```

Function names (`greet`) and map keys (`name of [person]`) are not variables,
so they stay bare.

## 3. Maths

An operator counts only when it has a space on **both** sides:

```adda
[next] = [age] + 1        # 31
[half] = [age] / 2        # 15
[left] = 10 % 3           # 1
```

Without the spaces, it is just part of a word:

```adda
[date] = 2024-01-15       # text, not maths
[who]  = Mary-Jane        # text
[code] = 007              # text, and the zeros survive
```

This is deliberate. It is what lets you type dates, names and percentages
without thinking about it.

If text needs an operator inside it, use quotes:

```adda
[title] = "Chapter 1 - Intro"
```

### Copying a value

Brackets on the right-hand side mean the variable's value:

```adda
[count] = 5
[copy] = [count]          # 5
```

Without them it is just the word:

```adda
[word] = count            # the word count, even though [count] exists
```

## 4. Asking a question

`ask` reads a line from whoever is running the program. The question needs no
quotes, same as `print`:

```adda
[name] = ask What is your name?
print Hello, [name]
```

The answer becomes a **number** when it looks like one, under exactly the same
rule the language uses for numbers you type in your source:

```adda
[age] = ask How old are you?
print Next year you will be {[age] + 1}
```

So `30` is a number you can do maths with, while `007` stays as text and keeps
its zeros — just as `[code] = 007` would in a program.

Spaces at either end of the answer are trimmed, and the question can contain
values like any other text:

```adda
[count] = 1
[thing] = ask Thing [count]?
```

Leave the question out to read a line with no prompt at all:

```adda
print Type something
[said] = ask
```

If the input runs out — the file being piped in ends, or nothing is connected —
`ask` gives back `nothing`, which you can check for:

```adda
[answer] = ask Anything else?
if [answer] is nothing
    print There was nothing left to read
end
```

## 5. True, false and nothing

```adda
[ok] = true
[done] = false
[result] = nothing
```

## 6. Making decisions

```adda
if [age] > 18
    print Adult
else if [age] > 12
    print Teenager
else
    print Child
end
```

Blocks end with `end`. Indentation is ignored, so you cannot break a program by
spacing it differently.

Comparisons: `is`, `is not`, `<`, `>`, `<=`, `>=`, and `!=`.

```adda
if [name] is Rocco
    print Hello again
end

if [name] is not Bob
    print You are not Bob
end
```

They chain the way they read:

```adda
if 1 < [x] < 10
    print In range
end
```

Combine tests with `and`, `or`, `not`:

```adda
if [age] > 18 and [name] is Rocco
    print Both
end

if not [done]
    print Still going
end
```

A test has to come out true or false. `if [count]` is an error — write
`if [count] > 0`.

## 7. Repeating

```adda
[count] = 0
while [count] < 5
    print count is [count]
    [count] = [count] + 1
end
```

## 8. Functions

```adda
define greet with [person]
    print Hello [person]
end

call greet with Rocco
```

Use `return` to send a value back, and `call` inside braces to use it:

```adda
define add_up with [a], [b]
    return [a] + [b]
end

print {call add_up with 2, 3}
```

Functions can call themselves:

```adda
define factorial with [n]
    if [n] <= 1
        return 1
    end
    return [n] * call factorial with [n] - 1
end
```

A function can be called before the line that defines it — Adda reads them all
first. Functions live on their own, at the outer level of a file; you cannot
define one inside an `if` or another function.

## 9. Lists

```adda
[nums] = list 10, 20, 30
```

Lists count from **1**:

```adda
print {item 1 of [nums]}      # 10
item 1 of [nums] = 99
```

Each thing between the commas follows the usual rule, so this is a list of
three words:

```adda
[friends] = list Rocco, Mary, Sam
```

Growing, shrinking and measuring:

```adda
add 40 to [nums]
remove item 2 of [nums]
print {length of [nums]}
print {has 40 of [nums]}
```

Going through one:

```adda
for each [n] in [nums]
    print [n]
end
```

An empty list to start with:

```adda
[scores] = list
add 10 to [scores]
```

## 10. Maps

A map holds values under names.

```adda
[person] = map
name of [person] = Rocco
age of [person] = 30

print {name of [person]}
```

`name of [person]` uses `name` as the key, exactly as written. When the key is
in a variable, use `item`:

```adda
[k] = name
print {item [k] of [person]}
```

That is also how you check and remove:

```adda
print {has "age" of [person]}
remove age of [person]
print {length of [person]}
```

Maps keep keys in the order you added them, so going through one is
predictable:

```adda
for each [key] in [person]
    print [key] is {item [key] of [person]}
end
```

## 11. When something is wrong

Adda points at the line and says what it expected:

```
examples/broken.adda:4: 'totl' is not defined - did you mean 'total'?
     4 | print [totl]
       | ^
```

Common ones:

| Message | Usually means |
|---|---|
| `'x' is not defined` | A typo, or the variable is set further down |
| `this test gave text, but a test has to be true or false` | Missing spaces: `[x]>5` should be `[x] > 5` |
| `there is no item 0 - Adda counts from 1` | Lists start at 1 |
| `this 'if' is never closed` | A missing `end` |
| `'item' is a special word in Adda` | Use quotes to write it as text |
| `'+' adds numbers, and one of these is text` | A variable without its brackets, or text: join text with `[a] [b]` |
| `to set a variable, put its name in square brackets` | Write `[name] = ...`, not `name = ...` |
| `to use a variable, write [name]` | `{name}` is the old way; write `[name]` |

## 12. Trying things out

Run `adda` with no file and you can type code straight at it. This is the
quickest way to find out what something does.

```
$ ./adda
Adda - type :help for help, :quit to leave.
adda> 2 + 2
4
```

A line on its own shows you its value, so you do not need `print` while you are
poking around:

```
adda> [name] = Rocco
adda> [name]
Rocco
adda> length of [name]
5
```

Blocks work exactly as they do in a file — the prompt changes to `...` and
keeps reading until your `end` arrives:

```
adda> [count] = 0
adda> while [count] < 3
  ...     print counting [count]
  ...     [count] = [count] + 1
  ... end
counting 0
counting 1
counting 2
```

Functions stay defined for the rest of the session, so you can build something
up in pieces and try it as you go:

```
adda> define double with [n]
  ...     return [n] * 2
  ... end
adda> call double with 21
42
```

Getting something wrong is not fatal. Adda says what went wrong and waits for
the next line:

```
adda> print [tota]
typed:9: 'tota' is not defined - did you mean 'total'?
       9 | print [tota]
         | ^
adda> 
```

Three commands are not Adda code but instructions to the prompt itself:

| Command | What it does |
|---|---|
| `:vars` | Lists every variable and function you have made |
| `:help` | A short reminder |
| `:quit` | Leaves. Ctrl-D does the same thing |

## 13. The window

`adda-gui` runs your program through the same `adda` you use from the command
line, in a window with a code box above and a console below.

**Run** (or Ctrl+Enter) starts it. Output appears as it happens rather than all
at the end, so when your program reaches an `ask`, the question shows up and
waits — and you answer it **in the console itself**, right where the question
is:

```
What is your name? Rocco
Hello, Rocco
```

Everything already printed is fixed; only the line you are typing can be
changed, and Enter sends it. **Esc** clears a line you have half typed.
**Stop** ends a program that is waiting for input it is never going to get, or
one stuck in a loop.

Drag the divider between the code and the console to give either one more room,
or double-click it to split them evenly. **F11** goes full screen.

Down the left side:

| Icon | What it does |
|---|---|
| Explorer | Lists the `.adda` files beside the program and in `examples/`. Click one to open it. |
| Search | Finds text in your code; Enter jumps to the next match. |
| New Project | Asks where to save a new project and what to call it, then makes that folder with `first.adda` and `style.adda` in it and shows it in the Explorer. |
| Cheat sheet | Everything in this guide as searchable one-liners. Double-click one to put the code into your program. |
| Settings | **Themes**: Follow Windows, Light, Dark, Beige, or Abyss. |

Clicking the Explorer or Search icon you are already on closes the panel again.
Whichever theme you pick is remembered for next time.

On a Mac the window is `Adda.app` and works the same way. The keys are the
Mac's own: **⌘R** or **⌘Return** runs, **⌘.** stops, Return answers an `ask`,
**⌃⌘F** (or the green button) goes full screen, and Settings offers
**Follow macOS** in place of Follow Windows. In the Explorer, Return renames a
file and Delete moves it to the Trash. The Explorer looks in the folder
`Adda.app` sits in and in the `examples/` folder next to it.

## 14. Full list of words Adda reserves

`print` `if` `else` `while` `for` `each` `in` `define` `with` `return` `call`
`add` `to` `remove` `end` `list` `map` `item` `length` `has` `ask` `of`
`is` `not` `and` `or` `true` `false` `nothing`

`list`, `map`, `item`, `length`, `call`, `has` and `ask` cannot be used as
variable names, because they start a value. The rest are fine as ordinary words in text.

## 14. Things to watch out for

- `[range] = 1 - 10` really is maths. Write `1-10` or `"1 - 10"`.
- `#` starts a comment wherever a word could start, so
  `[note] = press # for menu` loses the end. Quote it.
- Forgetting the brackets is not an error, because a bare word is text:
  `if [total] is totl` compares against the word `totl`. A misspelt name
  *inside* brackets is an error, and Adda suggests the nearest name.
- Adda frees memory only when the program ends, so a loop that keeps building
  new text will keep growing.
