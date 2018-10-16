//
// Created by 孙川 on 2018/6/15.
//

#include "slave.h"
#include "ha_plugin.h"
#include "log.h"

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
        //超时1秒,
        //select(dataNodeMaster.conn,)

        break;
    }

    if(mysql_ping_master())
    {
        goto get_master_message;
    }

    //mysql_ping to master failed.从此从机的角度看，master实例不可达。将slave置于prepare_to_promote状态
    ha_plugin_instance_status = new char[sizeof("prepare_to_promote")+1];
    memcpy(ha_plugin_instance_status,"prepare_to_promote",sizeof("prepare_to_promote")+1);

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

/**
 * 如果ha_cluster中之后两个实例
 * 1，通过client确定主机情况，如果主机存活，放弃提主
 */

    if(ha_cluster.size() == 2) //cluster中只有两个实例
    {
        if(check_master_alived_by_clients())  //通过client检测master是否存活
        {
            prepared=false;
            return prepared;
        }
        else
        {
            waiting_for_relay_log_replay();
            prepared =true;
            return prepared;
        }

    }

/**
 * 如果ha_cluster中超过了2个实例
 * 1，查看网络状况，自己是否处于大多数网络环境中，如果不是，则放弃提主
 * 2，如果是，对比网络环境中其他机器的priority，查看自己所处的地位
 * 3，如果较小，放弃提主，如果最大，获得提主资格，如果都一样，则对比gtid
 * 4，选择当前数据最接近主机的从，获取提主资格，其他从机，依然为从机
 */


    my_sleep(1000); //sleep 1秒，以便其他客户端都发现主库的问题。

    if(!cluster_net_detection())
    {
        prepared=false;
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

/**
 *
 * @return 0 所有的从机优先级一样 ; 1 此从机优先级较小; 2 此从机的优先级较高
 */
int slave::compare_priority()
{
    int  res = 0 ;
    char query_get_priority[]="select @@instance_promote_priority";
    MYSQL* conn;
    vector<data_node>::iterator it_ha_cluster;
    for(it_ha_cluster=ha_cluster.begin();it_ha_cluster!=ha_cluster.end();it_ha_cluster++)
    {
        if(it_ha_cluster->dataNodeStatus == MASTER || it_ha_cluster->host == instance_host )
        {
            continue;
        }

        conn=mysql_init(0);
        int  connect_timeout =  1;
        mysql_options(conn,MYSQL_OPT_CONNECT_TIMEOUT,(const char*)&connect_timeout);
        if(mysql_real_connect(conn,it_ha_cluster->host,cluster_repl_user,cluster_repl_password,NULL,mysqld_port,NULL,0) !=NULL)
        {

        }
        else
        {
            sql_print_error("Connect to slave %s:%d error when compare instance promote priority ",it_ha_cluster->host,it_ha_cluster->port);
        }
    }

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


/**
 * call this function (ha_cluster.size == 2 )
 *
 * @return
 * true: master is alived;
 * false: master has gone away.
 */
bool slave::check_master_alived_by_clients()
{
    bool res;
    if(vec_triangles.empty())
    {
        res=true; //no client.
    }

    int pingable_num=0;
    int unpingable_num=0;
    vector<triangle> ::iterator it_vec_tri;
    for(it_vec_tri = vec_triangles.begin();it_vec_tri != vec_triangles.end();it_vec_tri++)
    {
        it_vec_tri->init_line_node();
        it_vec_tri->init_line_status();
        if(it_vec_tri->get_line_client_you_status() == PINGABLE)
        {
            pingable_num++;
        }
        else
        {
            unpingable_num++;
        }
    }

    if(pingable_num >= unpingable_num)
    {
        res=true;
    }
    else
    {
        res=false;
    }
    return res;
}

/**
 *
 * @return
 */
bool slave::cluster_net_detection()
{
    bool res;
    int pingable_num=1; //include myself;
    int unpingable_num=0;
    vector<data_node>::iterator it_ha_cluster;
    for(it_ha_cluster=ha_cluster.begin();it_ha_cluster!=ha_cluster.end();it_ha_cluster++)
    {
        if(it_ha_cluster->host == cluster_sip || it_ha_cluster->host == instance_me.host)
        {
            continue;
        }


        triangle triangle_tmp(&instance_me,&(*it_ha_cluster),&instance_me);
        triangle_tmp.init_line_node();
        triangle_tmp.init_line_status();
        if(triangle_tmp.get_line_myself_you_status() == PINGABLE)
        {
            pingable_num++;
        }
        else
        {
            unpingable_num++;
        }

    }

    if(pingable_num > unpingable_num)
    {
        res=true;
    }
    else
    {
        res=false;
    }

    return res;
}
