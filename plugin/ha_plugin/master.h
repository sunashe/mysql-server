//
// Created by 孙川 on 2018/6/15.
//

#ifndef MYSQL_MASTER_H
#define MYSQL_MASTER_H
#include "instance.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "message.h"


class master:public instance{

public:
    master(){instanceStatus = MASTER;};
    ~master(){};

    void run();
    void stop();

    bool check_sip_bound_me(char *sip, bool& bind_others);

    bool bind_sip(char* sip);
    bool unbind_sip(char* sip);

    void thread_func_collection();

    bool send_m_to_others();

    void thread_func_waiting_for_connection();


    vector<data_node> ha_cluster; //主，从

    //切换相关函数
    bool downgrade_as_slave_isolated(); //被孤立，主动降级
    bool downgrade_as_slave_passive();//某个从机被提升为主机，则主机被动成为从机器
    bool downgrade_as_slave_initiative();//命令主动降级为从机器

    int init_vec_triangle();
    int add_triangle(data_node dataNode);
    int add_triangle_slave(data_node dataNode);
    int add_triangle_client(data_node dataNode);

    int add_clients_cluster(data_node dataNode);

    int remove_triangle(data_node dataNode);
    int remove_triangle_slave(data_node dataNode);
    int remove_triangle_client(data_node dataNode);
    void master_main_loop();
    vector<data_node> check_ha_cluster_zombie_datanode();
    bool detect_data_node(data_node dataNode);
private:
    m_clients_connections* clientsConnections = (m_clients_connections*)malloc(sizeof(m_clients_connections));
    pthread_t thread_waiting_for_connection;
    pthread_t thread_collection;


};

void cleanup_thread_waiting_for_connection(void *);

#endif //MYSQL_MASTER_H
