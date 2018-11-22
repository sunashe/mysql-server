//
// Created by 孙川 on 2018/10/22.
//

#ifndef MYSQL_SEMISYNC_SLAVE_PLUGIN_H
#define MYSQL_SEMISYNC_SLAVE_PLUGIN_H

//#include "semisync_slave.h"

int symisync_slave_init();


int handle_repl_semi_reset_slave(void *param);
int handle_repl_semi_slave_request_dump(void *param,
                                 uint32 flags);
int handle_repl_semi_slave_read_event(void *param,
                               const char *packet, unsigned long len,
                               const char **event_buf, unsigned long *event_len);
int handle_repl_semi_slave_queue_event(void *param,
                               const char *event_buf,
                               unsigned long event_len,
                               uint32 flags);

int handle_repl_semi_slave_io_start(void *param);

int handle_repl_semi_slave_io_end(void *param);

int handle_repl_semi_slave_sql_start(void *param);
int handle_repl_semi_slave_sql_stop(void *param, bool aborted);



#endif //MYSQL_SEMISYNC_SLAVE_PLUGIN_H
