//
// Created by 孙川 on 2018/6/15.
//

#ifndef MYSQL_SLAVE_H
#define MYSQL_SLAVE_H
#include "instance.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/types.h>
#include "message.h"

class slave:public instance {
public:
    slave(){instanceStatus = SLAVE;};

public:
    void run();

    void stop();

    bool find_master();

    bool register_to_cluster();
    bool check_master_alived();
    bool  promote();
    bool prepare_to_promote();
    void waiting_for_relay_log_replay();

    int compare_gtid();
    int compare_priority();
    bool compare_uuid();
    bool mysql_ping_master();
    bool check_slave_io_thread();
    bool check_master_alived_by_clients();
    bool cluster_net_detection();_
};


#endif //MYSQL_SLAVE_H
