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

class slave:public instance {
public:
    slave(){instanceStatus = SLAVE;};

public:
    void run();

    void stop();

    bool regist_to_cluster();
    bool check_master_alived();
    bool  promote();
};


#endif //MYSQL_SLAVE_H
