//
// Created by 孙川 on 2018/6/15.
//

#ifndef MYSQL_INSTANCE_H
#define MYSQL_INSTANCE_H
#include <mysql/plugin.h>
#include <string>
#include <vector>
#include <mysql/mysql.h>
#define UUID_LENGTH (8+1+4+1+4+1+4+1+12)
#define HOST_MAX_LEN (3+1+3+1+3+1+3)

using namespace std;

typedef enum INSTANCE_STATUS
{
    MASTER,
    SLAVE,
    UNKNOWN
}instance_status;

typedef struct DATA_NODE
{
    char host[HOST_MAX_LEN];
    unsigned int port;
    char uuid[UUID_LENGTH];
    instance_status instanceStatus;
    int conn;
}data_node;


class instance {
public:

    instance_status instanceStatus;
    char* get_host(){return host;}
    unsigned int get_port(){return port;}
    char* get_uuid(){return uuid;}

    int conn;

private:
    char* host;
    unsigned int port;
    char* uuid;

};


#endif //MYSQL_INSTANCE_H
