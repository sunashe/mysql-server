//
// Created by 孙川 on 2018/6/14.
//

#ifndef MYSQL_HA_PLUGIN_H
#define MYSQL_HA_PLUGIN_H
#include <mysql/plugin.h>
#include <string>
#include <vector>
#include <mysql/mysql.h>
#include "triangle.h"
#include <pthread.h>


#define UUID_LENGTH (8+1+4+1+4+1+4+1+12)

using namespace std;


/*ha_plugin global variables*/
extern my_bool ha_open;
extern my_bool is_master;
extern char* cluster_list;
extern char* cluster_sip;
extern unsigned int manager_port;
extern char* cluster_repl_user;
extern char* cluster_repl_password;
extern char* instance_host;
extern unsigned int slave_promote;
extern unsigned int waiting_for_slave_replay_seconds;

/*ha_plugin global status*/
extern char* ha_plugin_instance_status;
extern my_bool ha_plugin_ha_status;
extern char* ha_plugin_current_cluster_instance_list;

extern uint mysqld_port;


extern char server_uuid[UUID_LENGTH+1];

extern data_node instance_me;

extern vector<data_node> ha_cluster;
extern pthread_mutex_t m_ha_cluster;

extern vector<data_node> clients_cluster;
extern pthread_mutex_t m_clients_cluster;

extern vector<triangle> vec_triangles;
extern pthread_mutex_t m_vec_triangles;

/*ha plugin init function*/
void* ha_plugin_init_func(void*);




triangle find_triangle(const data_node*,const data_node*,const data_node*);

#endif //MYSQL_HA_PLUGIN_H
