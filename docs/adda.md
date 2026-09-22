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

### Waiting

`delay` waits, then runs the lines under it. The time is in milliseconds, so
1000 is one second:

```adda
print Get ready
delay - 1500
    print Go!
    print Run!
delay end
print This comes straight after Run!
```

The wait starts from wherever the program has got to. Anything printed before
it has already appeared, so this is how you pace output. The dash is optional
(`delay 1500`), the time can be a variable (`delay - [wait]`), a plain `end`
closes the block as well as `delay end`, and one delay can sit inside another.

### Going back to a line

`return (3)` goes back to line 3 and carries on from there. The number is the
line number shown down the left of the editor:

```adda
print hello
[name] = ask what is your name?
return (1)
```

That asks again and again, forever - press Stop to end it. Put the `return`
inside an `if` to go back only sometimes:

```adda
[count] = 0
[count] = [count] + 1
print round {[count]}
if [count] < 3
    return (2)
end
print done
```

A `return` can also jump forward, to skip lines. It jumps straight out of any
`if`, `while` or `for each` it is inside, but it can only land on a line that
is not inside a block. Going back to a blank line or a comment carries on
from the next line after it.

Inside a function `return` still hands back a value, so `return (1)` in a
`define` gives back 1. Outside one, write the number in brackets; plain
`return 3` asks you to.

**Going back over `openApplication`.** If the lines a `return` goes back over
include `openApplication`, Adda warns you before it runs and asks whether to
run anyway. This is what goes wrong:

- `openApplication` does not open another window each time round - the same
  window is used again.
- Everything printed or drawn keeps piling up in that window, on top of what
  is already there.
- If the `return` is not inside an `if`, nothing stops it, so the program never
  ends by itself - you have to press Stop or close the window.

The editor asks in a pop-up. Run from a terminal, `adda` asks
`Run it anyway? (y/n)`, and `adda --warnings file` just lists the warnings.

### Opening a window

`openApplication` opens a new window - the start of an app of your own.
Whatever follows it on the line is the window's title. From then on,
everything you `print` appears in the dead centre of the window instead of the
console, with the lines stacked as one centred block:

```adda
print This one goes to the console
openApplication My Game
print Hello
print Welcome to my game
delay - 1000
    print (a second later)
delay end
```

The program keeps running while the window is open, and `delay` keeps it
responding. When the program reaches its end the window stays up until you
close it; closing it earlier stops the program, as closing an app does.
Questions from `ask` still appear in the console.

#### Shapes

`insert` draws a shape in the window, behind the text. The lines straight
after it say how far each edge of the shape stops from that edge of the
window:

```adda
openApplication My App
insert rounded box
3px top,right,left
```

That box runs across the top, 3px in from the top, right and left edges.
Use as many distance lines as you like (`20px bottom`), in any order. An edge
you leave out gets a default size - 200px wide or 100px tall, measured from
the side you did give - and a box with neither of a pair is centred that way.
Because shapes are measured from the edges, they stretch when the window is
resized.

To set a size exactly, add a `height` or `width` line:

```adda
insert box
15px top,left,right
height = 25px
```

That is a strip 25px tall, 15px in from the top, left and right. A given size
always wins; the shape is placed from whichever side has a distance (the top
here), or centred when neither does. `width = 300px` works the same way across.

#### Text in a shape

Give a shape a name after a semicolon, and it becomes a block that ends with
`End`. Inside it, a `text` line writes in the shape:

```adda
insert rounded box; name = [roundedbox1]
15px top, right, left
Height = 15px
Text [roundedbox1] Hello; font helvetica; size15; location centre
End
```

The name goes in square brackets, like a variable - but it is only the shape's
label. After it come the words, then, each after a semicolon and all of them
optional:

| Setting | What it does |
|---|---|
| `font helvetica` | Any font on the computer, in any capitals. Leave it out for the usual one. Five that every Mac and Windows PC has, so they look the same everywhere: `arial` (plain), `georgia` (like a book), `courier new` (like a typewriter), `comic sans ms` (like handwriting) and `impact` (big and bold). The cheat sheet's **Fonts** topic shows each one. |
| `size 15` | The size in points (`size15` works too). 15 if you leave it out. |
| `location top left` | Where in the shape: `left`, `centre`, `right`, `top`, `bottom`, or two together such as `top right` or `bottom-left`. Centre if you leave it out. |

The words can hold variables (`text [panel] Hi [who]`), a shape can hold several
text lines, and a `text [name] ...` line can also come later in the program, to
write in a shape drawn earlier. Text too tall for its shape is centred across
it rather than cut off. `Height`, `Text` and `End` work in any capitals.

A shape with no name works just as before, with no `End`.

The shapes are:

| Shape | What it looks like |
|---|---|
| `box` | Square corners |
| `rounded box` | Soft corners |
| `pill` | A box with fully round ends |
| `circle` | The biggest circle that fits its space |
| `oval` | Stretches to fill its space |
| `triangle` | Pointing up |
| `diamond` | A square on its point |
| `hexagon` | Six sides, flat top and bottom |
| `star` | Five points, one straight up |
| `line` | From one corner of its space to the other |

The round and pointed ones (circle, triangle, diamond, hexagon, star) start
100px square rather than 200px wide. A `line` given only a top lies flat.
In the cheat sheet, the **Shapes** topic has every one.

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
| `this test gave text, but a test has to be true or false` | The test is a value, not a comparison: `if [name]` should be `if [name] is Rocco` |
| `I did not expect '>5' here` | Missing spaces: `[x]>5` should be `[x] > 5` |
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

**Right-click Run** for **Run All Files** (or Ctrl+Shift+Enter): it runs every
`.adda` file in the project, one after another - `first.adda` first, then the
rest by name, then the files in any folders - each under a heading with its
name. Each file is a program of its own, so a variable set in one is not there
in the next, and a mistake in one file does not stop the others. Stop ends the
whole run.

The code box colours your code as you type: keywords like `print`, `if` and
`end`, variables like `[name]`, numbers, `true`/`false`/`nothing`, quoted text
and `# comments` each have their own colour, which follows the theme (Settings, **Editor**, turns it off). A word is
only coloured as a keyword where it really acts as one, so the `and` in
`print Tom and Jo` stays plain text.

Drag the divider between the code and the console to give either one more room,
or double-click it to split them evenly. **F11** goes full screen.

Down the left side:

| Icon | What it does |
|---|---|
| Explorer | Shows the project you have open - its `.adda` files, and nothing else. Click a file to open it; each file keeps its own code, and what you type is saved into it when you open another file, press Run, or quit. With no project open it tells you how to get one. |
| Search | Finds text in your code; Enter jumps to the next match. |
| New Project | Asks where to save a new project and what to call it, then makes that folder with `first.adda` and `style.adda` in it and opens it as the project. |
| Tidy view | Shows each finished named shape in a cleaner form - `insert rounded box; name ——> box1`, `15px ——> top, right, left`, `text [box1] --> Hello --> font impact --> size 15` - and each finished `delay` block with `└─>` in front of the lines inside it instead of their indent, with the arrows in bold. A delay block with another block inside it (an `if`, say) stays as it is. It is only how the code is shown: your file is always saved as normal code. A block turns tidy as soon as you type its `end`; click on one of its lines to see it as raw code again. Click the button to turn tidy view off (it is lit while it is on). |
| Cheat sheet | Everything in this guide, grouped into topics. Click a topic to open its page, then click anything in it to open a page of its own: what it does, and an example to read and type out yourself. Nothing is pasted into your program. **<- Back** returns, and typing in the search box on the topics page searches every topic. |
| Settings | **Themes**: Follow Windows, Light, Dark, Beige, or Abyss. **Editor**: switch **Colour the code** off for code in one plain colour, or back on. Both are remembered. |

Along the bottom of the Explorer, **Save** writes your code wherever you choose,
and **Import** brings things in. Import a **folder** and it becomes the project:
it is opened where it is - nothing is copied, so importing it again just opens
it again - and its `first.adda` (or its first program) is opened. Import
**files** and they are copied into the project you have open. On Windows,
Import first asks which of the two you want. The project you had open is
remembered, and opened again the next time you start Adda.

Clicking the Explorer or Search icon you are already on closes the panel again.
Whichever theme you pick is remembered for next time.

On a Mac the window is `Adda.app` and works the same way. The keys are the
Mac's own: **⌘R** or **⌘Return** runs, **⇧⌘R** runs all files, **⌘.** stops, Return answers an `ask`,
**⌃⌘F** (or the green button) goes full screen, and Settings offers
**Follow macOS** in place of Follow Windows. In the Explorer, Return renames a
file and Delete moves it to the Trash, and the **+** at the top makes a new
file or folder in the project. To look at the examples that come with Adda,
import the `examples` folder.

## 14. Full list of words Adda reserves

`print` `if` `else` `while` `for` `each` `in` `delay` `openApplication` `insert` `text` `define` `with` `return` `call`
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
