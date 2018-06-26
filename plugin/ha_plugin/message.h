//
// Created by 孙川 on 2018/6/14.
//

#ifndef MYSQL_MESSAGE_H
#define MYSQL_MESSAGE_H
#include "ha_plugin.h"
#define M_REGIST_HEADER 1
#define M_CLIENTS_HEADER 2




typedef struct M_REGIST
{
    char message_header = 1;
    char host[20];
    unsigned int port;
    char uuid[UUID_LENGTH+1];
}m_regist;


typedef struct M_CLIENTS_CONNECTIONS
{
    char message_header = 2;
    char user[100];
    char host[100];
    unsigned long int current_connections;
    unsigned long int total_connections;
}m_clients_connections;


typedef struct M_PROMOTE
{
    char message_header = 3;
    char host[20];
    unsigned int port;

}m_promote;

typedef struct M_PERMIT
{
    char message_header = 4;
    char body = '0';
}m_permit;



#endif //MYSQL_MESSAGE_H
