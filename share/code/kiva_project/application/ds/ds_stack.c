#include <stdlib.h>

#include "ds_stack.h"

struct ds_stack_node
{
    struct ds_stack_node *next;
    void *data;
};

struct ds_stack
{
    struct ds_stack_node *top;
    size_t size;
};

DS_STACK_S *ds_stack_create(void)
{
    DS_STACK_S *stack;

    stack = malloc(sizeof(*stack));
    if (stack == NULL)
        return NULL;

    stack->top = NULL;
    stack->size = 0;
    return stack;
}

void ds_stack_destroy(DS_STACK_S *stack)
{
    if (stack == NULL)
        return;

    ds_stack_clear(stack);
    free(stack);
}

int ds_stack_push(DS_STACK_S *stack, void *data)
{
    struct ds_stack_node *node;

    if (stack == NULL || data == NULL || stack->size == SIZE_MAX)
        return -1;

    node = malloc(sizeof(*node));
    if (node == NULL)
        return -1;

    node->data = data;
    node->next = stack->top;
    stack->top = node;
    stack->size++;
    return 0;
}

void *ds_stack_pop(DS_STACK_S *stack)
{
    struct ds_stack_node *node;
    void *data;

    if (stack == NULL || stack->top == NULL)
        return NULL;

    node = stack->top;
    data = node->data;
    stack->top = node->next;
    stack->size--;
    free(node);
    return data;
}

void *ds_stack_top(const DS_STACK_S *stack)
{
    return stack != NULL && stack->top != NULL ? stack->top->data : NULL;
}

void ds_stack_clear(DS_STACK_S *stack)
{
    struct ds_stack_node *node;

    if (stack == NULL)
        return;

    while (stack->top != NULL)
    {
        node = stack->top;
        stack->top = node->next;
        free(node);
    }
    stack->size = 0;
}

size_t ds_stack_size(const DS_STACK_S *stack)
{
    return stack != NULL ? stack->size : 0;
}

int ds_stack_is_empty(const DS_STACK_S *stack)
{
    return stack == NULL || stack->top == NULL;
}
