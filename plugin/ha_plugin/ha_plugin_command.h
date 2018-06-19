//
// Created by 孙川 on 2018/6/14.
//

#ifndef MYSQL_HA_PLUGIN_COMMAND_H
#define MYSQL_HA_PLUGIN_COMMAND_H

typedef enum HA_PLUGIN_COMMAND
{
    HA_PLUGIN_BIND_SIP,
    HA_PLUGIN_UNBIND_SIP,
    HA_PLUGIN_REGISTER_SLAVE,
    //HA_plugin_remove_slave
}ha_plugin_command;

#endif //MYSQL_HA_PLUGIN_COMMAND_H
