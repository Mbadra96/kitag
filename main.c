#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "ctype.h"
#include "stdbool.h"
#include "assert.h"
         

#define ARENA_CAP 1024 * 10000

typedef struct 
{
    char begin[ARENA_CAP];
    size_t index;
}Arena_t;

void *arena_alloc(Arena_t *a, size_t size)
{
    assert((size+a->index) < ARENA_CAP && "ARENA ERROR: excceded the ARENA_CAP please increase");
    void* ptr = &a->begin[a->index];
    a->index += size;
    return ptr;
}
void arena_reset(Arena_t *a)
{
    a->index = 0;
}

Arena_t arena = {0}; 

#define MALLOC(bytes) arena_alloc(&arena ,bytes)

typedef enum
{
    NONE,
    SYMBOL,
    STRING,
    INTEGER,
    FLOAT,
    LIST

} s_type;

typedef struct node_s
{
    struct node_s *next;
    s_type type;
    union
    {
        char *value;
        struct node_s *list;
    };
    char *begin;
    char *end;

} node;

typedef struct
{
    char *root;
    char *ptr;
    size_t size;

} content_t;

typedef struct symbol_s
{
    struct symbol_s *next;
    const char *name;
    node *data;
} symbol_t;

typedef struct
{
    node *root;
    const char *version;
    const char *generator;
    int no_of_sym;
    symbol_t *symbols;
} kicad_symbol_lib;

content_t read_file(const char *file_path)
{
    FILE *fp = fopen(file_path, "r");

    if (!fp)
    {
        return (content_t){0};
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *content = (char *)MALLOC(file_size + 1);
    content[file_size] = '\0';
    fread(content, file_size, 1, fp);

    fclose(fp);
    return (content_t){content, content, file_size};
}
const char *type_cstr(s_type type)
{
    switch (type)
    {
    case INTEGER:
        return "INTEGER";
    case FLOAT:
        return "FLOAT";
    case STRING:
        return "STRING";
    case SYMBOL:
        return "SYMBOL";
    case LIST:
        return "LIST";
    default:
        return "NONE";
    }
}
node *make_value_node(const char *value, s_type type)
{
    node *n = (node *)MALLOC(sizeof(node));
    n->value = (char *)MALLOC(strlen(value) + 1);
    strcpy(n->value, value);
    n->type = type;
    return n;
}
node *make_list_node()
{
    node *n = (node *)MALLOC(sizeof(node));
    n->type = LIST;
    return n;
}
bool sexpr_print_list(FILE *f, node *n)
{
    if (n->type != LIST)
        return false;
    size_t size = n->end - n->begin + 1;
    if (n->end <= n->begin)
        return false;
    fwrite(n->begin, 1, size, f);
    fprintf(f, "\n");
    return true;
}


node *sexpr_parse_content(content_t *content)
{
    node *head = NULL;
    node *tail = NULL;
    while (*content->ptr != '\0')
    {
        node *node = NULL;
        if (*content->ptr == ')')
        {
            break;
        }
        else if (*content->ptr == ' ' || *content->ptr == '\n' || *content->ptr == '\t')
        {
            content->ptr++;
        }
        else if (*content->ptr == '(')
        {
            node = make_list_node();
            node->begin = content->ptr;
            content->ptr++;

            node->list = sexpr_parse_content(content);
            node->end = content->ptr;
            content->ptr++;
        }
        else
        {
            char *ptr = strpbrk(content->ptr, " )\n\t");
            size_t value_size = ptr - content->ptr;
            char str[value_size + 1];
            memcpy(str, content->ptr, value_size);
            str[value_size] = '\0';
            s_type type = NONE;
            if (str[0] == '"' || str[0] == '\"')
            {
                type = STRING;
            }
            else if (strspn(str, "-.0123456789"))
            {
                if (strchr(str, '.'))
                {
                    type = FLOAT;
                }
                else
                {
                    type = INTEGER;
                }
            }
            else
            {
                type = SYMBOL;
            }
            node = make_value_node(str, type);

            content->ptr += value_size;
        }
        if (node != NULL)
        {
            // Terminate the node
            node->next = NULL;

            if (head == NULL)
            {
                // Initialize the list head
                head = node;
                tail = node;
            }
            else
            {
                // Append the node to the list tail
                tail->next = node;
                tail = node;
            }
        }
    }
    return head;
}

void print_kicad_sym_lib(FILE *f, kicad_symbol_lib lib)
{
    if (lib.no_of_sym == 0)
        return;
    fprintf(f, "(kicad_symbol_lib\n");
    node *root = lib.root->list->next;
    while (root != NULL)
    {
        if (root->type == LIST)
        {
            if (strcmp(root->list->value, "symbol") == 0)
            {
                break;
            }
            else
            {
                sexpr_print_list(f, root);
            }
        }
        root = root->next;
    }

    symbol_t *symbol = lib.symbols;
    while (symbol != NULL)
    {
        sexpr_print_list(f, symbol->data);
        symbol = symbol->next;
    }

    fprintf(f, ")\n");
}
bool fill_kicad_sym_from_sexpr_node(node *n, symbol_t *symbol)
{
    node *current_sym = n;
    if (current_sym->type != LIST)
        return false;
    if (n->list->type != SYMBOL)
        return false;
    if (strcmp(current_sym->list->value, "symbol") != 0)
        return false;
    symbol->data = current_sym;
    symbol->name = current_sym->list->next->value;
    return true;
}

bool get_value_from_sexpr_list(node *n, const char *symbol, const char **version_ptr)
{
    node *current_sym = n;
    if (current_sym->type != LIST)
        return false;
    if (strcmp(current_sym->list->value, symbol))
    {
        fprintf(stderr, "ERROR: expected \"%s\" but got %s", symbol, current_sym->list->value);
        return false;
    }
    *version_ptr = current_sym->list->next->value;
    return true;
}


bool create_kicad_sym_lib_from_sexpr_node(node *n, kicad_symbol_lib *lib)
{
    lib->root = n;
    node *current_sym = n;
    if (current_sym->type != LIST)
        return false;
    if (strcmp(current_sym->list->value, "kicad_symbol_lib"))
    {
        fprintf(stderr, "ERROR: expected \"kicad_symbol_lib\" but got %s", current_sym->list->value);
        return false;
    }
    current_sym = current_sym->list->next;
    if (!get_value_from_sexpr_list(current_sym, "version", &lib->version))
        return false;
    current_sym = current_sym->next;
    if (!get_value_from_sexpr_list(current_sym, "generator", &lib->generator))
        return false;
    current_sym = current_sym->next;

    int i = 0;
    symbol_t *symbols = NULL;
    while (current_sym != NULL && current_sym->type == LIST)
    {
        if (current_sym->list->type == SYMBOL)
        {
            if (strcmp(current_sym->list->value, "symbol") == 0)
            {

                if (lib->symbols == NULL)
                {
                    lib->symbols = (symbol_t *)calloc(1, sizeof(symbol_t));
                    symbols = lib->symbols;
                }
                else
                {
                    symbols->next = (symbol_t *)calloc(1, sizeof(symbol_t));
                    symbols = symbols->next;
                }

                if (!fill_kicad_sym_from_sexpr_node(current_sym, symbols))
                    return false;
                i++;
            }
        }
        current_sym = current_sym->next;
    }
    lib->no_of_sym = i;
    return true;
}

void remove_sym_from_lib(kicad_symbol_lib *lib, const char *symbol_name)
{
    if (lib->symbols != NULL)
    {
        symbol_t *parent = lib->symbols;
        if (strcmp(parent->name, symbol_name) == 0)
        {
            lib->symbols = lib->symbols->next;
            lib->no_of_sym--;
            return;
        }
        symbol_t *child = parent->next;
        while (child != NULL)
        {
            if (strcmp(child->name, symbol_name) == 0)
            {
                parent->next = child->next;
                lib->no_of_sym--;
                return;
            }
            parent = child;
            child = child->next;
        }
    }
}

int main()
{
    const char *file_name = "CPU.kicad_sym";
    const char *output_file = "output1.kicad_sym";
    FILE *f = fopen(output_file, "w+");
    if (f == NULL)
    {
        fprintf(stderr, "ERROR: cannot write to file %s\n", output_file);
    }

    content_t content = read_file(file_name);
    node *root = sexpr_parse_content(&content);
    kicad_symbol_lib lib = {0};
    create_kicad_sym_lib_from_sexpr_node(root, &lib);
    // remove_sym_from_lib(&lib, "\"CDP1802ACEX\"");
    // remove_sym_from_lib(&lib,"\"Z80CPU\"");
    // remove_sym_from_lib(&lib,"\"CDP1802BCEX\"");
    // remove_sym_from_lib(&lib,"\"CDP1802BCE\"");
    // remove_sym_from_lib(&lib,"\"P4080-BGA1295\"");
    // remove_sym_from_lib(&lib, "\"CDP1802ACE\"");

    print_kicad_sym_lib(f, lib);
    return 0;
}