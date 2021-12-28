#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include <math.h>
#include "thirdparty/json.h"
#include "thirdparty/hashmap.h"
#include "tzo.h"

Value *makeString(char *str)
{
    Value *value = malloc(sizeof *value);
    value->type = String;
    value->string_value = str;
    return value;
}

Value *makeNumber(float val)
{
    Value *value = malloc(sizeof *value);
    value->type = Number;
    value->number_value = val;
    return value;
}

void _push(TzoVM *vm, Value val)
{
    vm->stack[vm->stackSize] = val;
    vm->stackSize += 1;
}

Value _top(TzoVM *vm)
{
    Value val = vm->stack[vm->stackSize - 1];
    return val;
}

Value _pop(TzoVM *vm)
{
    Value val = vm->stack[vm->stackSize - 1];
    vm->stackSize -= 1;
    return val;
}

Value _popS(TzoVM *vm)
{
    Value val = _pop(vm);
    assert(val.type == String);
    return val;
}

float asInt_f(Value val)
{
    if (fabsf(roundf(val.number_value) - val.number_value) <= 0.00001f)
    {
        return roundf(val.number_value);
    }
    return val.number_value;
}

char *asString(Value val)
{
    if (val.type == String)
    {
        return val.string_value;
    }
    else
    {
        if (fabsf(roundf(val.number_value) - val.number_value) <= 0.00001f)
        {
            size_t needed = snprintf(NULL, 0, "%i", (int)val.number_value) + 1;
            char *buffer = malloc(needed);
            sprintf(buffer, "%i", (int)val.number_value);
            return buffer;
        }
        else
        {
            size_t needed = snprintf(NULL, 0, "%f", val.number_value) + 1;
            char *buffer = malloc(needed);
            sprintf(buffer, "%f", val.number_value);
            return buffer;
        }
    }
}

void i_plus(TzoVM *vm)
{
    Value a = _pop(vm);
    Value b = _pop(vm);
    Value *c = makeNumber(a.number_value + b.number_value);
    _push(vm, *c);
}

void i_min(TzoVM *vm)
{
    Value a = _pop(vm);
    Value b = _pop(vm);
    Value *c = makeNumber(a.number_value - b.number_value);
    _push(vm, *c);
}

void i_mul(TzoVM *vm)
{
    Value a = _pop(vm);
    Value b = _pop(vm);
    Value *c = makeNumber(a.number_value * b.number_value);
    _push(vm, *c);
}

void i_nop(TzoVM *vm)
{
}

void i_br_close(TzoVM *vm);

void i_br_open(TzoVM *vm)
{
    int i = 1;
    int pc = vm->ppc + 1;
    while (pc < vm->programSize)
    {
        if (vm->program[pc].type == invoke_function && vm->program[pc].function_pointer == &i_br_open)
        {
            i += 1;
        }
        else if (vm->program[pc].type == invoke_function && vm->program[pc].function_pointer == &i_br_close)
        {
            i -= 1;
            if (i == 0)
            {
                // found it!
                vm->ppc = pc;
                return;
            }
        }
        pc += 1;
    }
}

void i_br_close(TzoVM *vm)
{
    // nop
}

void i_pop(TzoVM *vm)
{
    _pop(vm);
}

void i_stdout(TzoVM *vm)
{
    Value a = _pop(vm);
    if (a.type == Number)
    {
        printf("%f", a.number_value);
    }
    if (a.type == String)
    {
        printf("%s", a.string_value);
    }
}

void i_concat(TzoVM *vm)
{
    Value a = _pop(vm);
    Value b = _pop(vm);
    char* aStr = asString(a);
    char* bStr = asString(b);
    size_t len1 = strlen(aStr), len2 = strlen(bStr);
    char *concat = (char*) malloc(len1 + len2 + 1);

    memcpy(concat, aStr, len1);
    memcpy(concat+len1, bStr, len2+1);

    Value *c = makeString(concat);
    _push(vm, *c);
}

void i_rconcat(TzoVM *vm)
{
    Value a = _pop(vm);
    Value b = _pop(vm);
    char* aStr = asString(a);
    char* bStr = asString(b);
    size_t len1 = strlen(aStr), len2 = strlen(bStr);
    char *concat = (char*) malloc(len1 + len2 + 1);

    memcpy(concat, bStr, len2);
    memcpy(concat+len2, aStr, len1+1);

    Value *c = makeString(concat);
    _push(vm, *c);
}

void i_charCode(TzoVM *vm)
{
    Value a = _pop(vm);
    size_t needed = snprintf(NULL, 0, "%c", (int)a.number_value) + 1;
    char *buffer = malloc(needed);
    sprintf(buffer, "%c", (int)a.number_value);
    Value *b = makeString(buffer);
    _push(vm, *b);
}

void i_randInt(TzoVM *vm)
{
    Value a = _pop(vm);
    int i = rand() % (int)a.number_value;
    Value *b = makeNumber((float)i);
    _push(vm, *b);
}

void i_eq(TzoVM *vm)
{
    Value a = _pop(vm);
    Value b = _pop(vm);
    if (a.type != b.type)
    {
        _push(vm, *makeNumber(0));
    }
    else if (a.type == String)
    {
        if (strcmp(a.string_value, b.string_value) == 0)
        {
            _push(vm, *makeNumber(1));
        }
        else
        {
            _push(vm, *makeNumber(0));
        }
    }
    else if (a.type == Number)
    {
        if (a.number_value == b.number_value)
        {
            _push(vm, *makeNumber(1));
        }
        else
        {
            _push(vm, *makeNumber(0));
        }
    }
}

void i_and(TzoVM *vm)
{
    Value a = _pop(vm);
    Value b = _pop(vm);
    if (a.type == Number && b.type == Number)
    {
        if (a.number_value == 0 || b.number_value == 0)
        {
            _push(vm, *makeNumber(0));
            return;
        }
        else
        {
            _push(vm, *makeNumber(1));
            return;
        }
    }
    _push(vm, *makeNumber(0));
}

void i_dup(TzoVM *vm)
{
    Value a = _pop(vm);
    if (a.type == String)
    {
        _push(vm, *makeString(a.string_value));
        _push(vm, a);
    }
    else if (a.type == Number)
    {
        _push(vm, *makeNumber(a.number_value));
        _push(vm, a);
    }
}

void i_gt(TzoVM *vm)
{
    Value a = _pop(vm);
    Value b = _pop(vm);
    if (a.type == Number && b.type == Number)
    {
        if (a.number_value > b.number_value)
        {
            _push(vm, *makeNumber(1));
            return;
        }
        else
        {
            _push(vm, *makeNumber(0));
            return;
        }
    }
    _push(vm, *makeNumber(0));
}

void i_lt(TzoVM *vm)
{
    Value a = _pop(vm);
    Value b = _pop(vm);
    if (a.type == Number && b.type == Number)
    {
        if (a.number_value < b.number_value)
        {
            _push(vm, *makeNumber(1));
            return;
        }
        else
        {
            _push(vm, *makeNumber(0));
            return;
        }
    }
    _push(vm, *makeNumber(0));
}

void i_jgz(TzoVM *vm)
{
    Value a = _pop(vm);
    assert(("jgz: value must be number", a.type == Number));
    if (asInt_f(a) > 0)
    {
        vm->ppc += 1;
    }
}

void i_jz(TzoVM *vm)
{
    Value a = _pop(vm);
    assert(("jz: value must be number", a.type == Number));
    if (asInt_f(a) == 0)
    {
        vm->ppc += 1;
    }
}

void i_or(TzoVM *vm)
{
    Value a = _pop(vm);
    Value b = _pop(vm);
    if (a.type == Number && b.type == Number)
    {
        if (a.number_value == 0 && b.number_value == 0)
        {
            _push(vm, *makeNumber(0));
            return;
        }
        else
        {
            _push(vm, *makeNumber(1));
            return;
        }
    }
    _push(vm, *makeNumber(0));
}

void i_not(TzoVM *vm)
{
    Value a = _pop(vm);
    if (a.type == Number)
    {
        if (a.number_value == 0)
        {
            _push(vm, *makeNumber(1));
            return;
        }
        else
        {
            _push(vm, *makeNumber(0));
            return;
        }
    }
    _push(vm, *makeNumber(0));
}

void i_ppc(TzoVM *vm)
{
    _push(vm, *makeNumber((float)vm->ppc));
}

void i_stacksize(TzoVM *vm)
{
    _push(vm, *makeNumber((float)vm->stackSize));
}

void i_pause(TzoVM *vm)
{
    vm->running = false;
}

void i_exit(TzoVM *vm)
{
    vm->running = false;
    vm->exited = true;
}

void i_goto(TzoVM *vm)
{
    Value a = _pop(vm);
    if (a.type == Number)
    {
        int i = (int)(asInt_f(a)) - 1;
        vm->ppc = i;
    }
    else if (a.type == String)
    {
        int i = hashmap_get(&(vm->labelmap), a.string_value, strlen(a.string_value));
        vm->ppc = i - 1;
    }
}

void i_setContext(TzoVM *vm)
{
    Value a = _pop(vm);
    Value b = _pop(vm);
    char *key = a.string_value;
    if (b.type == String)
    {
        hashmap_put(&(vm->context), key, strlen(key), makeString(b.string_value));
    }
    else if (b.type == Number)
    {
        hashmap_put(&(vm->context), key, strlen(key), makeNumber(b.number_value));
    }
}

void i_getContext(TzoVM *vm)
{
    Value a = _pop(vm);
    char *key = a.string_value;
    Value *val = (Value *)hashmap_get(&(vm->context), a.string_value, strlen(a.string_value));
    _push(vm, *val);
}

void i_hasContext(TzoVM *vm)
{
    Value a = _pop(vm);
    char *key = a.string_value;
    void *const element = hashmap_get(&(vm->context), a.string_value, strlen(a.string_value));
    if (element == NULL)
    {
        _push(vm, *makeNumber(0));
    }
    else
    {
        _push(vm, *makeNumber(1));
    }
}

void i_delContext(TzoVM *vm)
{
    Value a = _pop(vm);
    char *key = a.string_value;
    hashmap_remove(&(vm->context), a.string_value, strlen(a.string_value));
}

struct json_value_s *loadFileGetJSON(TzoVM *vm, char *filename)
{
    FILE *f = fopen(filename, "rb");
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *inputjson = malloc(fsize + 1);
    fread(inputjson, 1, fsize, f);
    fclose(f);

    inputjson[fsize] = 0;
    struct json_value_s *root = json_parse(inputjson, strlen(inputjson));
    return root;
}

void initRuntime(TzoVM *vm)
{
    const unsigned initial_size = 128;
    if (0 != hashmap_create(initial_size, &(vm->labelmap)))
    {
        assert(("failed to create label hashmap", 0));
    }
    if (0 != hashmap_create(initial_size, &(vm->context)))
    {
        assert(("failed to create context hashmap", 0));
    }
    if (0 != hashmap_create(initial_size, &(vm->foreignFunctions)))
    {
        assert(("failed to create foreignFunctions hashmap", 0));
    }
}

void initLabelMapFromJSONObject(TzoVM *vm, struct json_object_s *obj)
{
    for (struct json_object_element_s *s = obj->start; s != NULL; s = s->next)
    {
        const char *key = s->name->string;
        if (json_value_as_number(s->value) != NULL)
        {
            struct json_number_s *n = json_value_as_number(s->value);
            float f = strtof(n->number, NULL);
            int i = (int)f;
            hashmap_put(&(vm->labelmap), key, strlen(key), i);
        }
    }
}

void initProgramListFromJSONArray(TzoVM *vm, struct json_array_s *array)
{
    vm->program = malloc(array->length * sizeof(*vm->program));
    vm->programSize = array->length;
    int piPointer = 0;
    for (struct json_array_element_s *elem = array->start; elem != NULL; elem = elem->next)
    {

        // set defaults:
        vm->program[piPointer].type = invoke_function;
        vm->program[piPointer].function_pointer = &i_nop;

        struct json_object_s *obj = json_value_as_object(elem->value);
        for (struct json_object_element_s *oe = obj->start; oe != NULL; oe = oe->next)
        {
            if (0 == strcmp(oe->name->string, "type"))
            {
                if (0 == strcmp(json_value_as_string(oe->value)->string, "push-number-instruction"))
                {
                    vm->program[piPointer].type = push_number;
                }
                if (0 == strcmp(json_value_as_string(oe->value)->string, "push-string-instruction"))
                {
                    vm->program[piPointer].type = push_string;
                }
                if (0 == strcmp(json_value_as_string(oe->value)->string, "invoke-function-instruction"))
                {
                    vm->program[piPointer].type = invoke_function;
                }
            }
            else if (0 == strcmp(oe->name->string, "label"))
            {
                char *key = json_value_as_string(oe->value)->string;
                hashmap_put(&(vm->labelmap), key, strlen(key), piPointer);
            }
            else if (0 == strcmp(oe->name->string, "value"))
            {
                if (json_value_as_string(oe->value) != NULL)
                {
                    struct json_string_s *s = json_value_as_string(oe->value);
                    char *str = (char *)malloc((s->string_size + 1) * sizeof(*str));
                    strncpy(str, s->string, s->string_size);
                    str[s->string_size] = 0;
                    vm->program[piPointer].value = makeString(str);
                }
                if (json_value_as_number(oe->value) != NULL)
                {
                    Value *v = makeNumber(atof(json_value_as_number(oe->value)->number));
                    vm->program[piPointer].value = v;
                }
            }
            else if (0 == strcmp(oe->name->string, "functionName"))
            {
                bind_function(vm, "nop", &i_nop);
                bind_function(vm, "plus", &i_plus);
                bind_function(vm, "+", &i_plus);
                bind_function(vm, "min", &i_min);
                bind_function(vm, "-", &i_min);
                bind_function(vm, "mul", &i_mul);
                bind_function(vm, "*", &i_mul);
                bind_function(vm, "pop", &i_pop);
                bind_function(vm, "stdout", &i_stdout);
                bind_function(vm, "concat", &i_concat);
                bind_function(vm, "rconcat", &i_rconcat);
                bind_function(vm, "charCode", &i_charCode);
                bind_function(vm, "randInt", &i_randInt);
                bind_function(vm, "eq", &i_eq);
                bind_function(vm, "and", &i_and);
                bind_function(vm, "dup", &i_dup);
                bind_function(vm, "gt", &i_gt);
                bind_function(vm, "lt", &i_lt);
                bind_function(vm, "not", &i_not);
                bind_function(vm, "or", &i_or);
                bind_function(vm, "ppc", &i_ppc);
                bind_function(vm, "stacksize", &i_stacksize);
                bind_function(vm, "jz", &i_jz);
                bind_function(vm, "jgz", &i_jgz);
                bind_function(vm, "{", &i_br_open);
                bind_function(vm, "}", &i_br_close);
                bind_function(vm, "pause", &i_pause);
                bind_function(vm, "exit", &i_exit);
                bind_function(vm, "goto", &i_goto);
                bind_function(vm, "setContext", &i_setContext);
                bind_function(vm, "getContext", &i_getContext);
                bind_function(vm, "hasContext", &i_hasContext);
                bind_function(vm, "delContext", &i_delContext);

                void *const element = hashmap_get(&(vm->foreignFunctions), json_value_as_string(oe->value)->string, strlen(json_value_as_string(oe->value)->string));
                if (element != NULL)
                {
                    vm->program[piPointer].function_pointer = element;
                }
            }
        }
        piPointer += 1;
    }
}

void tzo_step(TzoVM *vm)
{
    if (vm->program[vm->ppc].type == push_string)
    {
        // printf("->%s\n", program[ppc].value->string_value);
        _push(vm, *makeString(vm->program[vm->ppc].value->string_value));
    }
    if (vm->program[vm->ppc].type == push_number)
    {
        // printf("->%f\n", program[ppc].value->number_value);
        _push(vm, *makeNumber(vm->program[vm->ppc].value->number_value));
    }
    if (vm->program[vm->ppc].type == invoke_function)
    {
        // printf("->FUNC\n");
        vm->program[vm->ppc].function_pointer(vm);
    }
    vm->ppc = vm->ppc + 1;
}

void tzo_run(TzoVM *vm)
{
    vm->running = true;
    while (vm->running && vm->ppc < vm->programSize)
    {
        tzo_step(vm);
    }
    vm->running = false;
}

void registerForeignFunction(TzoVM *vm, char *name, void *func)
{
    hashmap_put(&(vm->foreignFunctions), name, strlen(name), func);
}

void tzo_pause(TzoVM *vm)
{
    i_pause(vm);
}

void tzo_resume(TzoVM *vm)
{
    vm->running = true;
}

TzoVM *createTzoVM()
{
    TzoVM *vm = malloc(sizeof(*vm));
    vm->ppc = 0;
    vm->exited = false;
    vm->running = false;
    vm->stack = malloc(TZO_MAX_STACK_SIZE * sizeof(*vm->stack));
    vm->stackSize = 0;
    vm->program = malloc(128 * sizeof(*vm->program));
    vm->programSize = 0;
    return vm;
}