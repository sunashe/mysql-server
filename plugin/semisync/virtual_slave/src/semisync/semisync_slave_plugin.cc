/* Copyright (C) 2007 Google Inc.
   Copyright (C) 2008 MySQL AB
   Copyright (c) 2008, 2017, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA */


#include "semisync_slave.h"
#include <mysql.h>
#include <mysqld_error.h>
//#include "semisync_slave_plugin.h"

ReplSemiSyncSlave repl_semisync;

/*
  indicate whether or not the slave should send a reply to the master.

  This is set to true in repl_semi_slave_read_event if the current
  event read is the last event of a transaction. And the value is
  checked in repl_semi_slave_queue_event.
*/
bool semi_sync_need_reply= false;

C_MODE_START

int repl_semi_reset_slave(Binlog_relay_IO_param *param)
{
  // TODO: reset semi-sync slave status here
  return 0;
}

int repl_semi_slave_request_dump(Binlog_relay_IO_param *param,
				 uint32 flags)
{
  MYSQL *mysql= param->mysql;
  MYSQL_RES *res= 0;
#ifndef DBUG_OFF
  MYSQL_ROW row= NULL;
#endif
  const char *query;
  uint mysql_error= 0;

  if (!repl_semisync.getSlaveEnabled())
    return 0;

  /* Check if master server has semi-sync plugin installed */
  query= "SELECT @@global.rpl_semi_sync_master_enabled";
  if (mysql_real_query(mysql, query, static_cast<ulong>(strlen(query))) ||
      !(res= mysql_store_result(mysql)))
  {
    mysql_error= mysql_errno(mysql);
    if (mysql_error != ER_UNKNOWN_SYSTEM_VARIABLE)
    {
     // sql_print_error("Execution failed on master: %s; error %d", query, mysql_error);
      printf("Execution failed on master: %s; error %d", query, mysql_error);

      return 1;
    }
  }
  else
  {
#ifndef DBUG_OFF
    row=
#endif
      mysql_fetch_row(res);
  }

  DBUG_ASSERT(mysql_error == ER_UNKNOWN_SYSTEM_VARIABLE ||
              strtoul(row[0], 0, 10) == 0 || strtoul(row[0], 0, 10) == 1);

  if (mysql_error == ER_UNKNOWN_SYSTEM_VARIABLE)
  {
    /* Master does not support semi-sync */
    sql_print_warning("Master server does not support semi-sync, "
                      "fallback to asynchronous replication");
    rpl_semi_sync_slave_status= 0;
    mysql_free_result(res);
    return 0;
  }
  mysql_free_result(res);

  /*
    Tell master dump thread that we want to do semi-sync
    replication
  */
  query= "SET @rpl_semi_sync_slave= 1";
  if (mysql_real_query(mysql, query, static_cast<ulong>(strlen(query))))
  {
    sql_print_error("Set 'rpl_semi_sync_slave=1' on master failed");
    return 1;
  }
  mysql_free_result(mysql_store_result(mysql));
  rpl_semi_sync_slave_status= 1;
  return 0;
}

int repl_semi_slave_read_event(Binlog_relay_IO_param *param,
			       const char *packet, unsigned long len,
			       const char **event_buf, unsigned long *event_len)
{
  if (rpl_semi_sync_slave_status)
    return repl_semisync.slaveReadSyncHeader(packet, len,
					     &semi_sync_need_reply,
					     event_buf, event_len);
  *event_buf= packet;
  *event_len= len;
  return 0;
}

int repl_semi_slave_queue_event(Binlog_relay_IO_param *param,
				const char *event_buf,
				unsigned long event_len,
				uint32 flags)
{
  if (rpl_semi_sync_slave_status && semi_sync_need_reply)
  {
    /*
      We deliberately ignore the error in slaveReply, such error
      should not cause the slave IO thread to stop, and the error
      messages are already reported.
    */
    (void) repl_semisync.slaveReply(param->mysql,
                                    param->master_log_name,
                                    param->master_log_pos);
  }
  return 0;
}

int repl_semi_slave_io_start(Binlog_relay_IO_param *param)
{
  return repl_semisync.slaveStart(param);
}

int repl_semi_slave_io_end(Binlog_relay_IO_param *param)
{
  return repl_semisync.slaveStop(param);
}

int repl_semi_slave_sql_start(Binlog_relay_IO_param *param)
{
  return 0;
}

int repl_semi_slave_sql_stop(Binlog_relay_IO_param *param, bool aborted)
{
  return 0;
}

C_MODE_END


int symisync_slave_init()
{
  return repl_semisync.initObject();
}

int handle_repl_semi_slave_request_dump(void *param,
                                        uint32 flags)
{
  return repl_semi_slave_request_dump((Binlog_relay_IO_param*) param,flags);
}
int handle_repl_semi_slave_read_event(void *param,
                                      const char *packet, unsigned long len,
                                      const char **event_buf, unsigned long *event_len)
{
  return repl_semi_slave_read_event((Binlog_relay_IO_param*) param,packet,len,event_buf,event_len);
}
int handle_repl_semi_slave_queue_event(void *param,
                                       const char *event_buf,
                                       unsigned long event_len,
                                       uint32 flags)
{
  return repl_semi_slave_queue_event((Binlog_relay_IO_param*) param,event_buf,event_len,flags);
}

int handle_repl_semi_slave_io_start(void *param)
{
  return repl_semi_slave_io_start((Binlog_relay_IO_param*)param);
}

int handle_repl_semi_slave_io_end(void *param)
{
  return repl_semi_slave_io_end((Binlog_relay_IO_param*)param);
}

int handle_repl_semi_slave_sql_start(void *param)
{
  return repl_semi_slave_sql_start((Binlog_relay_IO_param*)param);
}
int handle_repl_semi_slave_sql_stop(void *param, bool aborted)
{
  return repl_semi_slave_sql_stop((Binlog_relay_IO_param*)param,aborted);
}