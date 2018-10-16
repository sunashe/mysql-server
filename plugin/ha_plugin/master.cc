//
// Created by 孙川 on 2018/6/15.
//

#include "master.h"
#include "ha_plugin.h"
#include "log.h"
#include <string.h>
#include <map>
#include <iostream>
using std::map;


extern char server_uuid[UUID_LENGTH+1];
/**
 *
 */

extern "C" void* handler_waiting_for_connection(void* arg)
{
    reinterpret_cast<master *>(arg)->thread_func_waiting_for_connection();
    return NULL;
}

extern "C" void* handler_collection(void* arg)
{
    reinterpret_cast<master *>(arg)->thread_func_collection();
    return NULL;
}
/**
 *
 *
 * @param sip  cluster_sip
 * @param bind_others  bind with others
 * @return true as bind with me;false as not bind with me.
 */

bool master::check_sip_bound_me(char *sip, bool& bind_others) {

    bool r = false;
    MYSQL* conn;
    conn=mysql_init(0);
    int  connect_timeout =  1;
    mysql_options(conn,MYSQL_OPT_CONNECT_TIMEOUT,(const char*)&connect_timeout);
    if(mysql_real_connect(conn,sip,cluster_repl_user,cluster_repl_password,NULL,mysqld_port,NULL,0) != NULL) //todo 设置超时时间
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

/**
 *
 * @param sip cluster_sip
 * @return true sucessfully bind with me;false bind failed.
 */
bool master::bind_sip(char *sip) {
    bool res = false;
    //
    char bind_cmd[1000];
    sprintf(bind_cmd,"ip addr add %s/32 dev enp0s5",cluster_sip); //todo 确定网卡设备名称
#ifdef __linux__
    sprintf(bind_cmd,"ip addr add %s/32 dev enp0s5",cluster_sip); //todo 确定网卡设备名称
    if(system(bind_cmd) ==0)
    {
        sql_print_information("bind sip.");
        res = true;
    }
#elif __APPLE__


#endif

    return res;

}

/**
 *
 * @param sip cluster_sip
 * @return true unbind sucessfully ;false failed.
 */
bool master::unbind_sip(char* sip)
{
    bool res = false;
    //
    char bind_cmd[1000];
    sprintf(bind_cmd,"ip addr add %s/32 dev enp0s5",cluster_sip); //todo 确定网卡设备名称
#ifdef __linux__
    sprintf(bind_cmd,"ip addr add %s/32 dev enp0s5",cluster_sip); //todo 确定网卡设备名称
    if(system(bind_cmd) ==0)
    {
        sql_print_information("bind sip.");
        res = true;
    }
#elif __APPLE__


#endif

    return res;
}

/**
 *
 * @param arg
 */
void cleanup_thread_waiting_for_connection(void *arg)
{
    int i = *(int*)arg;

    shutdown(i,SHUT_RDWR);

}


void master::thread_func_waiting_for_connection()
{

    fd_set cluster_fd_set;
    struct timeval tv;
    int retval, maxfd;
    int ss = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_sockaddr;
    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_port = htons(manager_port);
    server_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);

    int mw_optval = 1;

    setsockopt(ss, SOL_SOCKET, SO_REUSEADDR, (char *)&mw_optval,sizeof(mw_optval));


    if(bind(ss, (struct sockaddr* )&server_sockaddr, sizeof(server_sockaddr)) == -1)
    {
        perror("bind");
       // exit(1);
    }

    pthread_cleanup_push(cleanup_thread_waiting_for_connection,(void*)&ss);

    if(listen(ss, 20/* todo config it*/) == -1)
    {
        perror("listen");
        //exit(1);
    }

    while(1)
    {
        struct sockaddr_in client_addr;
        socklen_t length = sizeof(client_addr);
        int conn = accept(ss, (struct sockaddr *) &client_addr, &length);
        if( conn < 0 ) {
            perror("connect"); //todo log
           // exit(1);
        }

        char* host=inet_ntoa(client_addr.sin_addr);
        data_node tmp_instance;
        memcpy(tmp_instance.host,host,strlen(host));
        tmp_instance.conn=conn;
        tmp_instance.dataNodeStatus=SLAVE;
        ha_cluster.push_back(tmp_instance);//li.push_back(conn);
        add_triangle(tmp_instance);
    }
    pthread_cleanup_pop(0);
    //ha_open = 0/NO todo send this to slave.
}


void master::thread_func_collection(){

    MYSQL* conn;
    MYSQL_RES* res;
    MYSQL_ROW row;
    conn = mysql_init(0);
    int  connect_timeout =  1;
    mysql_options(conn,MYSQL_OPT_CONNECT_TIMEOUT,(const char*)&connect_timeout);
    if(mysql_real_connect(conn,instance_host,cluster_repl_user,cluster_repl_password,NULL,mysqld_port,NULL,0) == NULL)
    {
        //connection error todo log it
    }

    while(true)
    {
        //collect ** and send to others.

        /* 1，收集当前链接最多的ip地址
         * 2，收集历史链接最多的ip地址
         * 3，收集GTID信息
         *
         */
        char query_for_most_current_connections[1000];
        char query_for_most_total_connections[1000];
        char query_for_gtid[1000];

        sprintf(query_for_most_current_connections,"select * from performance_schema.accounts "
                "where USER is not null and HOST is not null and host not in ('127.0.0.1','localhost','%s')"
                " order by CURRENT_CONNECTIONS desc limit 1",instance_host); //exclude 127.0.0.1,localhost,and host of this instance.

        if(mysql_query(conn,query_for_most_current_connections) == 0) //todo exclude user(root,dba_manager and so on)
        {
            res = mysql_store_result(conn);

            if(res->row_count > 0)
            {
                row = mysql_fetch_row(res);

                if(row[0] != NULL)
                {
                    //set to 0,
                    memset(clientsConnections->user,0,sizeof(clientsConnections->user));
                    memset(clientsConnections->host,0,sizeof(clientsConnections->host));

                    //save clientsConnections
                    memcpy(clientsConnections->user,row[0],strlen(row[0])+1);
                    memcpy(clientsConnections->host,row[1],strlen(row[1])+1);
                   // memcpy(clientsConnections->current_connections,)
                    clientsConnections->current_connections = atoi(row[2]);
                    clientsConnections->total_connections = atoi(row[3]);

                    data_node dataNodeClient;
                    memcpy(dataNodeClient.host,clientsConnections->host,strlen(clientsConnections->host)+1);
                    dataNodeClient.dataNodeStatus = CLIENT;
                    add_clients_cluster(dataNodeClient);
                    add_triangle(dataNodeClient);//add triangle

                }
            }
            else //no res
            {
                //todo log it and check performance_schema is on.
            }
        }
        else //query error
        {
            //todo log here.
        }

        if(send_m_to_others())
        {

        }
        else
        {

        }
        sleep(1);

    }
}

bool master::send_m_to_others() {
    if(ha_cluster.size() <= 1)
    {
        return true;//todo log here,no slave.
    }
    char send_buff[sizeof(m_clients_connections)];
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

    return true;
}

void master::run() {

    init_vec_triangle();

    if (cluster_sip == NULL) {
        //todo log here. no sip err
        sql_print_warning("cluster_sip is NULL");

    } else {
        bool bind_others = false;
        if (!check_sip_bound_me(cluster_sip, bind_others)) //check sip is used or no or bind with myself.
        {
            if (bind_others)//sip is not binding with myself , and bind others. that is not permitted.
            {
                //todo log it
                sql_print_error("sip has bound with others");
            } else  //sip is not used,bind it.
            {
                if (bind_sip(cluster_sip))
                {
                    assert(check_sip_bound_me(cluster_sip, bind_others));//如果绑定成功了，再次检测不应该失败。
                } else //failed bind sip;log it.
                {
                    sql_print_error("ha_plugin bind sip error");
                }
            }
        } else //sip is already bound with myself, log it
        {
            //todo log it
            sql_print_information("sip is binding with this instance");
        }

    }

    if (pthread_create(&thread_waiting_for_connection, NULL, handler_waiting_for_connection, this) != 0)
    {
        sql_print_error("ha plugin start thread waiting for connection error");
    } else {
        sql_print_information("ha plugin start thread waiting for connection successfully");
    }

    if(pthread_create(&thread_collection,NULL,handler_collection,this) !=0)
    {
        sql_print_error("ha plugin start thread collection error");
    } else{
        sql_print_information("ha plugin start thread collection successfully");
    }

    master_main_loop();

}


/*考虑什么情况下应该停止，如何安全的停止
     * 1,uninstall plugin;
     * 2,手动停止ha操作；
     * 3,主动降级为从
     * 4,被动降级为从
     */

void master::stop()
{
    //停止前应该做哪些数据清理工作
    pthread_cancel(thread_waiting_for_connection);
    pthread_cancel(thread_collection);
    pthread_join(thread_waiting_for_connection,NULL);
    pthread_join(thread_collection,NULL);
}

/* 如下三个函数分别适用于三种情况
 * 1，downgrade_as_slave_initiative() 手动降级为从，需要通知所有健康从机，设置readonly --> 通知从机 --> 从机确认 --> 状态变更信息落盘--> 解绑vip --> 停止主操作 --> 进入从机行为
 * 2，downgrade_as_slave_isolated() 主被孤立，主动降级  设置readonly --> 状态变更信息落盘 --> 解绑vip --> 进入从机器行为
 * 3，downgrade_as_slave_passive() 某个从机器被提升为主机
 */

bool master::downgrade_as_slave_initiative()
{

}//貌似不需要，只有一个从机提升为主机的行为就可以了

bool master::downgrade_as_slave_isolated(){

    bool res=false;
    opt_readonly=true;
    opt_super_readonly=true;

    unbind_sip(cluster_sip);
    is_master=false;

    return res;
}



bool master::downgrade_as_slave_passive()
{

}

/**
 *
 * @param dataNode
 * @return
 */


int master::add_triangle(data_node dataNode)
{
    pthread_mutex_lock(&m_vec_triangles);
    int size;
    switch(dataNode.dataNodeStatus){
        case MASTER:

            break;

        case SLAVE:
            add_triangle_slave(dataNode);

            break;

        case CLIENT:
            add_triangle_client(dataNode);

            break;

        default:
            break;

    }
    size = vec_triangles.size();
    pthread_mutex_unlock(&m_vec_triangles);
    return size;

}

int master::add_clients_cluster(data_node dataNode)
{
    int size;
    if(dataNode.dataNodeStatus == CLIENT)
    {
        pthread_mutex_lock(&m_clients_cluster);

        clients_cluster.push_back(dataNode);
        size=clients_cluster.size();

        pthread_mutex_unlock(&m_clients_cluster);

    }
    else
    {
        //todo log here
    }

    return size;
}

int master::add_triangle_slave(data_node dataNode)
{
    int size;
    map<char*,int> map_clients;
    map<char*,int>::iterator it_map_clients;
    size = vec_triangles.size();

    vector<triangle>::iterator  it_vec_triangles;
    for(it_vec_triangles = vec_triangles.begin();it_vec_triangles != vec_triangles.end();it_vec_triangles++)
    {
        if(it_vec_triangles->get_she().host == dataNode.host &&it_vec_triangles->get_she().port == dataNode.port )
        {
            it_vec_triangles->set_triangle_line_status();
            goto end;
        }
    }



    //添加一个新的，但是确实不知道要添加多少个，这取决于有多少个clients
    for(it_vec_triangles = vec_triangles.begin();it_vec_triangles != vec_triangles.end();it_vec_triangles++)
    {
        map_clients.insert(make_pair<char*,int>(it_vec_triangles->get_client().host,0));//去client重复
//        triangle triangle_new = *it_vec_triangles;
//        triangle_new.set_data_node_she(&dataNode);
//        triangle_new.set_triangle_line_status();
//        vec_triangles.push_back(triangle_new);
    }

    for(it_map_clients = map_clients.begin();it_map_clients != map_clients.end();it_map_clients++)
    {
        data_node dataNodeClient;
        memcpy(dataNodeClient.host,it_map_clients->first,strlen(it_map_clients->first)+1);
        dataNodeClient.port=22;
        dataNodeClient.dataNodeStatus=CLIENT;
        triangle triangle_new;
        triangle_new.set_data_node_me(&instance_me);
        triangle_new.set_data_node_she(&dataNode);
        triangle_new.set_data_node_client(&dataNodeClient);
        triangle_new.set_triangle_line_status();
        vec_triangles.push_back(triangle_new);

    }

    size=vec_triangles.size();

    end:
    return size;
}

int master::add_triangle_client(data_node dataNode)
{
    int size;
    size = vec_triangles.size();
    vector<triangle>::iterator  it_vec_triangles;
    for(it_vec_triangles = vec_triangles.begin();it_vec_triangles != vec_triangles.end();it_vec_triangles++)
    {
        if(it_vec_triangles->get_client().host == dataNode.host)
        {
            it_vec_triangles->set_triangle_line_status();
            goto end;
        }
    }
    //添加一个新的，但是确实不知道要添加多少个，这取决于有多少个slave;
    for(int i=1;i < ha_cluster.size();i++)
    {

        triangle triangle_new;
        triangle_new.set_data_node_me(&instance_me);
        triangle_new.set_data_node_she(&ha_cluster[i]);//
        triangle_new.set_data_node_client(&dataNode);
        triangle_new.set_triangle_line_status();
        vec_triangles.push_back(triangle_new);
    }

    size=vec_triangles.size();

    end:
    return size;
};


int master::init_vec_triangle()
{
   // data_node dataNodeMe;
    triangle triangle_virtual(&instance_me,&instance_me,&instance_me);
    triangle_virtual.set_triangle_line_status();
    vec_triangles.push_back(triangle_virtual);
}

/*
 * 移除一个triangle
 */
int master::remove_triangle(data_node dataNode)
{
    pthread_mutex_lock(&m_vec_triangles);
    int size;
    switch(dataNode.dataNodeStatus){
        case MASTER:

            break;

        case SLAVE:
            remove_triangle_slave(dataNode);

            break;

        case CLIENT:
            remove_triangle_client(dataNode);

            break;

        default:
            break;

    }
    size = vec_triangles.size();
    pthread_mutex_unlock(&m_vec_triangles);
    return size;
}


/*
 * 移除一个slave triangle
 */
int master::remove_triangle_slave(data_node dataNode)
{
    vector<triangle>::iterator it_vec_triangle;
    for(it_vec_triangle=vec_triangles.begin(); it_vec_triangle != vec_triangles.end();it_vec_triangle++)
    {
        if(strcmp(it_vec_triangle->get_she().host , dataNode.host) == 0 && it_vec_triangle->get_she().port == dataNode.port)
        {
            vec_triangles.erase(it_vec_triangle);
        }
    }

    return vec_triangles.size();
}

/*
 * 移除一个client triangle
 */
int master::remove_triangle_client(data_node dataNode)
{
    vector<triangle>::iterator it_vec_triangle;
    for(it_vec_triangle=vec_triangles.begin(); it_vec_triangle != vec_triangles.end();it_vec_triangle++)
    {
        if(strcmp(it_vec_triangle->get_she().host , dataNode.host))
        {
            vec_triangles.erase(it_vec_triangle);
        }
    }

    return vec_triangles.size();
}

/* 主循环作用
 * 1，用来检测已注册数据节点的存活状态
 * 2，移除失联节点
 * 3，故障检测
 * 3，检测参数上的变更，并进行相应的操作
 */

void master::master_main_loop()
{
    while(is_master && ha_open)
    {
        vector<data_node> data_nodes_unreachable;
        data_nodes_unreachable= check_ha_cluster_zombie_datanode();
        if(data_nodes_unreachable.size()>0)//如果存在失联节点，则进行triangle相关的操作，来确定master被网络隔离的问题。
        {
            /*
             * 1，寻找所有包含主从节点的triangles
             * 2，检测这些triangles的line status
             * 3，决策是否被隔离
             */


            int clients_unreachable=0;
            int clients_pingable=0;
            vector<data_node>::iterator it_data_nodes_unreachable;
            for(it_data_nodes_unreachable =data_nodes_unreachable.begin();it_data_nodes_unreachable != data_nodes_unreachable.end();it_data_nodes_unreachable++)
            {
                vector<data_node>::iterator it_clients_cluster;
                for(it_clients_cluster = clients_cluster.begin();it_clients_cluster != clients_cluster.end();it_clients_cluster++)
                {
                    triangle triangle1(&instance_me,&(*it_data_nodes_unreachable),&(*it_clients_cluster));
                    triangle1.set_triangle_line_status();
                    if(triangle1.get_line_myself_client_status() == PINGABLE)
                    {
                        clients_unreachable++;
                    }
                    else
                    {
                        clients_pingable++;
                    }

                }
            }

            //主被隔离，降级为从机
            if(clients_pingable < clients_unreachable)
            {
                //降级到slave
                downgrade_as_slave_isolated();
            }

        }

    }

    //is_master = false 进入到stop阶段
    stop();
}

//去除由于不确定因素失联的ha_cluster数据节点
vector<data_node> master::check_ha_cluster_zombie_datanode()
{
    //检测ha_cluster中每个data_node的存活

    vector<data_node> data_nodes_unreachable;
    pthread_mutex_lock(&m_ha_cluster);
    vector<data_node>::iterator it_ha_cluster;
    for(it_ha_cluster = ha_cluster.begin();it_ha_cluster != ha_cluster.end();it_ha_cluster++)
    {
        if(!detect_data_node(*it_ha_cluster))
        {
            data_nodes_unreachable.push_back(*it_ha_cluster);
        }
    }

    pthread_mutex_unlock(&m_ha_cluster);
    //remove zomble clients;

    return data_nodes_unreachable;
}


bool master::detect_data_node(data_node dataNode)
{
    //简单实现如下
    char cmd[50];
    sprintf(cmd,"ping %s -c 1 -W 1 > /dev/null",dataNode.host);
    if(system(cmd) !=0)
    {
        return false;
    }
    return true;
}
