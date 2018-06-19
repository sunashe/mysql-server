//
// Created by 孙川 on 2018/6/15.
//

#include "slave.h"
#include "ha_plugin.h"

void slave::run()
{



}

void slave::stop()
{

}

bool slave::regist_to_cluster() {

    int sock_cli;
    fd_set rfds;
    struct timeval tv;
    int retval, maxfd;
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(ha_plugin_manager_port);  ///服务器端口
    servaddr.sin_addr.s_addr = inet_addr(ha_plugin_cluster_sip);  ///服务器ip

    //todo keep connect to master.
    if (connect(sock_cli, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
    {
        perror("connect"); //todo log it
        return false;
    }

    //send mes

    //save conn

    return true;
}

/* 1,check mysqld with sip
 * 2,check server
 *
 */
bool slave::check_master_alived() {


}

/* 1,send promote message to master
 * 2,master readonly and unbind sip,
 * 3,master send permit or no  message to this slave
 * 4,re init as a master.
 * 5,send new_master message to all others.
 */
bool slave::promote() {


}



