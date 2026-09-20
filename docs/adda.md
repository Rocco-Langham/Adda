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
name = Rocco
age = 30
```

A bare word is text. A number on its own is a number.

To put a value inside text, wrap it in braces:

```adda
print Hello {name}, you are {age}
```

Braces mean **the value of**. That is the only thing they ever mean.

## 3. Maths

An operator counts only when it has a space on **both** sides:

```adda
next = age + 1          # 31
half = age / 2          # 15
left = 10 % 3           # 1
```

Without the spaces, it is just part of a word:

```adda
date = 2024-01-15       # text, not maths
who  = Mary-Jane        # text
code = 007              # text, and the zeros survive
```

This is deliberate. It is what lets you type dates, names and percentages
without thinking about it.

If text needs an operator inside it, use quotes:

```adda
title = "Chapter 1 - Intro"
```

### Copying a value

A bare word that names a variable means that variable:

```adda
count = 5
copy = count            # 5
```

If nothing has that name, it stays as the word:

```adda
name = Rocco            # the word Rocco
```

And quotes always win:

```adda
word = "count"          # the word count, even though count exists
```

## 4. True, false and nothing

```adda
ok = true
done = false
result = nothing
```

## 5. Making decisions

```adda
if age > 18
    print Adult
else if age > 12
    print Teenager
else
    print Child
end
```

Blocks end with `end`. Indentation is ignored, so you cannot break a program by
spacing it differently.

Comparisons: `is`, `is not`, `<`, `>`, `<=`, `>=`, and `!=`.

```adda
if name is Rocco
    print Hello again
end

if name is not Bob
    print You are not Bob
end
```

They chain the way they read:

```adda
if 1 < x < 10
    print In range
end
```

Combine tests with `and`, `or`, `not`:

```adda
if age > 18 and name is Rocco
    print Both
end

if not done
    print Still going
end
```

A test has to come out true or false. `if count` is an error — Adda will tell
you to write `if count > 0`.

## 6. Repeating

```adda
count = 0
while count < 5
    print count is {count}
    count = count + 1
end
```

## 7. Functions

```adda
define greet with person
    print Hello {person}
end

call greet with Rocco
```

Use `return` to send a value back, and `call` inside braces to use it:

```adda
define add_up with a, b
    return a + b
end

print {call add_up with 2, 3}
```

Functions can call themselves:

```adda
define factorial with n
    if n <= 1
        return 1
    end
    return n * call factorial with n - 1
end
```

A function can be called before the line that defines it — Adda reads them all
first. Functions live on their own, at the outer level of a file; you cannot
define one inside an `if` or another function.

## 8. Lists

```adda
nums = list 10, 20, 30
```

Lists count from **1**:

```adda
print {item 1 of nums}      # 10
item 1 of nums = 99
```

Each thing between the commas follows the usual rule, so this is a list of
three words:

```adda
friends = list Rocco, Mary, Sam
```

Growing, shrinking and measuring:

```adda
add 40 to nums
remove item 2 of nums
print {length of nums}
print {has 40 of nums}
```

Going through one:

```adda
for each n in nums
    print {n}
end
```

An empty list to start with:

```adda
scores = list
add 10 to scores
```

## 9. Maps

A map holds values under names.

```adda
person = map
name of person = Rocco
age of person = 30

print {name of person}
```

`name of person` uses `name` as the key, exactly as written. When the key is in
a variable, use `item`:

```adda
k = name
print {item k of person}
```

That is also how you check and remove:

```adda
print {has "age" of person}
remove age of person
print {length of person}
```

Maps keep keys in the order you added them, so going through one is
predictable:

```adda
for each key in person
    print {key} is {item key of person}
end
```

## 10. When something is wrong

Adda points at the line and says what it expected:

```
examples/broken.adda:4: 'totl' is not defined - did you mean 'total'?
     4 | print {totl}
       | ^
```

Common ones:

| Message | Usually means |
|---|---|
| `'x' is not defined` | A typo, or the variable is set further down |
| `this test gave text, but a test has to be true or false` | Missing spaces: `x>5` should be `x > 5` |
| `there is no item 0 - Adda counts from 1` | Lists start at 1 |
| `this 'if' is never closed` | A missing `end` |
| `'item' is a special word in Adda` | Use quotes to write it as text |
| `'+' adds numbers, and one of these is text` | Join text with braces: `{a} {b}` |

## 11. Full list of words Adda reserves

`print` `if` `else` `while` `for` `each` `in` `define` `with` `return` `call`
`add` `to` `remove` `end` `list` `map` `item` `length` `has` `of` `is` `not`
`and` `or` `true` `false` `nothing`

`list`, `map`, `item`, `length`, `call` and `has` cannot be used as variable
names, because they start a value. The rest are fine as ordinary words in text.

## 12. Things to watch out for

- `range = 1 - 10` really is maths. Write `1-10` or `"1 - 10"`.
- `#` starts a comment wherever a word could start, so
  `note = press # for menu` loses the end. Quote it.
- `x = -y` is the word `-y`, because `-` has no space after it. Write `0 - y`.
- A misspelt name in a comparison quietly becomes text rather than an error:
  `if total is totl` compares against the word `totl`. Adda suggests the nearest
  name when it can.
- Adda frees memory only when the program ends, so a loop that keeps building
  new text will keep growing.
