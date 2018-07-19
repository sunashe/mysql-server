//
// Created by 孙川 on 2018/7/6.
//

#include "triangle.h"

void triangle::set_triangle_line_status()
{
    init_line_node();
    init_line_status();
}


bool triangle::compare_triangle(const data_node*, const data_node*,const data_node*) const
{
    return true;
}


void  triangle::init_line_status()
{
    if(test_network(&myself,&you))
    {
        line_me_she.lineStatus = PINGABLE;
    }
    else
    {
        line_me_she.lineStatus = UNREACHABLE;
    }

    if(test_network(&myself,&client))
    {
        line_me_she.lineStatus = PINGABLE;
    }
    else
    {
        line_me_she.lineStatus = UNREACHABLE;
    }
}

void triangle::init_line_node()
{
    line_me_she.l_this=&myself;
    line_me_she.l_that=&you;
    line_me_client.l_this=&myself;
    line_me_client.l_that=&client;
    line_client_she.l_this=&you;
    line_client_she.l_that=&client;
}

bool triangle::test_network(data_node* l_this,data_node* l_that)
{
    char cmd[50];
    sprintf(cmd,"ping %s -c 1 -W 1",l_that->host);
    if(system(cmd) ==0)
    {
        return true;
    }

    return false;
}