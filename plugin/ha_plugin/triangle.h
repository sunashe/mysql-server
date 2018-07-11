//
// Created by 孙川 on 2018/7/6.
//

#ifndef MYSQL_TRIANGLE_H
#define MYSQL_TRIANGLE_H
#include "instance.h"

/* 三角形是一种非常稳定的关系，这里的三角形包含如下元素
 * 1，三个点，m，s，c，
 * 2，三条线，m-s，m-c，s-c
 * 为了确定三个点的存活状态，只需要确定三条线的状态
 */

typedef enum LINE_STATUS
{
    PINGABLE  = 0,
    UNREACHABLE = 1,
}line_status;

typedef struct LINE {
    data_node* l_this;
    data_node* l_that;
    line_status lineStatus;

}line;

class triangle {
public:
    triangle(){};
    triangle(const data_node* const r_me,const data_node* const r_she,const data_node* const r_cli):me(*r_me),she(*r_she),client(*r_cli)
    {

    };
    ~triangle(){};

    triangle& operator =(const triangle& tri_r)
    {
        if(this!=&tri_r)
        {
            this->me = tri_r.me;
            this->she = tri_r.she;
            this->client = tri_r.client;
            this->line_me_she = tri_r.line_me_she;
            this->line_me_client = tri_r.line_me_client;
            this->line_client_she = tri_r.line_client_she;
        }

        return *this;
    }

    bool compare_triangle(const data_node*, const data_node*,const data_node*)const;
    void set_triangle(data_node*,data_node*,data_node*);
    void set_triangle_line_status();
    line_status get_line_me_she_status(){return this->line_me_she.lineStatus;}
    line_status get_line_me_client_status(){return this->line_me_client.lineStatus;}
    line_status get_line_client_she_status(){return this->line_client_she.lineStatus;}
    data_node get_me(){return this->me;}
    data_node get_she(){return this->she;}
    data_node get_client(){return this->client;}

    void set_data_node_me(const data_node* dataNode){this->me = *dataNode;}
    void set_data_node_she(const data_node* dataNode){this->she = *dataNode;}
    void set_data_node_client(const data_node* dataNode){this->client = *dataNode;}
    void init_line_status();
    void init_line_node();

    bool test_network(data_node*,data_node*);

private:
    data_node me;
    data_node she;
    data_node client;
    line line_me_she;
    line line_me_client;
    line line_client_she;
};


#endif //MYSQL_TRIANGLE_H
