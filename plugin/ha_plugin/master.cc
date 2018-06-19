//
// Created by 孙川 on 2018/6/15.
//

#include "master.h"
#include "ha_plugin.h"
#include "log.h"

extern char server_uuid[UUID_LENGTH+1];



bool master::check_sip_bound_me(char *sip, bool& bind_others) {

    bool r = false;
    MYSQL* conn;
    conn=mysql_init(0);
    if(mysql_real_connect(conn,sip,cluster_repl_user,cluster_repl_password,NULL,mysqld_port,NULL,0) != NULL)
    {
        if(mysql_query(conn,"select @@uuid") == 0)
        {
            MYSQL_RES* res;
            MYSQL_ROW  row;
            res=mysql_store_result(conn);
            row=mysql_fetch_row(res);
            if(row[0] != NULL)
            {
                if(strcmp(row[0],server_uuid) == 0)
                {
                    r=true;
                    bind_others = false;
                }
                else
                {
                    r = false;
                    bind_others = true;
                }
            }
            else
            {
                //log 获取到的server_uuid 为NULL 不正常的情况
                r=false;
            }
        }
        else
        {
            //获取server_uuid失败，有可能是权限问题,log it
            r=false;
        }
    }
    else  //通过数据库链接的方式检测失败，通过ping检测
    {

    }

    return r;
}

bool master::bind_sip(char *sip) {
    bool res = false;


    //

    return res;

}


void* master::thread_func_waiting_for_connection(void *arg) {

    fd_set cluster_fd_set;
    struct timeval tv;
    int retval, maxfd;
    int ss = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_sockaddr;
    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_port = htons(ha_plugin_manager_port);
    server_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if(bind(ss, (struct sockaddr* )&server_sockaddr, sizeof(server_sockaddr)) == -1)
    {
        perror("bind");
        exit(1);
    }

    if(listen(ss, 20/* todo config it*/) == -1)
    {
        perror("listen");
        exit(1);
    }

    while(1)
    {
        struct sockaddr_in client_addr;
        socklen_t length = sizeof(client_addr);
        int conn = accept(ss, (struct sockaddr *) &client_addr, &length);
        if( conn < 0 ) {
            perror("connect"); //todo log
            exit(1);
        }

        {
            //如果不是从机，而是ha命令.这些命令可以通过mysql中的set来解决。

        }
        char* host=inet_ntoa(client_addr.sin_addr);
        data_node tmp_instance;
        memcpy(tmp_instance.host,host,strlen(host));
        tmp_instance.conn=conn;
        ha_cluster.push_back(tmp_instance);//li.push_back(conn);
    }

    //ha_plugin_ha_open = 0/NO todo send this to slave.
    return NULL;
}


void* master::thread_func_collection(void*){

    MYSQL* conn;
    MYSQL_RES* res;
    MYSQL_ROW row;
    conn = mysql_init(0);
    if(mysql_real_connect(conn,instance_host,cluster_repl_user,cluster_repl_password,NULL,mysqld_port,NULL,0) == NULL)
    {
        //connection error todo log it
    }

    while(1)
    {
        //collect ** and send to others.

        /* 1，收集当前链接最多的ip地址
         * 2，收集历史链接最多的ip地址
         * 3，收集GTID信息
         *
         */
        char* query_for_most_current_connections = NULL;
        char* query_for_most_total_connections = NULL;
        char* query_for_gtid = NULL;

        sprintf(query_for_most_current_connections,"select * from performance_schema.accounts "
                "where USER is not null and HOST is not null and host not in ('127.0.0.1','localhost','%s')"
                " order by CURRENT_CONNECTIONS desc limit 1",instance_host);

        if(mysql_query(conn,query_for_most_current_connections) == 0)
        {
            res = mysql_store_result(conn);
            if(res->row_count > 0)
            {
                row = mysql_fetch_row(res);
                if(row[0] != NULL)
                {

                }
            }
            else
            {
                //todo log it and check performance_schema is on.
            }
        }

        if(send_m_to_others())
        {

        }
        else
        {

        }

    }

    return NULL;

}

bool master::send_m_to_others() {

    char* send_buff = NULL;
    memcpy(send_buff,clientsConnections,sizeof(m_clients_connections));
    vector<data_node>::iterator it_cluster;
    for(it_cluster = ha_cluster.begin(); it_cluster != ha_cluster.end();it_cluster++)
    {
        if(memcmp(it_cluster->host,instance_host,strlen(instance_host)))
        {
            continue;
        }

        //todo 处理send返回值，失败，成功
        send(it_cluster->conn,send_buff,sizeof(m_clients_connections),0);
    }
}

void master::run()
{
    if(ha_plugin_cluster_sip == NULL)
    {
        //todo log here. no sip err
        sql_print_warning("ha_plugin_cluster_sip is NULL");

    }
    else
    {
        bool bind_others=false;
        if(!check_sip_bound_me(ha_plugin_cluster_sip, bind_others)) //check sip is used or no or bind with me.
        {
            if(bind_others)//sip is not binding with me , and bind others. that is not permitted.
            {
                //todo log it
                sql_print_error("sip has bound with others");
            }
            else  //sip is not used,bind it.
            {
                if(bind_sip(ha_plugin_cluster_sip)) //sucessfully bind sip
                {
                    assert(check_sip_bound_me(ha_plugin_cluster_sip, bind_others));//如果绑定成功了，再次检测不应该失败。
                }
                else //failed bind sip;log it.
                {
                    sql_print_error("ha_plugin bind sip error");
                }
            }
        }
        else //sip is already bound with me, log it
        {
            //todo log it
            sql_print_information("sip is binding with this instance");
        }

    }



    pthread_t thread_waiting_for_connection;
    pthread_create(&thread_waiting_for_connection,NULL,thread_func_waiting_for_connection,NULL);
    pthread_t thread_collection;
    pthread_create(&thread_collection,NULL,thread_func_collection,NULL);
}

void master::stop()
{

}

bool master::downgrade_as_slave(){

    bool res;

    return res;
}
