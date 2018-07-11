//
// Created by 孙川 on 2018/6/14.
//

//
// Created by 孙川 on 2018/4/26.
//
#include "ha_plugin.h"
#include <mysql/plugin.h>
#include<mysql_version.h>
#include <my_global.h>
#include <my_sys.h>
#include<sys/resource.h>
#include <sys/time.h>
#include <pthread.h>
#include <mysqld_thd_manager.h>
#include "log.h"
#include "message.h"
#include "master.h"
#include "slave.h"
#include <string.h>
#include "triangle.h"



#define MONITORING_BUFFER 1024

/*global  cluster for master and slave*/
vector<data_node> ha_cluster;
pthread_mutex_t m_ha_cluster;

vector<data_node> clients_cluster;
pthread_mutex_t m_clients_cluster;

/*global instance of myself*/
data_node instance_me;

/*used for master instance*/
master Master;

/*used for slave instance*/
slave Slave;

/*global vector triangle*/
vector<triangle> vec_triangles;
pthread_mutex_t m_vec_triangles;

/* global information*/



/*ha_plugin global variables list*/
my_bool ha_open = false;      //表示ha是否开启
my_bool is_master = false;    //表示此实例是否是master
char* cluster_list = NULL;   //表示此cluster内的ip:port
char* cluster_sip = NULL;    //表示cluster对外提供服务的ip地址
unsigned int manager_port = 23308; //ha_plugin的服务端口
char* cluster_repl_user = NULL;              //复制用户
char* cluster_repl_password = NULL;//复制用户密码
char* instance_host = NULL;
unsigned int slave_promote = 1;
unsigned int waiting_for_slave_replay_seconds=5;

/*创建系统变量，可以通过配置文件或set global来修改*/
MYSQL_SYSVAR_BOOL(ha_open,ha_open,PLUGIN_VAR_OPCMDARG,"open or close ha when ha_plugin init",NULL, NULL, FALSE);
MYSQL_SYSVAR_BOOL(is_master,is_master,PLUGIN_VAR_OPCMDARG,"master or not when ha_plugin init",NULL, NULL, FALSE);
MYSQL_SYSVAR_STR(cluster_list,cluster_list,PLUGIN_VAR_OPCMDARG|PLUGIN_VAR_MEMALLOC,"ha cluster list",NULL,NULL,NULL);
MYSQL_SYSVAR_STR(cluster_sip,cluster_sip,PLUGIN_VAR_OPCMDARG|PLUGIN_VAR_MEMALLOC,"ha cluster sip",NULL,NULL,"10.211.55.202");
MYSQL_SYSVAR_UINT(manager_port,manager_port,PLUGIN_VAR_OPCMDARG,"ha plugin manager port",NULL,NULL,23308,23307,65535,0);
MYSQL_SYSVAR_UINT(slave_promote,slave_promote,PLUGIN_VAR_OPCMDARG,"slave promote level,0:never promote;1:promote when no master;2:promote when no master when init",NULL,NULL,1,0,2,1);
MYSQL_SYSVAR_UINT(waiting_for_slave_replay_seconds,waiting_for_slave_replay_seconds,PLUGIN_VAR_OPCMDARG,"waiting for slave replay seconds",NULL,NULL,5,0,10000,1);
MYSQL_SYSVAR_STR(cluster_repl_user,cluster_repl_user,PLUGIN_VAR_OPCMDARG|PLUGIN_VAR_MEMALLOC,"cluster repl user",NULL,NULL,"repl");
MYSQL_SYSVAR_STR(cluster_repl_password,cluster_repl_password,PLUGIN_VAR_OPCMDARG|PLUGIN_VAR_MEMALLOC,"cluster repl password",NULL,NULL,"repl");
MYSQL_SYSVAR_STR(instance_host,instance_host,PLUGIN_VAR_OPCMDARG|PLUGIN_VAR_MEMALLOC,"this instance host",NULL,NULL,"127.0.0.1");

//
//static MYSQL_SYSVAR_INT(
//        flow_control_certifier_threshold,     /* name */
//        flow_control_certifier_threshold_var, /* var */
//        PLUGIN_VAR_OPCMDARG,                  /* optional var */
//        "Specifies the number of waiting transactions that will trigger "
//                "flow control. Default: 25000",
//        NULL,                                 /* check func. */
//        NULL,                                 /* update func. */
//        DEFAULT_FLOW_CONTROL_THRESHOLD,       /* default */
//        MIN_FLOW_CONTROL_THRESHOLD,           /* min */
//        MAX_FLOW_CONTROL_THRESHOLD,           /* max */
//        0                                     /* block */
//);

struct st_mysql_sys_var* vars_system_var[] = {

        MYSQL_SYSVAR(ha_open),
        MYSQL_SYSVAR(is_master),
        MYSQL_SYSVAR(cluster_list),
        MYSQL_SYSVAR(cluster_sip),
        MYSQL_SYSVAR(slave_promote),
        MYSQL_SYSVAR(manager_port),
        MYSQL_SYSVAR(waiting_for_slave_replay_seconds),
        MYSQL_SYSVAR(cluster_repl_user),
        MYSQL_SYSVAR(cluster_repl_password),
        MYSQL_SYSVAR(instance_host),

        NULL
};

/*ha_plugin global status*/

char* ha_plugin_instance_status;
my_bool ha_plugin_ha_status;
char* ha_plugin_current_cluster_instance_list;

static st_mysql_show_var sys_status_var[] =

        {
                //{"monitor_num", (char *)&monitor_num, SHOW_LONG},
                {"ha_plugin_instance_status", (char *)ha_plugin_instance_status, SHOW_CHAR},
                {"ha_plugin_ha_status", (char *)&ha_plugin_ha_status, SHOW_BOOL},
                {"monitor_num", (char *)ha_plugin_current_cluster_instance_list, SHOW_CHAR},
                NULL

        };






/*系统启动或加载插件时时调用该函数，用于创建后台线程*/

pthread_t thread_ha_plugin_init_func;

static int ha_plugin_init(void*p)
{

        pthread_create(&thread_ha_plugin_init_func,NULL,ha_plugin_init_func,NULL);
    return 0;
}


/*卸载插件时调用*/

static int ha_plugin_deinit(void *p)
{
    if(is_master)
    {
        Master.stop();
    }
    else
    {
        Slave.stop();
    }

    pthread_cancel(thread_ha_plugin_init_func);
    pthread_join(thread_ha_plugin_init_func,NULL);
    return 0;
}



struct st_mysql_daemon ha_plugin = { MYSQL_DAEMON_INTERFACE_VERSION };

/*声明插件*/
mysql_declare_plugin(monitoring)
                {
                        MYSQL_DAEMON_PLUGIN,

                        &ha_plugin,

                        "ha_plugin",

                        "sunashe",

                        "ha_plugin",

                        PLUGIN_LICENSE_GPL,

                        ha_plugin_init,

                        ha_plugin_deinit,

                        0x0100,

                        sys_status_var,

                        vars_system_var,
                        NULL
                }
        mysql_declare_plugin_end;




void* ha_plugin_init_func(void*)
{
    //在启动阶段，如果初始化参数ha_plugin_ha_open是off状态，在一直等到开启
    pthread_mutex_init(&m_vec_triangles,NULL);
    pthread_mutex_init(&m_clients_cluster,NULL);
    pthread_mutex_init(&m_ha_cluster,NULL);
    bool print_waiting_log =false;

    waiting_for_ha_open:
    do
    {
        if(!print_waiting_log) {
            sql_print_information("waiting for ha_open on");
            print_waiting_log = true;
        }
        usleep(10000);
    }while(!ha_open);

    sql_print_information("ha plugin init...");
    memcpy(instance_me.host,instance_host,strlen(instance_host)+1);
    instance_me.port = mysqld_port;
    memcpy(instance_me.uuid,server_uuid,strlen(server_uuid));
    ha_cluster.push_back(instance_me);

    for(;;) //考虑如下四个过程，1，初始化为主实例；2，初始化为从实例；3，主实例切换为从实例；4，从实例切换为主实例
    {
        if(!ha_open)
        {
            goto waiting_for_ha_open;
        }

        if(is_master) //is a master
        {
            instance_me.dataNodeStatus = MASTER;
            Master.run();
        }
        else  //is a slave
        {
            instance_me.dataNodeStatus = SLAVE;
            Slave.run();
        }
    }

    return NULL;
}





void* thread_func_interaction(void* argv)
{

    return NULL;
}


triangle find_triangle(const data_node* tri_l,const data_node* tri_r,const data_node* tri_cli)
{
    vector<triangle>::const_iterator it_vec_triangle;
    for(it_vec_triangle = vec_triangles.begin();it_vec_triangle != vec_triangles.end();it_vec_triangle++)
    {
        if(it_vec_triangle->compare_triangle(tri_l,tri_r,tri_cli))
        {
            triangle triangle1 = *it_vec_triangle;
            return triangle1;
        }
    }
}