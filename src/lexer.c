/* The lexer, where Adda's central rule is enforced.
 *
 * An operator character is only an operator when it has whitespace on BOTH
 * sides. That single decision is what lets bare words be text without the
 * language guessing:
 *
 *     date = 2024-01-15      one word, so text          (not 2024 minus 1 minus 15)
 *     name = Mary-Jane       one word, so text
 *     off  = 50% off         one word, so text
 *     next = age + 1         spaced, so maths
 *     x    = -5              '-' has no space after it, so it is part of "-5"
 *
 * Two exceptions, both deliberate:
 *   - '=' is always its own token, so `x=5` still assigns.
 *   - '>=', '<=' and '!=' are always operators. They never appear in ordinary
 *     prose, and treating `x>=5` as a word would let a comparison silently
 *     become text - the exact failure this design exists to prevent.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "adda.h"

typedef struct {
    Token    *items;
    uint32_t  count, cap;
} Buf;

static void emit(Buf *b, Token t)
{
    if (b->count + 1 > b->cap) {
        uint32_t cap = b->cap < 64 ? 64 : b->cap * 2;
        Token *items = adda_alloc(sizeof(Token) * cap);
        if (b->count) memcpy(items, b->items, sizeof(Token) * b->count);
        b->items = items;
        b->cap = cap;
    }
    b->items[b->count++] = t;
}

static bool is_hspace(char c) { return c == ' ' || c == '\t' || c == '\r'; }
static bool is_digit(char c)  { return c >= '0' && c <= '9'; }

/* '>=', '<=', '!=' - operators wherever they appear. */
static int two_char_op(const char *p, TokenKind *kind)
{
    if (p[0] == '>' && p[1] == '=') { *kind = TK_GE; return 2; }
    if (p[0] == '<' && p[1] == '=') { *kind = TK_LE; return 2; }
    if (p[0] == '!' && p[1] == '=') { *kind = TK_NE; return 2; }
    return 0;
}

/* A single-character operator, but only when it is space-delimited. */
static int spaced_op(const char *src, const char *p, TokenKind *kind)
{
    TokenKind k;

    switch (p[0]) {
        case '+': k = TK_PLUS;    break;
        case '-': k = TK_MINUS;   break;
        case '*': k = TK_STAR;    break;
        case '/': k = TK_SLASH;   break;
        case '%': k = TK_PERCENT; break;
        case '<': k = TK_LT;      break;
        case '>': k = TK_GT;      break;
        default:  return 0;
    }
    if (!(p == src || is_hspace(p[-1]) || p[-1] == '\n')) return 0;
    if (!(p[1] == 0   || is_hspace(p[1]) || p[1] == '\n')) return 0;

    *kind = k;
    return 1;
}

/* Where a bare word ends. Note that operator characters are NOT listed: if one
 * were space-delimited, the space would already have ended the word. */
static bool word_ends_at(const char *p)
{
    TokenKind k;
    char c = *p;

    if (c == 0 || c == '\n' || is_hspace(c)) return true;
    if (c == ',' || c == '(' || c == ')' || c == '"' || c == '=') return true;
    if (c == '{' && p[1] != '{') return true;
    if (c == '}' && p[1] != '}') return true;
    if (two_char_op(p, &k)) return true;
    return false;
}

/* A word is a number if it is entirely numeric AND has no leading zero, so
 * that `code = 007` keeps its shape instead of quietly becoming 7. */
static bool word_is_number(const char *s, uint32_t len, double *out)
{
    uint32_t i = 0;
    bool seen_digit = false, seen_dot = false;
    char buf[64];

    if (len == 0 || len >= sizeof buf) return false;
    if (s[0] == '-') i = 1;
    if (i >= len) return false;

    /* leading zero: 0 and 0.5 are fine, 007 and 01 are not */
    if (s[i] == '0' && i + 1 < len && s[i + 1] != '.') return false;

    for (; i < len; i++) {
        if (is_digit(s[i])) { seen_digit = true; continue; }
        if (s[i] == '.' && !seen_dot && seen_digit) { seen_dot = true; continue; }
        return false;
    }
    if (!seen_digit) return false;
    if (s[len - 1] == '.') return false;

    memcpy(buf, s, len);
    buf[len] = 0;
    *out = strtod(buf, NULL);
    return true;
}

TokenList lex_range(const char *base, const char *start, const char *end,
                    uint32_t first_line)
{
    Buf b;
    const char *p = start;
    uint32_t line = first_line;
    TokenList out;

    b.items = NULL;
    b.count = b.cap = 0;

    while (p < end) {
        Token t;
        TokenKind kind;
        int n;

        memset(&t, 0, sizeof t);
        t.line = line;
        t.start = p;

        if (is_hspace(*p)) { p++; continue; }

        if (*p == '\n') {
            t.kind = TK_NEWLINE; t.len = 1;
            emit(&b, t);
            line++; p++;
            continue;
        }

        /* '#' only starts a comment at the beginning of a token, so C# and
         * file#1 survive as words. */
        if (*p == '#') {
            while (p < end && *p != '\n') p++;
            continue;
        }

        if (*p == '"') {
            const char *inner = ++p;
            while (p < end && *p != '"') {
                if (*p == '\\' && p + 1 < end) p++;
                if (*p == '\n') line++;
                p++;
            }
            if (p >= end || *p != '"')
                adda_error_at(t.start, t.line, "this text is missing its closing quote");
            t.kind  = TK_STRING;
            t.start = inner;                     /* content, without the quotes */
            t.len   = (uint32_t)(p - inner);
            emit(&b, t);
            p++;
            continue;
        }

        if ((n = two_char_op(p, &kind)) != 0) {
            t.kind = kind; t.len = (uint32_t)n;
            emit(&b, t);
            p += n;
            continue;
        }

        if ((n = spaced_op(base, p, &kind)) != 0) {
            t.kind = kind; t.len = (uint32_t)n;
            emit(&b, t);
            p += n;
            continue;
        }

        if (*p == '=' || *p == ',' || *p == '(' || *p == ')' ||
            (*p == '{' && p[1] != '{') || (*p == '}' && p[1] != '}')) {
            switch (*p) {
                case '=': t.kind = TK_ASSIGN; break;
                case ',': t.kind = TK_COMMA;  break;
                case '(': t.kind = TK_LPAREN; break;
                case ')': t.kind = TK_RPAREN; break;
                case '{': t.kind = TK_LBRACE; break;
                default:  t.kind = TK_RBRACE; break;
            }
            t.len = 1;
            emit(&b, t);
            p++;
            continue;
        }

        /* a bare word */
        {
            const char *word = p;
            double num;
            uint32_t len;

            while (p < end && !word_ends_at(p)) {
                /* {{ }} and \" are escapes, so they stay inside the word */
                if ((*p == '{'  && p[1] == '{') ||
                    (*p == '}'  && p[1] == '}') ||
                    (*p == '\\' && p[1] == '"')) p += 2;
                else p++;
            }
            len = (uint32_t)(p - word);
            if (len == 0) {
                adda_error_at(p, line, "I do not understand the character '%c' here", *p);
            }

            t.start = word;
            t.len   = len;
            if (word_is_number(word, len, &num)) {
                t.kind = TK_NUMBER;
                t.number = num;
            } else {
                t.kind = TK_WORD;
                t.text = intern(word, len);
            }
            emit(&b, t);
        }
    }

    {
        Token t;
        memset(&t, 0, sizeof t);
        t.kind = TK_EOF;
        t.line = line;
        t.start = p;
        emit(&b, t);
    }

    out.tokens = b.items;
    out.count  = b.count;
    return out;
}

TokenList lex(const char *src)
{
    return lex_range(src, src, src + strlen(src), 1);
}

bool token_is_word(const Token *t, const char *word)
{
    size_t len = strlen(word);
    return t->kind == TK_WORD && t->len == len &&
           memcmp(t->start, word, len) == 0;
}

const char *token_kind_name(TokenKind k)
{
    switch (k) {
        case TK_EOF:     return "end";
        case TK_NEWLINE: return "newline";
        case TK_WORD:    return "word";
        case TK_NUMBER:  return "number";
        case TK_STRING:  return "text";
        case TK_PLUS:    return "+";
        case TK_MINUS:   return "-";
        case TK_STAR:    return "*";
        case TK_SLASH:   return "/";
        case TK_PERCENT: return "%";
        case TK_LT:      return "<";
        case TK_GT:      return ">";
        case TK_LE:      return "<=";
        case TK_GE:      return ">=";
        case TK_NE:      return "!=";
        case TK_ASSIGN:  return "=";
        case TK_COMMA:   return ",";
        case TK_LPAREN:  return "(";
        case TK_RPAREN:  return ")";
        case TK_LBRACE:  return "{";
        case TK_RBRACE:  return "}";
    }
    return "?";
}
