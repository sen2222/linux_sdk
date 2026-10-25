#ifndef STATUS_BAR_H_
#define STATUS_BAR_H_


/* 在 LVGL 互斥锁内调用；状态栏随顶层对象销毁时自动清理定时器。 */
int app_status_bar_init(void);
void app_status_bar_deinit(void);


#endif
