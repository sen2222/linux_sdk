#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ds_stack.h"

static size_t fail_malloc_call;
static size_t malloc_calls;

void *__real_malloc(size_t size);

void *__wrap_malloc(size_t size)
{
    malloc_calls++;
    if (fail_malloc_call != 0 && malloc_calls == fail_malloc_call)
        return NULL;
    return __real_malloc(size);
}

static void test_lifo_and_identity(void)
{
    DS_STACK_S *stack = ds_stack_create();
    int values[4096];
    int i;
    assert(stack);
    assert(ds_stack_is_empty(stack));
    assert(ds_stack_size(stack) == 0);
    assert(ds_stack_top(stack) == NULL);
    assert(ds_stack_pop(stack) == NULL);
    for (i = 0; i < 4096; i++) {
        values[i] = i;
        assert(ds_stack_push(stack, &values[i]) == 0);
    }
    assert(ds_stack_size(stack) == 4096);
    assert(ds_stack_top(stack) == &values[4095]);
    values[4095] = -7;
    assert(*(int *)ds_stack_top(stack) == -7);
    assert(ds_stack_size(stack) == 4096);
    for (i = 4095; i >= 0; i--)
        assert(ds_stack_pop(stack) == &values[i]);
    assert(ds_stack_is_empty(stack));
    for (i = 0; i < 4096; i++)
        assert(ds_stack_push(stack, &values[i]) == 0);
    ds_stack_clear(stack);
    assert(ds_stack_is_empty(stack) && ds_stack_size(stack) == 0);
    for (i = 0; i < 4096; i++)
        assert(ds_stack_push(stack, &values[i]) == 0);
    ds_stack_destroy(stack);
    assert(values[4095] == -7);
}

static void test_caller_owns_data(void)
{
    struct payload { int id; char name[13]; } item = {7, "original"};
    DS_STACK_S *stack = ds_stack_create();
    char *heap_data = malloc(32);
    assert(stack && heap_data);
    strcpy(heap_data, "caller owned");
    assert(ds_stack_push(stack, &item) == 0);
    strcpy(item.name, "changed");
    assert(ds_stack_top(stack) == &item);
    assert(strcmp(((struct payload *)ds_stack_pop(stack))->name, "changed") == 0);
    assert(ds_stack_push(stack, heap_data) == 0);
    ds_stack_clear(stack);
    assert(strcmp(heap_data, "caller owned") == 0);
    assert(ds_stack_push(stack, heap_data) == 0);
    ds_stack_destroy(stack);
    assert(strcmp(heap_data, "caller owned") == 0);
    free(heap_data);
}

static void test_invalid_arguments(void)
{
    int value = 1;
    DS_STACK_S *stack = ds_stack_create();
    assert(stack);
    assert(ds_stack_push(NULL, &value) == -1);
    assert(ds_stack_push(stack, NULL) == -1);
    assert(ds_stack_pop(NULL) == NULL);
    assert(ds_stack_top(NULL) == NULL);
    assert(ds_stack_size(NULL) == 0);
    assert(ds_stack_is_empty(NULL));
    ds_stack_clear(NULL);
    ds_stack_destroy(NULL);
    ds_stack_destroy(stack);
}

static void test_allocation_failures(void)
{
    DS_STACK_S *stack;
    int values[] = {3, 7, 9};
    malloc_calls = 0;
    fail_malloc_call = 1;
    assert(ds_stack_create() == NULL);
    fail_malloc_call = 0;
    stack = ds_stack_create();
    assert(stack);
    assert(ds_stack_push(stack, &values[0]) == 0);
    assert(ds_stack_push(stack, &values[1]) == 0);
    fail_malloc_call = malloc_calls + 1;
    assert(ds_stack_push(stack, &values[2]) == -1);
    assert(ds_stack_size(stack) == 2);
    assert(ds_stack_top(stack) == &values[1]);
    fail_malloc_call = 0;
    assert(ds_stack_push(stack, &values[2]) == 0);
    assert(ds_stack_pop(stack) == &values[2]);
    assert(ds_stack_pop(stack) == &values[1]);
    assert(ds_stack_pop(stack) == &values[0]);
    fail_malloc_call = malloc_calls + 1;
    assert(ds_stack_push(stack, &values[0]) == -1);
    assert(ds_stack_is_empty(stack));
    fail_malloc_call = 0;
    assert(ds_stack_push(stack, &values[0]) == 0);
    ds_stack_destroy(stack);
}

int main(void)
{
    test_lifo_and_identity();
    test_caller_owns_data();
    test_invalid_arguments();
    test_allocation_failures();
    puts("ds_stack: all tests passed");
    return 0;
}
