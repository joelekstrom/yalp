#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>

enum sexpr_t
{
    nil,
    error,
    list,
    integer,
    symbol,
    function
};

enum function_t
{
    builtin,
    lambda
};

struct env;

struct sexpr
{
    enum sexpr_t tag;
    union 
    {
        struct
        {
            struct sexpr* head;
            struct sexpr* tail;
        } list;
        int integer;
        const char* name;
        const char* message;
        struct
        {
            enum function_t tag;
            union
            {
                struct sexpr* (*builtin) (struct env*, struct sexpr*);
                struct 
                {
                    struct sexpr* params;
                    struct sexpr* expr;
                } lambda;
            };
        } function;
    };    
} _NIL = { .tag = nil };

struct sexpr* NIL = &_NIL;

struct sexpr* const new_sexpr(enum sexpr_t tag)
{
    struct sexpr* e = malloc(sizeof(struct sexpr));
    memset(e, 0, sizeof(struct sexpr));

    e->tag = tag;

    return e;
}

struct sexpr* const new_function(enum function_t tag)
{
    struct sexpr* e = new_sexpr(function);
    e->function.tag = tag;
    return e;
}

struct sexpr* new_error(const char* message)
{
    struct sexpr* e = new_sexpr(error);
    e->message = message;
    return e;
}

#define CHECK_ERROR(expr) if (!expr || expr->tag == error) return expr;

#define MAX_SYMBOL_NAME 64


struct symbol_mapping
{
    struct sexpr* symbol;
    struct sexpr* value;
};

struct env
{
    struct symbol_mapping* symbols;
    int symbol_count;
};

struct sexpr* get_env_symbol(struct env* env, const char* name)
{
    for(int i=0;i<env->symbol_count;i++)
    {
        if (env->symbols[i].symbol && strcmp(env->symbols[i].symbol->name, name) == 0)
            return env->symbols[i].value;
    }

    return NULL;
}

void add_env_symbol(struct env* env, struct symbol_mapping mapping)
{
    // Try reusing a free spot
    for (int i=0;i<env->symbol_count;i++)
    {
        if (env->symbols[i].symbol == NULL)
        {
            env->symbols[i] = mapping;
            return;
        }
    }
    env->symbols = realloc(env->symbols, sizeof(struct symbol_mapping) * (env->symbol_count+1));
    env->symbol_count += 1;
    env->symbols[env->symbol_count-1] = mapping;
}

void add_env_value(struct env* env, const char* name, struct sexpr* val)
{
    struct sexpr* s = new_sexpr(symbol);
    s->name = (const char*)malloc(MAX_SYMBOL_NAME);
    strcpy(s->name, name);

    add_env_symbol(env, (struct symbol_mapping){.symbol = s, .value = val}); 
}

void remove_env_value(struct env* env, const char* name)
{
    for(int i=0;i<env->symbol_count;i++)
    {
        struct symbol_mapping* mapping = &env->symbols[i];
        if (mapping->symbol && strcmp(mapping->symbol->name, name) == 0)
        {
            mapping->symbol = NULL;
            mapping->value = NULL;
            return;
        }
    }
}

struct sexpr* next(struct sexpr** list)
{
    if (*list == NIL)
        return NULL;

    struct sexpr* element = (*list)->list.head;
    *list =(*list)->list.tail;
    return element;
}

int list_length(struct sexpr* list)
{
    int count = 0;
    while (next(&list)) count++;
    return count;
}

void add_env_builtin_function(struct env* env, const char* name, struct sexpr* (*fn) (struct env* env, struct sexpr*))
{
    struct sexpr* v = new_function(builtin);
    v->function.builtin = fn;

    add_env_value(env, name, v);
}

struct sexpr* read_sexpr(const char** str);

bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

bool is_whitespace(char c)
{
    return c == ' ' || c == '\r' || c == '\n' || c == '\t';
}

void skip_whitespace(const char** str)
{
    for (;(**str) != '\0' && is_whitespace(**str); (*str)++);
}

struct sexpr* read_integer(const char** str)
{
    if (is_digit(**str))
    {
        int number = 0;
        while (is_digit(**str))
        {
            number = number * 10 + (**str) - '0';
            (*str)++;
        }
        struct sexpr* atom = new_sexpr(integer);
        atom->integer = number;
        return atom;
    }
    else
    {
        return NULL;
    }
}

bool is_symbol_character(char c)
{
    return
        (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        (c == '_');
}

bool is_operator(char c)
{   
    return         
        (c == '+') ||
        (c == '-') ||
        (c == '*') ||
        (c == '/');
}

struct sexpr* read_symbol(const char** str)
{
    if ((**str) == '\0')
        return NULL;

    if (is_digit(**str))
        return NULL; 

    char* name = malloc(MAX_SYMBOL_NAME);
    memset(name, '\0', MAX_SYMBOL_NAME);

    if (is_operator(**str))
    {
        name[0] = **str;
        (*str)++;
    }
    else
    {
        for (int i=0;i<MAX_SYMBOL_NAME-1;i++)
        {
            if (is_symbol_character(**str))
            {
                name[i] = **str;
                (*str)++;
            }
        }
    }

    struct sexpr* s = new_sexpr(symbol);
    s->name = name;

    return s;
}
struct sexpr* create_list(int element_count, ...)
{
    struct sexpr* head = NIL;
    struct sexpr* previous = NULL;
    va_list valist;
    va_start(valist, element_count); 

    for (int i=0;i<element_count;i++)
    {
        struct sexpr* element = va_arg(valist, struct sexpr*);
        struct sexpr* cell = new_sexpr(list);
        cell->list.head = element;
        cell->list.tail = NIL;
        if (previous)
            previous->list.tail = cell;
        else
            head = cell;

        previous = cell;
    }

    va_end(valist); 
    
    return head;
}

struct sexpr* read_quote(const char** str)
{
    if (**str == '\'')
    {
        (*str)++;
        
        struct sexpr* s = new_sexpr(symbol);
        s->name = "quote";
        struct sexpr* sexpr = read_sexpr(str);
        return create_list(2, s, sexpr);        
    }

    return NULL;
}

struct sexpr* read_list(const char** str)
{
    if ((**str) == '(')
    {
        (*str)++;
        struct sexpr* head = NIL;
        struct sexpr* previous = NULL;
        
        while ((*str)[0] != ')')
        {
            if ((*str)[0] == ' ')
            {
                (*str)++;
                continue;
            }
            struct sexpr* cell = new_sexpr(list);
            cell->list.head = read_sexpr(str);
            cell->list.tail = NIL;

            if (previous)
                previous->list.tail = cell;
            else
                head = cell;                

            previous = cell;
        }
        (*str)++;

        return head;
    }
    else
    {
        return NULL;
    }
    
}

struct sexpr* read_sexpr(const char** str)
{
    struct sexpr* e = NULL;
    
    // Skip spaces
    skip_whitespace(str);

    if ((e = read_integer(str)))
        return e;

    if ((e = read_list(str)))
        return e;

    if ((e = read_quote(str)))
        return e;

    if ((e = read_symbol(str)))
        return e;

    return new_error("Syntax error");
}

int as_integer(struct sexpr* e)
{
    if (e->tag == integer)
        return e->integer;
    else
        return 0; // Error?
}

bool as_bool(struct sexpr* e)
{
    switch (e->tag)
    {
        case integer:
            return e->integer != 0;
        default:
            return false;
    }
}

struct sexpr* eval_sexpr(struct env* env, struct sexpr* sexpr);
struct sexpr* eval_argument(struct env* env, struct sexpr* args, int n);

struct sexpr* eval_lambda(struct env* env, struct sexpr* args)
{
    struct sexpr* params = args->list.head;
    struct sexpr* body = args->list.tail->list.head;

    struct sexpr* sexpr = new_function(lambda);
    sexpr->function.lambda.params = params;
    sexpr->function.lambda.expr = body;

    return sexpr;
}

struct sexpr* eval_defun(struct env* env, struct sexpr* args)
{
    struct sexpr* s = args->list.head;

    CHECK_ERROR(s);

    if (s->tag != symbol)
    {
        return new_error("First argument to defun must be symbol");
    }

    struct sexpr* lambda = eval_lambda(env, args->list.tail);

    CHECK_ERROR(lambda);

    add_env_value(env, s->name, lambda);

    return lambda;
}

struct sexpr* eval_if(struct env* env, struct sexpr* args)
{
    struct sexpr* cond = eval_argument(env, args, 0);

    if (as_bool(cond))
        return eval_argument(env, args, 1);
    else
        return eval_argument(env, args, 2);
}

struct sexpr* eval_operator(char op, struct env* env, struct sexpr* args)
{

    int result = 0;
    if (op == '*' || op == '/')
        result = 1;
    bool first = true;
    while (args != NIL)
    {
        struct sexpr* arg = eval_sexpr(env, args->list.head);
        CHECK_ERROR(arg);

        int value = as_integer(arg);
        switch (op)
        {
            case '+': result += value; break;
            case '-': result -= value; break;
            case '*': result *= value; break;
            case '/':
                if (args->list.tail && first)
                    result = value;
                else
                    result /= value;
                break;
        }
        args = args->list.tail;
        first = false;
    }

    struct sexpr* e = new_sexpr(integer);
    e->integer = result;
    return e;
}

struct sexpr* eval_add(struct env* env, struct sexpr* args)
{
    return eval_operator('+', env, args);
}

struct sexpr* eval_subtract(struct env* env, struct sexpr* args)
{
    return eval_operator('-', env, args);
}

struct sexpr* eval_multiply(struct env* env, struct sexpr* args)
{
    return eval_operator('*', env, args);
}

struct sexpr* eval_division(struct env* env, struct sexpr* args)
{
    return eval_operator('/', env, args);
}

struct sexpr* eval_quote(struct env* env, struct sexpr* args)
{
    return args->list.head; // Quote returns the unevaluated first argument
}

struct sexpr* eval_list(struct env* env, struct sexpr* args)
{
    struct sexpr* head = NIL;
    struct sexpr* previous = NULL;

    struct sexpr* el;
    while ((el = next(&args)))
    {
        struct sexpr* cell = new_sexpr(list);
        cell->list.head = eval_sexpr(env, el);
        cell->list.tail = NIL;
        if (previous)
            previous->list.tail = cell;
        else
            head = cell;
        previous = cell;
    }

    return head;
}

struct sexpr* eval_argument(struct env* env, struct sexpr* args, int n)
{
    while (n-- > 0)
        next(&args);

    if (args != NIL)
        return eval_sexpr(env,args->list.head);
    else
        return NIL;
}

struct sexpr* eval_define(struct env* env, struct sexpr* args)
{
    struct sexpr* sym = args->list.head;

    CHECK_ERROR(sym);

    if (sym->tag != symbol)
    {
        return new_error("Argument not evaluated to symbol");
    }

    struct sexpr* value = eval_argument(env, args, 1);

    CHECK_ERROR(value);

    add_env_value(env, sym->name, value);

    return value;
}

struct sexpr* call_lambda(struct env* env, struct sexpr* lambda, struct sexpr* args)
{
    struct sexpr* params = lambda->function.lambda.params;
    struct sexpr* body = lambda->function.lambda.expr;

    // Iterate over and bind parameters
    struct sexpr* param;
    int pos = 0;
    while (param = next(&params))
    {
        struct sexpr* arg = eval_argument(env, args, pos);

        if (!arg)
        {
            return new_error("Not enough arguments for lambda function");
        }

        add_env_value(env, param->name, arg);
        pos++;
    }

    struct sexpr* result = eval_sexpr(env, body);

    // Iterate over parameters again to unbind them
    params = lambda->function.lambda.params;
    while (param = next(&params))
    {
        remove_env_value(env, param->name);
    }

    return result;
}

struct sexpr* eval_sexpr(struct env* env, struct sexpr* sexpr)
{
    // Only lists are evaluated
    if (sexpr->tag == list)
    {
        struct sexpr* value = eval_sexpr(env, sexpr->list.head);
        
        if (value->tag == function)
        {
            struct sexpr* args = sexpr->list.tail;
            switch (value->function.tag)
            {
                case builtin:
                    return value->function.builtin(env, args);
                case lambda:
                    return call_lambda(env, value, args);
            }
        }
        else
        {
            return new_error("Non function value found when evaluating list");
        }
    }
    else if (sexpr->tag == symbol)
    {
        struct sexpr* value = get_env_symbol(env, sexpr->name);
        if (!value)
        {
            printf("Unknown symbol: %s\n", sexpr->name);
            return new_error("Unknown symbol");
        }
        else
        {
            return value;
        }
    }
    else
    {
        return sexpr;
    }
}

void print_sexpr(struct sexpr* sexpr)
{
    switch (sexpr->tag)
    {
    case error:
        printf("Error: %s", sexpr->message);
        break;
    case nil:
        printf("()");
        break;
    case integer:
        printf("%d", sexpr->integer);
        break;
    case symbol:
        printf("%s", sexpr->name);
        break;
    case function:
        switch (sexpr->function.tag)
        {
            case builtin:
                printf("<builtin function>"); // TODO: Try to get name
                break;
            case lambda:
                printf("<lambda function>");
                break;
        }
        break;
    case list:
        printf("(");
        while (sexpr != NIL)
        {
            print_sexpr(sexpr->list.head);
            
            if ((sexpr = sexpr->list.tail) != NIL)
                printf(" ");
        }
        printf(")");
        break;
    }
}

void set_env(struct env* env)
{
    env->symbols = NULL;
    env->symbol_count = 0;
    add_env_builtin_function(env, "+", eval_add);
    add_env_builtin_function(env, "-", eval_subtract);
    add_env_builtin_function(env, "*", eval_multiply);
    add_env_builtin_function(env, "/", eval_define);
    add_env_builtin_function(env, "'", eval_quote);
    add_env_builtin_function(env, "quote", eval_quote);
    add_env_builtin_function(env, "list", eval_list);
    add_env_builtin_function(env, "define", eval_define);
    add_env_builtin_function(env, "if", eval_if);
    add_env_builtin_function(env, "lambda", eval_lambda);
    add_env_builtin_function(env, "defun", eval_defun);
}

void readline(char* buff, size_t size, bool* eof)
{
    memset(buff, '\0', size);
    for(int i=0;i<size-1;)
    {
        int data = getchar();
        if (data == EOF)
        {
            if (eof)
                *eof = true;
            return;
        }
        if (data == '\r')
            continue;
        if (data == '\n')
            return;
        buff[i++] = data;
    }
}


int main()
{
    struct env env;
    set_env(&env);

    bool eof = false;

    while (!eof)
    {
        char line[4096];
        printf("> "); readline(line, 4096, &eof);
        const char* input = line;

        skip_whitespace(&input);

        if (input[0] == '\0')
            continue;

        struct sexpr* e = read_sexpr(&input);

        if (*input != '\0')
        {
            printf("Error: unparsed content in string: %s\n", input);
            continue;
        }

        if (!e || e->tag == error)
        {
            printf("Error: %s\n", e->message);
            continue;
        }

        e = eval_sexpr(&env, e);
        printf("< "); print_sexpr(e); printf("\n");
    }

    return 0;
}