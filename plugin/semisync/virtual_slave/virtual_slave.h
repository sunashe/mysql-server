//
// Created by 孙川 on 2018/11/21.
//

#ifndef MYSQL_VIRTUAL_SLAVE_H
#define MYSQL_VIRTUAL_SLAVE_H
#include <mysql/mysql.h>
#include <string.h>

char* report_host = strdup("127.0.0.1");
char* report_password = strdup("ashe");
char* report_user = strdup("ashe");
uint report_port = 3239;
uint heartbeat_period = 15;
uint get_start_gtid_mode;
uint net_read_time_out;

int register_slave_on_master(MYSQL* mysql,bool *suppress_warnings);
int set_heartbeat_period(MYSQL* mysql);
int set_slave_uuid(MYSQL* mysql);
char* string_to_char(string str);

#endif //MYSQL_VIRTUAL_SLAVE_H
