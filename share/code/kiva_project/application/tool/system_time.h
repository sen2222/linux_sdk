#ifndef __SYSTEM_TIME_H__
#define __SYSTEM_TIME_H__

#define SYSTEM_TIME_URL             "http://acs.m.taobao.com/gw/mtop.common.getTimestamp/"

/* 创建其他线程前初始化；默认使用北京时间显示。 */
int system_time_init(void);
/* 请求网络时间并校准CLOCK_REALTIME，成功返回0，失败返回-1。 */
int system_time_sync(void);
int system_time_get_sync_flag(void);













#endif
