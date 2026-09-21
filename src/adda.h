/* adda.h - shared types for the Adda language.
 *
 * Adda is a small, deliberately punctuation-light language:
 *
 *     name = Rocco                 # bare words are text
 *     age  = 30
 *     next = age + 1               # a spaced operator means maths
 *     print Hello {name}           # braces mean "the value of"
 *
 * The interpreter is a tree-walker. Memory comes from a bump arena that is
 * released once at exit; there is no collector yet. Every allocation still
 * carries an Obj header on an all-objects list so a mark-sweep pass can be
 * dropped in later without touching the rest of the code.
 */
#ifndef ADDA_H
#define ADDA_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>

/* ------------------------------------------------------------------ arena */

void   arena_init(void);
void  *adda_alloc(size_t size);      /* the only allocator in the program */
void   arena_free_all(void);
size_t arena_bytes_used(void);

/* ------------------------------------------------------------------ errors */

extern jmp_buf adda_error_jmp;       /* longjmp target installed in main() */

void adda_source(const char *filename, const char *text);
void adda_error(uint32_t line, const char *fmt, ...);   /* never returns */
void adda_error_at(const char *at, uint32_t line, const char *fmt, ...);
void adda_hint(const char *key, const char *fmt, ...);  /* prints once per key */

/* The last error reported, kept for `adda --check`, which sets adda_quiet so
 * nothing is printed and reads these instead. */
extern bool        adda_quiet;
extern uint32_t    adda_err_line;
extern const char *adda_err_at;          /* NULL when only the line is known */
extern char        adda_err_msg[256];

/* ------------------------------------------------------------------ values */

typedef enum {
    T_NOTHING, T_BOOL, T_NUMBER, T_TEXT, T_LIST, T_MAP, T_FUNC
} Type;

typedef struct Obj {
    struct Obj *next;    /* all-objects list; becomes the GC sweep list */
    uint8_t     type;
    uint8_t     mark;    /* unused in v1 */
} Obj;

typedef struct Value {
    uint8_t type;
    union { double num; bool b; Obj *obj; } as;
} Value;

/* Text is immutable, length-prefixed, NUL-terminated (so the C library still
 * works on it) and carries a cached hash, because `name of person` inside a
 * loop would otherwise re-hash the key every iteration. */
typedef struct Text {
    Obj      obj;
    uint32_t len;
    uint32_t hash;
    char     bytes[1];   /* actually len + 1 bytes */
} Text;

typedef struct List {
    Obj      obj;
    uint32_t len, cap;
    Value   *items;
} List;

typedef struct Entry { Text *key; Value val; } Entry;

/* Dense entries + an open-addressed slot table, so `for each` over a map
 * yields keys in insertion order, which is what people expect. */
typedef struct Map {
    Obj       obj;
    uint32_t  count, cap;
    Entry    *entries;
    int32_t  *index;
    uint32_t  index_cap;
} Map;

typedef struct Node Node;

typedef struct Func {
    Obj      obj;
    Text    *name;
    Text   **params;
    uint8_t  nparams;
    Node    *body;
} Func;

#define IS_NOTHING(v) ((v).type == T_NOTHING)
#define IS_BOOL(v)    ((v).type == T_BOOL)
#define IS_NUMBER(v)  ((v).type == T_NUMBER)
#define IS_TEXT(v)    ((v).type == T_TEXT)
#define IS_LIST(v)    ((v).type == T_LIST)
#define IS_MAP(v)     ((v).type == T_MAP)
#define IS_FUNC(v)    ((v).type == T_FUNC)

#define AS_TEXT(v)    ((Text *)(v).as.obj)
#define AS_LIST(v)    ((List *)(v).as.obj)
#define AS_MAP(v)     ((Map  *)(v).as.obj)
#define AS_FUNC(v)    ((Func *)(v).as.obj)

Obj  *adda_obj_new(size_t size, Type type);  /* the one object allocator */
Value nothing_value(void);
Value bool_value(bool b);
Value number_value(double n);
Value text_value(Text *t);
Value obj_value(Type type, Obj *o);

Text *text_new(const char *bytes, size_t len);
Text *text_from_cstr(const char *s);
Text *text_concat(Text *a, Text *b);
Text *intern(const char *bytes, size_t len);   /* identifiers and literal keys */

List *list_new(void);
void  list_push(List *l, Value v);
void  list_insert(List *l, uint32_t at, Value v);
void  list_remove(List *l, uint32_t at);

Map  *map_new(void);
bool  map_get(Map *m, Text *key, Value *out);
void  map_set(Map *m, Text *key, Value v);
bool  map_remove(Map *m, Text *key);

const char *type_name(Type t);
Text *value_to_text(Value v);          /* how a value prints */
bool  value_equal(Value a, Value b);
void  value_print(Value v);

/* Values living only in C locals are invisible to a future collector. These
 * are no-ops beyond a counter today, but the call sites are already correct. */
void  temp_push(Value v);
void  temp_pop(int n);
#define PUSH_TEMP(v) temp_push(v)
#define POP_TEMP(n)  temp_pop(n)

/* ------------------------------------------------------------------ tokens */

typedef enum {
    TK_EOF, TK_NEWLINE,
    TK_WORD,        /* identifier, keyword, or a bare word of text */
    TK_NUMBER,
    TK_STRING,
    TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH, TK_PERCENT,
    TK_LT, TK_GT, TK_LE, TK_GE, TK_NE,
    TK_ASSIGN,      /* '=' : always a token, never folded into a word */
    TK_COMMA, TK_LPAREN, TK_RPAREN, TK_LBRACE, TK_RBRACE
} TokenKind;

typedef struct {
    TokenKind   kind;
    const char *start;    /* points into the source buffer */
    uint32_t    len;
    uint32_t    line;
    double      number;   /* TK_NUMBER only */
    Text       *text;     /* TK_WORD (interned) and TK_STRING */
    bool        bracketed;/* TK_WORD written as [name]: a variable */
} Token;

typedef struct {
    Token    *tokens;
    uint32_t  count;
} TokenList;

TokenList lex(const char *src);

/* openApplication: a window whose printed lines sit in its dead centre. One
 * implementation per platform: window_mac.m, window_win.c, window_none.c.
 * Closing the window ends the program. */
bool adda_open_window(const char *title);          /* false: no window here */
bool adda_window_is_open(void);
void adda_window_print(const char *text, size_t len);  /* one line */
void adda_window_wait_ms(double ms);   /* a delay that keeps the window alive */
void adda_window_run(void);            /* at the end: wait for it to close */
/* Length of a `[name]` starting at p (brackets included), or 0. It means the
 * same as {name}: the value of the variable called name. */
uint32_t  adda_bracket_name(const char *p);
TokenList lex_range(const char *base, const char *start, const char *end,
                    uint32_t first_line);
const char *token_kind_name(TokenKind k);
bool token_is_word(const Token *t, const char *word);

/* The rule that decides a number from text. The lexer uses it on source words
 * and `ask` uses it on what the user types, so both behave the same. */
bool adda_number_from_text(const char *s, uint32_t len, double *out);

/* ------------------------------------------------------------------ ast */

typedef enum {
    /* expressions */
    N_NUMBER, N_TEXT, N_BOOL, N_NOTHING,
    N_VAR,        /* bare word resolved as a variable */
    N_TEMPLATE,   /* text with {...} holes; kids are literals and expressions */
    N_BINARY, N_UNARY, N_AND, N_OR,
    N_LIST, N_MAP,
    N_INDEX,      /* item <a> of <b> */
    N_FIELD,      /* <name> of <b>, literal key */
    N_LENGTH,     /* length of <a> */
    N_HAS,        /* has <a> of <b> */
    N_ASK,        /* ask <prompt> - reads a line from the person running it */
    N_CALL,
    /* statements */
    N_BLOCK, N_ASSIGN, N_PRINT, N_IF, N_WHILE, N_FOREACH,
    N_DEFINE, N_RETURN, N_ADD, N_REMOVE, N_EXPRSTMT, N_DELAY, N_OPENAPP
} NodeKind;

struct Node {
    NodeKind  kind;
    uint32_t  line;
    Node     *a, *b, *c;     /* operands / condition / branches */
    Node    **kids;          /* block statements, list elements, call args */
    uint32_t  nkids;
    Text     *name;          /* variable, field key, function name */
    Text    **params;
    uint8_t   nparams;
    double    number;
    bool      flag;
    int       op;            /* TokenKind for N_BINARY / N_UNARY */
};

Node *parse(TokenList tokens);
/* Interactive parsing lets a bare expression stand as a statement, so the REPL
 * can echo it instead of refusing the line. */
Node *parse_mode(TokenList tokens, bool interactive);

/* ------------------------------------------------------------------ interp */

void interpret(Node *program);      /* one whole program */

/* The REPL keeps one set of globals alive across many entries. */
void interp_init(void);
void interp_run(Node *program);
void interp_show_variables(void);

void repl(void);

#endif /* ADDA_H */
