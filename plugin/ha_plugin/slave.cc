//
// Created by 孙川 on 2018/6/15.
//

#include "slave.h"
#include "ha_plugin.h"

void slave::run()
{
    if(!find_master() && slave_promote ==2)
    {
        is_master=true;
        return ;
    }


    if(!register_to_cluster())
    {
        return ;
    }

    if(!check_master_alived())
    {
        if(prepare_to_promote())
        {
            promote();
        }
    }

    return ;
}

bool slave::find_master()
{
    bool found;
    char cmd[100];
    sprintf(cmd,"ping %s -c 1 -W 1 > /dev/null",cluster_sip);
    if(system(cmd) == 0)
    {
        found=true;
    }
    else
    {
        found=false;
    }
    return found;
}

void slave::stop()
{

}


/*向主机注册
 * @return false,promote as master;
 * @return true, register successfully.
 */

bool slave::register_to_cluster() {

    int sock_cli;
    fd_set rfds;
    struct timeval tv;
    int retval, maxfd;
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(manager_port);  ///服务器端口
    servaddr.sin_addr.s_addr = inet_addr(cluster_sip);  ///服务器ip

    //todo keep connect to master.
    while(true)
    {
        if(is_master)
        {
            return false;
        }

        if (connect(sock_cli, (struct sockaddr *)&servaddr, sizeof(servaddr)) != 0)
        {
            perror("connect"); //todo log it
        }
        else
        {
            break;
        }

        sleep(1);
    }


    data_node instance_master;
    memset(instance_master.host,0,HOST_MAX_LEN);
    memcpy(instance_master.host,cluster_sip,strlen(cluster_sip));
    instance_master.port=mysqld_port;
    instance_master.dataNodeStatus = MASTER;


    //send mes
    char sendbuf[1000];
    m_register* mRegister = (m_register*)malloc(sizeof(m_register));
    memset(mRegister->host,0,sizeof(mRegister->host));
    memcpy(mRegister->host,instance_host,strlen(instance_host)+1);
    mRegister->port=mysqld_port;
    memcpy(mRegister->uuid,server_uuid,strlen(server_uuid)+1);

    memcpy(sendbuf,mRegister,sizeof(m_register));
    send(sock_cli,sendbuf,strlen(sendbuf)+1,0);

    instance_master.conn = sock_cli;

    //save conn
    ha_cluster.push_back(instance_master);
    return true;
}

/* 1,select 3秒超时
 * 2,check server
 * 3,
 */
bool slave::check_master_alived()
{
    bool res;
    data_node dataNodeMaster;
    vector<data_node>::const_iterator it_ha_cluster;
    for(it_ha_cluster=ha_cluster.begin();it_ha_cluster!=ha_cluster.end();it_ha_cluster++)
    {
        if(it_ha_cluster->dataNodeStatus == MASTER)
        {
            dataNodeMaster = *it_ha_cluster;
        }
    }

get_master_message:
    while(true)
    {
        //超时3秒,
        //select(dataNodeMaster.conn,)

        break;
    }

    if(mysql_ping_master())
    {
        goto get_master_message;
    }

    res=false;
    return res;
}

/* 1,send promote message to master
 * 2,master readonly and unbind sip,
 * 3,master send permit or no  message to this slave
 * 4,re init as a master.
 * 5,send new_master message to all others.
 */



bool slave::promote()
{


}

/* 1,在设置的时间下等待relay log回放
 * 2,选主，
 * 3,自己被选择为主，或者为从。
 */

bool slave::prepare_to_promote()
{

    bool prepared = false;

    if(ha_cluster.size() == 2)//ha_cluster中只有两个实例
    {
        waiting_for_relay_log_replay();
        prepared =true;
        return prepared;
    }

    int priority;
    priority = compare_priority();

    switch(priority)
    {
        case 0:          //所有的从机优先级一样
        {
            int gtid_res;
            waiting_for_relay_log_replay();
            gtid_res = compare_gtid();
            switch(gtid_res)  //比较回放情况
            {
                case 0:
                {
                    if(compare_uuid())
                    {
                        prepared=true;
                    }
                    else
                    {
                        prepared=false;
                    }
                    break;
                }

                case 1:
                {
                    prepared=false;
                    break;
                }

                case 2:
                {
                    prepared=true;
                    break;
                }
            }
            break;
        }

        case 1:          //此从机优先级较小
        {
            prepared=false;
            break;
        }

        case 2:          //此从机优先级最大
        {
            prepared = true;
            waiting_for_relay_log_replay();
            break;
        }
        default:
        {
            break;
        }
    }


    return prepared;
}

void slave::waiting_for_relay_log_replay()
{
    int waitTime=waiting_for_slave_replay_seconds;
    while(waitTime!=0)
    {

        waitTime--;
        sleep(1);
    }
}


/*@return 0,所有从机的gtid一致 ；1，此从机的gtid值较小；3，此从机的gtid较大
 */

int slave::compare_gtid()
{
    int res=3; //初始化默自己的gtid最大
    char get_gtid_query[]="show master status";
    MYSQL* conn;
    vector<data_node>::iterator it_ha_cluster;
    for(it_ha_cluster=ha_cluster.begin();it_ha_cluster!=ha_cluster.end();it_ha_cluster++)
    {
        conn=mysql_init(0);
        int  connect_timeout =  1;
        mysql_options(conn,MYSQL_OPT_CONNECT_TIMEOUT,(const char*)&connect_timeout);
        if(mysql_real_connect(conn,it_ha_cluster->host,cluster_repl_user,cluster_repl_password,NULL,mysqld_port,NULL,0) !=NULL)
        {
            if(mysql_query(conn,get_gtid_query) == 0 )
            {
                char *gtid_others=NULL;
                MYSQL_RES* res;
                MYSQL_ROW  row;
                res=mysql_store_result(conn);
                row=mysql_fetch_row(res);
                if(row[4] == NULL)
                {//todo 对比GTID
                }
                else
                {

                }

            }
            else
            {
                res=3;
            }

            mysql_close(conn);
        }
        else
        {
            res=3;
        }
    }


    return res;
}


int slave::compare_priority()
{
    int  res;


    return res;
}

bool slave::compare_uuid()
{
    bool res;


    return res;
}


bool slave::mysql_ping_master(){

    bool res;

    MYSQL* conn;
    conn=mysql_init(0);
    int  connect_timeout =  1;
    mysql_options(conn,MYSQL_OPT_CONNECT_TIMEOUT,(const char*)&connect_timeout);
    if(mysql_real_connect(conn,cluster_sip,cluster_repl_user,cluster_repl_password,NULL,mysqld_port,NULL,0) != NULL)
    {
        if(mysql_ping(conn) ==0)
        {
            res=true;
        }
        else
        {
            res=false;
        }

        mysql_close(conn);
    }
    else
    {
        res=false;
    }

    return res;
}

bool slave::check_slave_io_thread()
{

}