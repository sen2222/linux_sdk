#ifndef __DS_STACK_H__
#define __DS_STACK_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 链表栈只保存用户数据指针；仅管理栈对象和节点，不分配、复制或释放用户数据。 */
/* 用户需保证数据在栈内期间有效；多线程访问需在外部加锁。 */
typedef struct ds_stack DS_STACK_S;

/* 创建失败返回 NULL。 */
DS_STACK_S *ds_stack_create(void);
void ds_stack_destroy(DS_STACK_S *stack);

/* 压栈成功返回 0，参数错误或节点分配失败返回 -1，失败时栈内容不变。 */
int ds_stack_push(DS_STACK_S *stack, void *data); /* data 不能为 NULL。 */
void *ds_stack_pop(DS_STACK_S *stack); /* 返回用户数据指针并释放栈顶节点，空栈返回 NULL。 */
void *ds_stack_top(const DS_STACK_S *stack); /* 返回用户数据指针，不出栈，空栈返回 NULL。 */

/* 清空并释放全部节点，保留栈对象，不释放用户数据；允许传入 NULL。 */
void ds_stack_clear(DS_STACK_S *stack);
size_t ds_stack_size(const DS_STACK_S *stack); /* NULL 返回 0。 */
int ds_stack_is_empty(const DS_STACK_S *stack); /* 空栈或 NULL 返回 1，否则返回 0。 */

#ifdef __cplusplus
}
#endif

#endif
