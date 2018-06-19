//
// Created by 孙川 on 2018/6/14.
//

#ifndef MYSQL_HA_PLUGIN_H
#define MYSQL_HA_PLUGIN_H
#include <mysql/plugin.h>
#include <string>
#include <vector>
#include <mysql/mysql.h>

#define UUID_LENGTH (8+1+4+1+4+1+4+1+12)

using namespace std;


/*ha_plugin global variables*/
extern my_bool ha_plugin_ha_open;
extern my_bool ha_plugin_is_master;
extern char* ha_plugin_cluster_list;
extern char* ha_plugin_cluster_sip;
extern unsigned int ha_plugin_manager_port;
extern char* cluster_repl_user;
extern char* cluster_repl_password;
extern char* instance_host;

/*ha_plugin global status*/
extern char* ha_plugin_instance_status;
extern my_bool ha_plugin_ha_status;
extern char* ha_plugin_current_cluster_instance_list;

extern uint mysqld_port;


extern char server_uuid[UUID_LENGTH+1];


/*ha plugin init function*/
void* ha_plugin_init_func(void*);



#endif //MYSQL_HA_PLUGIN_H
