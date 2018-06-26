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

    void thread_func_collection();

    bool send_m_to_others();

    void thread_func_waiting_for_connection();

    vector<data_node> ha_cluster;

    m_clients_connections* clientsConnections = (m_clients_connections*)malloc(sizeof(m_clients_connections));

    bool downgrade_as_slave();
};


#endif //MYSQL_MASTER_H
