/*
   Copyright (c) 2000, 2016, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
*/

#define MYSQL_CLIENT
#undef MYSQL_SERVER
#include "./include/my_default.h"
#include "./include/my_time.h"
#include "semisync_slave_plugin.h"
#include <string>
using std::string;
#include <mysql/mysql.h>
#include <mysql/sql_common.h>
#include <mysql/errmsg.h>

/* That one is necessary for defines of OPTION_NO_FOREIGN_KEY_CHECKS etc */
#include "./include/query_options.h"
#include <signal.h>
#include "./include/my_dir.h"

#include "./include/prealloced_array.h"
#include "virtual_slave.h"
#include "Config/config.h"
/*
  error() is used in macro BINLOG_ERROR which is invoked in
  rpl_gtid.h, hence the early forward declaration.
*/
static void error(const char *format, ...)
  MY_ATTRIBUTE((format(printf, 1, 2)));
static void warning(const char *format, ...)
  MY_ATTRIBUTE((format(printf, 1, 2)));

#include "./include/rpl_gtid.h"
#include "./include/log_event.h"
#include "./include/log_event_old.h"
#include "./include/rpl_constants.h"
#include <mysql/sql_common.h>
#include <mysql/my_dir.h>
#include "./include/welcome_copyright_notice.h" // ORACLE_WELCOME_COPYRIGHT_NOTICE
#include "./include/sql_string.h"
#include "./include/my_decimal.h"
//include "rpl_constants.h"

#include <algorithm>
#include <utility>
#include <map>

using std::min;
using std::max;

/*
  Map containing the names of databases to be rewritten,
  to a different one.
*/


/**
  The function represents Log_event delete wrapper
  to reset possibly active temp_buf member.
  It's to be invoked in context where the member is
  not bound with dynamically allocated memory and therefore can
  be reset as simple as with plain assignment to NULL.

  @param ev  a pointer to Log_event instance
*/
inline void reset_temp_buf_and_delete(Log_event *ev)
{
  ev->temp_buf= NULL;
  delete ev;
}

/*
  The character set used should be equal to the one used in mysqld.cc for
  server rewrite-db
*/
#define mysqld_charset &my_charset_latin1

#define CLIENT_CAPABILITIES	(CLIENT_LONG_PASSWORD | CLIENT_LONG_FLAG | CLIENT_LOCAL_FILES)

char server_version[SERVER_VERSION_LENGTH];
ulong filter_server_id = 0;

/*
  This strucure is used to store the event and the log postion of the events 
  which is later used to print the event details from correct log postions.
  The Log_event *event is used to store the pointer to the current event and 
  the event_pos is used to store the current event log postion.
*/
struct buff_event_info
{
  Log_event *event;
  my_off_t event_pos;
};

/* 
  One statement can result in a sequence of several events: Intvar_log_events,
  User_var_log_events, and Rand_log_events, followed by one
  Query_log_event. If statements are filtered out, the filter has to be
  checked for the Query_log_event. So we have to buffer the Intvar,
  User_var, and Rand events and their corresponding log postions until we see 
  the Query_log_event. This dynamic array buff_ev is used to buffer a structure 
  which stores such an event and the corresponding log position.
*/
typedef Prealloced_array<buff_event_info, 16, true> Buff_ev;
Buff_ev *buff_ev(PSI_NOT_INSTRUMENTED);

// needed by net_serv.c
ulong bytes_sent = 0L, bytes_received = 0L;
ulong mysqld_net_retry_count = 10L;
ulong open_files_limit;
ulong opt_binlog_rows_event_max_size;
uint test_flags = 0; 
static uint opt_protocol= 0;
static FILE *result_file;

#ifndef DBUG_OFF
static const char* default_dbug_option = "d:t:o,/tmp/mysqlbinlog.trace";
#endif
static const char *load_default_groups[]= { "mysqlbinlog","client",0 };

static my_bool one_database=0, disable_log_bin= 0;
static my_bool opt_hexdump= 0;
const char *base64_output_mode_names[]=
{"NEVER", "AUTO", "UNSPEC", "DECODE-ROWS", NullS};
TYPELIB base64_output_mode_typelib=
  { array_elements(base64_output_mode_names) - 1, "",
    base64_output_mode_names, NULL };
static enum_base64_output_mode opt_base64_output_mode= BASE64_OUTPUT_UNSPEC;
static char *opt_base64_output_mode_str= 0;
static my_bool opt_remote_alias= 0;
const char *remote_proto_names[]=
{"BINLOG-DUMP-NON-GTIDS", "BINLOG-DUMP-GTIDS", NullS};
TYPELIB remote_proto_typelib=
  { array_elements(remote_proto_names) - 1, "",
    remote_proto_names, NULL };
static enum enum_remote_proto {
  BINLOG_DUMP_NON_GTID= 0,
  BINLOG_DUMP_GTID= 1,
  BINLOG_LOCAL= 2
} opt_remote_proto= BINLOG_LOCAL;
static char *opt_remote_proto_str= 0;
static char *database= 0;
static char *output_file= 0;
static char *rewrite= 0;
static my_bool force_opt= 0, short_form= 0, idempotent_mode= 0;
static my_bool debug_info_flag, debug_check_flag;
static my_bool force_if_open_opt= 1, raw_mode= 0;
static my_bool to_last_remote_log= 0, stop_never= 0;
static my_bool opt_verify_binlog_checksum= 1;
static ulonglong offset = 0;
static int64 stop_never_slave_server_id= -1;
static int64 connection_server_id= -1;
static char* host = 0;
static int port= 0;
static uint my_end_arg;
static const char* sock= 0;
static char *opt_plugin_dir= 0, *opt_default_auth= 0;
static my_bool opt_secure_auth= TRUE;

#if defined (_WIN32) && !defined (EMBEDDED_LIBRARY)
static char *shared_memory_base_name= 0;
#endif
static char* user = 0;
static char* pass = 0;
static char *opt_bind_addr = NULL;
static char *charset= 0;

static uint verbose= 0;

static ulonglong start_position=4, stop_position;
#define start_position_mot ((my_off_t)start_position)
#define stop_position_mot  ((my_off_t)stop_position)
static MYSQL* mysql = NULL;
static char* dirname_for_local_load= 0;
static uint opt_server_id_bits = 0;
static ulong opt_server_id_mask = 0;
Sid_map *global_sid_map= NULL;
Checkable_rwlock *global_sid_lock= NULL;
Gtid_set *gtid_set_included= NULL;
Gtid_set *gtid_set_excluded= NULL;

/**
  Pointer to the Format_description_log_event of the currently active binlog.

  This will be changed each time a new Format_description_log_event is
  found in the binlog. It is finally destroyed at program termination.
*/
static Format_description_log_event* glob_description_event= NULL;

/**
  Exit status for functions in this file.
*/
enum Exit_status {
  /** No error occurred and execution should continue. */
  OK_CONTINUE= 0,
  /** An error occurred and execution should stop. */
  ERROR_STOP,
  /** No error occurred but execution should stop. */
  OK_STOP
};

/*
  Options that will be used to filter out events.
*/
static char *opt_include_gtids_str= NULL,
            *opt_exclude_gtids_str= NULL;
static my_bool opt_skip_gtids= 0;

static Exit_status dump_remote_log_entries(PRINT_EVENT_INFO *print_event_info,
                                           const char* logname);
static Exit_status dump_single_log(PRINT_EVENT_INFO *print_event_info,
                                   const char* logname);
static Exit_status dump_multiple_logs(int argc, char **argv);
static Exit_status safe_connect();

struct buff_event_info buff_event;

class Load_log_processor
{
  char target_dir_name[FN_REFLEN];
  size_t target_dir_name_len;

  /*
    When we see first event corresponding to some LOAD DATA statement in
    binlog, we create temporary file to store data to be loaded.
    We add name of this file to file_names set using its file_id as index.
    If we have Create_file event (i.e. we have binary log in pre-5.0.3
    format) we also store save event object to be able which is needed to
    emit LOAD DATA statement when we will meet Exec_load_data event.
    If we have Begin_load_query event we simply store 0 in
    File_name_record::event field.
  */
  struct File_name_record
  {
    char *fname;
    Create_file_log_event *event;
  };

  typedef std::map<uint, File_name_record> File_names;
  File_names file_names;

  /**
    Looks for a non-existing filename by adding a numerical suffix to
    the given base name, creates the generated file, and returns the
    filename by modifying the filename argument.

    @param[in,out] filename Base filename

    @param[in,out] file_name_end Pointer to last character of
    filename.  The numerical suffix will be written to this position.
    Note that there must be a least five bytes of allocated memory
    after file_name_end.

    @retval -1 Error (can't find new filename).
    @retval >=0 Found file.
  */
  File create_unique_file(char *filename, char *file_name_end)
    {
      File res;
      /* If we have to try more than 1000 times, something is seriously wrong */
      for (uint version= 0; version<1000; version++)
      {
	sprintf(file_name_end,"-%x",version);
	if ((res= my_create(filename,0,
			    O_CREAT|O_EXCL|O_BINARY|O_WRONLY,MYF(0)))!=-1)
	  return res;
      }
      return -1;
    }

public:
  Load_log_processor() : file_names()
  {}
  ~Load_log_processor() {}

  void init_by_dir_name(const char *dir)
    {
      target_dir_name_len= (convert_dirname(target_dir_name, dir, NullS) -
			    target_dir_name);
    }
  void init_by_cur_dir()
    {
      if (my_getwd(target_dir_name,sizeof(target_dir_name),MYF(MY_WME)))
	exit(1);
      target_dir_name_len= strlen(target_dir_name);
    }
  void destroy()
  {
    File_names::iterator iter= file_names.begin();
    File_names::iterator end= file_names.end();
    for (; iter != end; ++iter)
    {
      File_name_record *ptr= &iter->second;
      if (ptr->fname)
      {
        my_free(ptr->fname);
        delete ptr->event;
        memset(ptr, 0, sizeof(File_name_record));
      }
    }

    file_names.clear();
  }

  /**
    Obtain Create_file event for LOAD DATA statement by its file_id
    and remove it from this Load_log_processor's list of events.

    Checks whether we have already seen a Create_file_log_event with
    the given file_id.  If yes, returns a pointer to the event and
    removes the event from array describing active temporary files.
    From this moment, the caller is responsible for freeing the memory
    occupied by the event.

    @param[in] file_id File id identifying LOAD DATA statement.

    @return Pointer to Create_file_log_event, or NULL if we have not
    seen any Create_file_log_event with this file_id.
  */
  Create_file_log_event *grab_event(uint file_id)
  {
    File_name_record *ptr;
    Create_file_log_event *res;

    File_names::iterator it= file_names.find(file_id);
    if (it == file_names.end())
      return NULL;
    ptr= &((*it).second);
    if ((res= ptr->event))
      memset(ptr, 0, sizeof(File_name_record));
    return res;
  }

  /**
    Obtain file name of temporary file for LOAD DATA statement by its
    file_id and remove it from this Load_log_processor's list of events.

    @param[in] file_id Identifier for the LOAD DATA statement.

    Checks whether we have already seen Begin_load_query event for
    this file_id. If yes, returns the file name of the corresponding
    temporary file and removes the filename from the array of active
    temporary files.  From this moment, the caller is responsible for
    freeing the memory occupied by this name.

    @return String with the name of the temporary file, or NULL if we
    have not seen any Begin_load_query_event with this file_id.
  */
  char *grab_fname(uint file_id)
  {
    File_name_record *ptr;
    char *res= NULL;

    File_names::iterator it= file_names.find(file_id);
    if (it == file_names.end())
      return NULL;
    ptr= &((*it).second);
    if (!ptr->event)
    {
      res= ptr->fname;
      memset(ptr, 0, sizeof(File_name_record));
    }
    return res;
  }
  Exit_status process(Create_file_log_event *ce);
  Exit_status process(Begin_load_query_log_event *ce);
  Exit_status process(Append_block_log_event *ae);
  File prepare_new_file_for_old_format(Load_log_event *le, char *filename);
  Exit_status load_old_format_file(NET* net, const char *server_fname,
                                   uint server_fname_len, File file);
  Exit_status process_first_event(const char *bname, size_t blen,
                                  const uchar *block,
                                  size_t block_len, uint file_id,
                                  Create_file_log_event *ce);
};


/**
  Creates and opens a new temporary file in the directory specified by previous call to init_by_dir_name() or init_by_cur_dir().

  @param[in] le The basename of the created file will start with the
  basename of the file pointed to by this Load_log_event.

  @param[out] filename Buffer to save the filename in.

  @return File handle >= 0 on success, -1 on error.
*/
File Load_log_processor::prepare_new_file_for_old_format(Load_log_event *le,
							 char *filename)
{
  size_t len;
  char *tail;
  File file;
  
  fn_format(filename, le->fname, target_dir_name, "", MY_REPLACE_DIR);
  len= strlen(filename);
  tail= filename + len;
  
  if ((file= create_unique_file(filename,tail)) < 0)
  {
    error("Could not construct local filename %s.",filename);
    return -1;
  }
  
  le->set_fname_outside_temp_buf(filename,len+(uint) strlen(tail));
  
  return file;
}


/**
  Reads a file from a server and saves it locally.

  @param[in,out] net The server to read from.

  @param[in] server_fname The name of the file that the server should
  read.

  @param[in] server_fname_len The length of server_fname.

  @param[in,out] file The file to write to.

  @retval ERROR_STOP An error occurred - the program should terminate.
  @retval OK_CONTINUE No error, the program should continue.
*/
Exit_status Load_log_processor::load_old_format_file(NET* net,
                                                     const char*server_fname,
                                                     uint server_fname_len,
                                                     File file)
{
  uchar buf[FN_REFLEN+1];
  buf[0] = 0;
  memcpy(buf + 1, server_fname, server_fname_len + 1);
  if (my_net_write(net, buf, server_fname_len +2) || net_flush(net))
  {
    error("Failed requesting the remote dump of %s.", server_fname);
    return ERROR_STOP;
  }
  
  for (;;)
  {
    ulong packet_len = my_net_read(net);
    if (packet_len == 0)
    {
      if (my_net_write(net, (uchar*) "", 0) || net_flush(net))
      {
        error("Failed sending the ack packet.");
        return ERROR_STOP;
      }
      /*
	we just need to send something, as the server will read but
	not examine the packet - this is because mysql_load() sends 
	an OK when it is done
      */
      break;
    }
    else if (packet_len == packet_error)
    {
      error("Failed reading a packet during the dump of %s.", server_fname);
      return ERROR_STOP;
    }
    
    if (packet_len > UINT_MAX)
    {
      error("Illegal length of packet read from net.");
      return ERROR_STOP;
    }
    if (my_write(file, (uchar*) net->read_pos, 
		 (uint) packet_len, MYF(MY_WME|MY_NABP)))
      return ERROR_STOP;
  }
  
  return OK_CONTINUE;
}


/**
  Process the first event in the sequence of events representing a
  LOAD DATA statement.

  Creates a temporary file to be used in LOAD DATA and writes first
  block of data to it. Registers its file name (and optional
  Create_file event) in the array of active temporary files.

  @param bname Base name for temporary file to be created.
  @param blen Base name length.
  @param block First block of data to be loaded.
  @param block_len First block length.
  @param file_id Identifies the LOAD DATA statement.
  @param ce Pointer to Create_file event object if we are processing
  this type of event.

  @retval ERROR_STOP An error occurred - the program should terminate.
  @retval OK_CONTINUE No error, the program should continue.
*/
Exit_status Load_log_processor::process_first_event(const char *bname,
                                                    size_t blen,
                                                    const uchar *block,
                                                    size_t block_len,
                                                    uint file_id,
                                                    Create_file_log_event *ce)
{
  size_t full_len= target_dir_name_len + blen + 9 + 9 + 1;
  Exit_status retval= OK_CONTINUE;
  char *fname, *ptr;
  File file;
  File_name_record rec;
  DBUG_ENTER("Load_log_processor::process_first_event");

  if (!(fname= (char*) my_malloc(PSI_NOT_INSTRUMENTED,
                                 full_len,MYF(MY_WME))))
  {
    error("Out of memory.");
    delete ce;
    DBUG_RETURN(ERROR_STOP);
  }

  memcpy(fname, target_dir_name, target_dir_name_len);
  ptr= fname + target_dir_name_len;
  memcpy(ptr,bname,blen);
  ptr+= blen;
  ptr+= sprintf(ptr, "-%x", file_id);

  if ((file= create_unique_file(fname,ptr)) < 0)
  {
    error("Could not construct local filename %s%s.",
          target_dir_name,bname);
    my_free(fname);
    delete ce;
    DBUG_RETURN(ERROR_STOP);
  }

  rec.fname= fname;
  rec.event= ce;

  /*
     fname is freed in process_event()
     after Execute_load_query_log_event or Execute_load_log_event
     will have been processed, otherwise in Load_log_processor::destroy()
  */
  file_names[file_id]= rec;

  if (ce)
    ce->set_fname_outside_temp_buf(fname, (uint) strlen(fname));

  if (my_write(file, (uchar*)block, block_len, MYF(MY_WME|MY_NABP)))
  {
    error("Failed writing to file.");
    retval= ERROR_STOP;
  }
  if (my_close(file, MYF(MY_WME)))
  {
    error("Failed closing file.");
    retval= ERROR_STOP;
  }
  DBUG_RETURN(retval);
}


/**
  Process the given Create_file_log_event.

  @see Load_log_processor::process_first_event(const char*,uint,const char*,uint,uint,Create_file_log_event*)

  @param ce Create_file_log_event to process.

  @retval ERROR_STOP An error occurred - the program should terminate.
  @retval OK_CONTINUE No error, the program should continue.
*/
Exit_status  Load_log_processor::process(Create_file_log_event *ce)
{
  const char *bname= ce->fname + dirname_length(ce->fname);
  size_t blen= ce->fname_len - (bname-ce->fname);

  return process_first_event(bname, blen, ce->block, ce->block_len,
                             ce->file_id, ce);
}


/**
  Process the given Begin_load_query_log_event.

  @see Load_log_processor::process_first_event(const char*,uint,const char*,uint,uint,Create_file_log_event*)

  @param ce Begin_load_query_log_event to process.

  @retval ERROR_STOP An error occurred - the program should terminate.
  @retval OK_CONTINUE No error, the program should continue.
*/
Exit_status Load_log_processor::process(Begin_load_query_log_event *blqe)
{
  return process_first_event("SQL_LOAD_MB", 11, blqe->block, blqe->block_len,
                             blqe->file_id, 0);
}


/**
  Process the given Append_block_log_event.

  Appends the chunk of the file contents specified by the event to the
  file created by a previous Begin_load_query_log_event or
  Create_file_log_event.

  If the file_id for the event does not correspond to any file
  previously registered through a Begin_load_query_log_event or
  Create_file_log_event, this member function will print a warning and
  return OK_CONTINUE.  It is safe to return OK_CONTINUE, because no
  query will be written for this event.  We should not print an error
  and fail, since the missing file_id could be because a (valid)
  --start-position has been specified after the Begin/Create event but
  before this Append event.

  @param ae Append_block_log_event to process.

  @retval ERROR_STOP An error occurred - the program should terminate.

  @retval OK_CONTINUE No error, the program should continue.
*/
Exit_status Load_log_processor::process(Append_block_log_event *ae)
{
  DBUG_ENTER("Load_log_processor::process");
  File_names::iterator it= file_names.find(ae->file_id);
  const char *fname= ((it != file_names.end()) ?
                      (*it).second.fname : NULL);

  if (fname)
  {
    File file;
    Exit_status retval= OK_CONTINUE;
    if (((file= my_open(fname,
			O_APPEND|O_BINARY|O_WRONLY,MYF(MY_WME))) < 0))
    {
      error("Failed opening file %s", fname);
      DBUG_RETURN(ERROR_STOP);
    }
    if (my_write(file,(uchar*)ae->block,ae->block_len,MYF(MY_WME|MY_NABP)))
    {
      error("Failed writing to file %s", fname);
      retval= ERROR_STOP;
    }
    if (my_close(file,MYF(MY_WME)))
    {
      error("Failed closing file %s", fname);
      retval= ERROR_STOP;
    }
    DBUG_RETURN(retval);
  }

  /*
    There is no Create_file event (a bad binlog or a big
    --start-position). Assuming it's a big --start-position, we just do
    nothing and print a warning.
  */
  warning("Ignoring Append_block as there is no "
          "Create_file event for file_id: %u", ae->file_id);
  DBUG_RETURN(OK_CONTINUE);
}


static Load_log_processor load_processor;




/**
  Indicates whether the given database should be filtered out,
  according to the --database=X option.

  @param log_dbname Name of database.

  @return nonzero if the database with the given name should be
  filtered out, 0 otherwise.
*/


/**
  Checks whether the given event should be filtered out,
  according to the include-gtids, exclude-gtids and
  skip-gtids options.

  @param ev Pointer to the event to be checked.

  @return true if the event should be filtered out,
          false, otherwise.
*/


/**
  Auxiliary function used by error() and warning().

  Prints the given text (normally "WARNING: " or "ERROR: "), followed
  by the given vprintf-style string, followed by a newline.

  @param format Printf-style format string.
  @param args List of arguments for the format string.
  @param msg Text to print before the string.
*/
static void error_or_warning(const char *format, va_list args, const char *msg)
{
  fprintf(stderr, "%s: ", msg);
  vfprintf(stderr, format, args);
  fprintf(stderr, "\n");
}

/**
  Prints a message to stderr, prefixed with the text "ERROR: " and
  suffixed with a newline.

  @param format Printf-style format string, followed by printf
  varargs.
*/
static void error(const char *format,...)
{
  va_list args;
  va_start(args, format);
  error_or_warning(format, args, "ERROR");
  va_end(args);
}


/**
  This function is used in log_event.cc to report errors.

  @param format Printf-style format string, followed by printf
  varargs.
*/
static void sql_print_error(const char *format,...)
{
  va_list args;
  va_start(args, format);
  error_or_warning(format, args, "ERROR");
  va_end(args);
}

/**
  Prints a message to stderr, prefixed with the text "WARNING: " and
  suffixed with a newline.

  @param format Printf-style format string, followed by printf
  varargs.
*/
static void warning(const char *format,...)
{
  va_list args;
  va_start(args, format);
  error_or_warning(format, args, "WARNING");
  va_end(args);
}

/**
  Frees memory for global variables in this file.
*/
static void cleanup()
{
//  my_free(pass);
//  my_free(database);
  my_free(rewrite);
//  my_free(host);
//  my_free(user);
  my_free(dirname_for_local_load);
  if(pass)
  {
    delete pass;
  }
  if(database)
  {
    delete database;
  }

  if(host)
  {
    delete host;
  }

  if(user)
  {
    delete user;
  }



  for (size_t i= 0; i < buff_ev->size(); i++)
  {
    buff_event_info pop_event_array= buff_ev->at(i);
    delete (pop_event_array.event);
  }
  delete buff_ev;

  delete glob_description_event;
  if (mysql)
    mysql_close(mysql);
}


/**
  Create and initialize the global mysql object, and connect to the
  server.

  @retval ERROR_STOP An error occurred - the program should terminate.
  @retval OK_CONTINUE No error, the program should continue.
*/
static Exit_status safe_connect()
{
  /*
    A possible old connection's resources are reclaimed now
    at new connect attempt. The final safe_connect resources
    are mysql_closed at the end of program, explicitly.
  */
  mysql_close(mysql);
  mysql= mysql_init(NULL);

  if (!mysql)
  {
    error("Failed on mysql_init.");
    return ERROR_STOP;
  }

  if (opt_plugin_dir && *opt_plugin_dir)
    mysql_options(mysql, MYSQL_PLUGIN_DIR, opt_plugin_dir);

  if (opt_default_auth && *opt_default_auth)
    mysql_options(mysql, MYSQL_DEFAULT_AUTH, opt_default_auth);

  if (opt_protocol)
    mysql_options(mysql, MYSQL_OPT_PROTOCOL, (char*) &opt_protocol);
  if (opt_bind_addr)
    mysql_options(mysql, MYSQL_OPT_BIND, opt_bind_addr);
  if(net_read_time_out)
  {
    mysql_options(mysql,MYSQL_OPT_READ_TIMEOUT,&net_read_time_out);
  }
  int opt_connect_timeout=2;
  mysql_options(mysql,MYSQL_OPT_CONNECT_TIMEOUT,&opt_connect_timeout);
#if defined (_WIN32) && !defined (EMBEDDED_LIBRARY)
  if (shared_memory_base_name)
    mysql_options(mysql, MYSQL_SHARED_MEMORY_BASE_NAME,
                  shared_memory_base_name);
#endif
  mysql_options(mysql, MYSQL_OPT_CONNECT_ATTR_RESET, 0);
  mysql_options4(mysql, MYSQL_OPT_CONNECT_ATTR_ADD,
                 "program_name", "mysqlbinlog");
  mysql_options4(mysql, MYSQL_OPT_CONNECT_ATTR_ADD,
                "_client_role", "binary_log_listener");

  if (!mysql_real_connect(mysql, host, user, pass, 0, port, sock, 0))
  {
    error("Failed on connect: %s", mysql_error(mysql));
    return ERROR_STOP;
  }
  mysql->reconnect= 1;
  return OK_CONTINUE;
}


/**
  High-level function for dumping a named binlog.

  This function calls dump_remote_log_entries() or
  dump_local_log_entries() to do the job.

  @param[in] logname Name of input binlog.

  @retval ERROR_STOP An error occurred - the program should terminate.
  @retval OK_CONTINUE No error, the program should continue.
  @retval OK_STOP No error, but the end of the specified range of
  events to process has been reached and the program should terminate.
*/
static Exit_status dump_single_log(PRINT_EVENT_INFO *print_event_info,
                                   const char* logname)
{
  DBUG_ENTER("dump_single_log");

  Exit_status rc= OK_CONTINUE;

  switch (opt_remote_proto)
  {
    case BINLOG_DUMP_NON_GTID:
    case BINLOG_DUMP_GTID:
      rc= dump_remote_log_entries(print_event_info, logname);
    break;
    default:
      DBUG_ASSERT(0);
    break;
  }
  return rc;
}


static Exit_status dump_multiple_logs(int argc, char **argv)
{
  DBUG_ENTER("dump_multiple_logs");
  Exit_status rc= OK_CONTINUE;

  PRINT_EVENT_INFO print_event_info;
  if (!print_event_info.init_ok())
    DBUG_RETURN(ERROR_STOP);
  /*
     Set safe delimiter, to dump things
     like CREATE PROCEDURE safely
  */
  my_stpcpy(print_event_info.delimiter, "/*!*/;");
  
  print_event_info.verbose= short_form ? 0 : verbose;
  print_event_info.short_form= short_form;
  print_event_info.base64_output_mode= opt_base64_output_mode;
  print_event_info.skip_gtids= opt_skip_gtids;

  // Dump all logs.
  my_off_t save_stop_position= stop_position;
  stop_position= ~(my_off_t)0;
  stop_position = save_stop_position;
  const char* start_binlog_file="mysql-bin-000001";
  start_position= BIN_LOG_HEADER_SIZE;
  if((rc = dump_single_log(&print_event_info,start_binlog_file)) != OK_CONTINUE)
  {
    error("dump single log error");
  }

  if (!buff_ev->empty())
    warning("The range of printed events ends with an Intvar_event, "
            "Rand_event or User_var_event with no matching Query_log_event. "
            "This might be because the last statement was not fully written "
            "to the log, or because you are using a --stop-position or "
            "--stop-datetime that refers to an event in the middle of a "
            "statement. The event(s) from the partial statement have not been "
            "written to output. ");

  else if (print_event_info.have_unflushed_events)
    warning("The range of printed events ends with a row event or "
            "a table map event that does not have the STMT_END_F "
            "flag set. This might be because the last statement "
            "was not fully written to the log, or because you are "
            "using a --stop-position or --stop-datetime that refers "
            "to an event in the middle of a statement. The event(s) "
            "from the partial statement have not been written to output.");

  /* Set delimiter back to semicolon */
  return(rc);
}


/**
  When reading a remote binlog, this function is used to grab the
  Format_description_log_event in the beginning of the stream.
  
  This is not as smart as check_header() (used for local log); it will
  not work for a binlog which mixes format. TODO: fix this.

  @retval ERROR_STOP An error occurred - the program should terminate.
  @retval OK_CONTINUE No error, the program should continue.
*/
static Exit_status check_master_version()
{
  DBUG_ENTER("check_master_version");
  MYSQL_RES* res = 0;
  MYSQL_ROW row;
  const char* version;

  if (mysql_query(mysql, "SELECT VERSION()") ||
      !(res = mysql_store_result(mysql)))
  {
    error("Could not find server version: "
          "Query failed when checking master version: %s", mysql_error(mysql));
    DBUG_RETURN(ERROR_STOP);
  }
  if (!(row = mysql_fetch_row(res)))
  {
    error("Could not find server version: "
          "Master returned no rows for SELECT VERSION().");
    goto err;
  }

  if (!(version = row[0]))
  {
    error("Could not find server version: "
          "Master reported NULL for the version.");
    goto err;
  }
  /* 
     Make a notice to the server that this client
     is checksum-aware. It does not need the first fake Rotate
     necessary checksummed. 
     That preference is specified below.
  */
  if (mysql_query(mysql, "SET @master_binlog_checksum='NONE'"))
  {
    error("Could not notify master about checksum awareness."
          "Master returned '%s'", mysql_error(mysql));
    goto err;
  }
  delete glob_description_event;
  switch (*version) {
  case '3':
    glob_description_event= new Format_description_log_event(1);
    break;
  case '4':
    glob_description_event= new Format_description_log_event(3);
    break;
  case '5':
    /*
      The server is soon going to send us its Format_description log
      event, unless it is a 5.0 server with 3.23 or 4.0 binlogs.
      So we first assume that this is 4.0 (which is enough to read the
      Format_desc event if one comes).
    */
    glob_description_event= new Format_description_log_event(3);
    break;
  default:
    glob_description_event= NULL;
    error("Could not find server version: "
          "Master reported unrecognized MySQL version '%s'.", version);
    goto err;
  }
  if (!glob_description_event || !glob_description_event->is_valid())
  {
    error("Failed creating Format_description_log_event; out of memory?");
    goto err;
  }

  mysql_free_result(res);
  DBUG_RETURN(OK_CONTINUE);

err:
  mysql_free_result(res);
  DBUG_RETURN(ERROR_STOP);
}


static int get_dump_flags()
{
  return stop_never ? 0 : BINLOG_DUMP_NON_BLOCK;
}



typedef struct Binlog_relay_IO_param {
    uint32 server_id;
    my_thread_id thread_id;

    /* Channel name */
    char* channel_name;

    /* Master host, user and port */
    char *host;
    char *user;
    unsigned int port;

    char *master_log_name;
    my_off_t master_log_pos;

    MYSQL *mysql;                        /* the connection to master */
} Binlog_relay_IO_param;

Binlog_relay_IO_param* binlogRelayIoParam;

/**
  Requests binlog dump from a remote server and prints the events it
  receives.

  @param[in,out] print_event_info Parameters and context state
  determining how to print.
  @param[in] logname Name of input binlog.

  @retval ERROR_STOP An error occurred - the program should terminate.
  @retval OK_CONTINUE No error, the program should continue.
  @retval OK_STOP No error, but the end of the specified range of
  events to process has been reached and the program should terminate.
*/
static Exit_status dump_remote_log_entries(PRINT_EVENT_INFO *print_event_info,
                                           const char* logname)
{
  const char *error_msg= NULL;
  Log_event *ev= NULL;
  Log_event_type type= binary_log::UNKNOWN_EVENT;
  uchar *command_buffer= NULL;
  size_t command_size= 0;
  ulong len= 0;
  ulong len_old;
  size_t logname_len= 0;
  uint server_id= 0;
  NET* net= NULL;
  my_off_t old_off= start_position_mot;
  char fname[FN_REFLEN + 1];
  char log_file_name[FN_REFLEN + 1];
  Exit_status retval= OK_CONTINUE;
  enum enum_server_command command= COM_END;

  fname[0]= log_file_name[0]= 0;

  /*
    Even if we already read one binlog (case of >=2 binlogs on command line),
    we cannot re-use the same connection as before, because it is now dead
    (COM_BINLOG_DUMP kills the thread when it finishes).
  */
  if ((retval= safe_connect()) != OK_CONTINUE)
  {
    return retval;
  }
  net= &mysql->net;

  if ((retval= check_master_version()) != OK_CONTINUE)
  {
    return retval;
  }

  if (connection_server_id != -1)
  {
    server_id= static_cast<uint>(connection_server_id);
  }
  size_t tlen = strlen(logname);
  if (tlen > UINT_MAX) 
  {
    error("Log name too long.");
    return ERROR_STOP;
  }

  binlogRelayIoParam = new Binlog_relay_IO_param;
  binlogRelayIoParam->channel_name="test";
  binlogRelayIoParam->host=host;
  binlogRelayIoParam->user=user;
  binlogRelayIoParam->server_id=server_id;
  binlogRelayIoParam->thread_id = 1;
  binlogRelayIoParam->master_log_name =NULL;
  binlogRelayIoParam->master_log_pos=0;
  binlogRelayIoParam->mysql = mysql;

  if(handle_repl_semi_slave_request_dump((void*)binlogRelayIoParam,0))
  {
    error("call repl_semi_slave_request_dump error");
  }

  size_t BINLOG_NAME_INFO_SIZE= logname_len= tlen;
  
  if (opt_remote_proto == BINLOG_DUMP_NON_GTID)
  {
    command= COM_BINLOG_DUMP;
    size_t allocation_size= ::BINLOG_POS_OLD_INFO_SIZE +
      BINLOG_NAME_INFO_SIZE + ::BINLOG_FLAGS_INFO_SIZE +
      ::BINLOG_SERVER_ID_INFO_SIZE + 1;
    if (!(command_buffer= (uchar *) my_malloc(PSI_NOT_INSTRUMENTED,
                                              allocation_size, MYF(MY_WME))))
    {
      error("Got fatal error allocating memory.");
      return ERROR_STOP;
    }
    uchar* ptr_buffer= command_buffer;

    /*
      COM_BINLOG_DUMP accepts only 4 bytes for the position, so
      we are forced to cast to uint32.
    */
    int4store(ptr_buffer, (uint32) start_position);
    ptr_buffer+= ::BINLOG_POS_OLD_INFO_SIZE;
    int2store(ptr_buffer, get_dump_flags());
    ptr_buffer+= ::BINLOG_FLAGS_INFO_SIZE;
    int4store(ptr_buffer, server_id);
    ptr_buffer+= ::BINLOG_SERVER_ID_INFO_SIZE;
    memcpy(ptr_buffer, logname, BINLOG_NAME_INFO_SIZE);
    ptr_buffer+= BINLOG_NAME_INFO_SIZE;

    command_size= ptr_buffer - command_buffer;
    DBUG_ASSERT(command_size == (allocation_size - 1));
  }
  else
  {

    bool suppress_warnings;
    register_slave_on_master(mysql,&suppress_warnings);
    command= COM_BINLOG_DUMP_GTID;
    char* real_log_name="";
    BINLOG_NAME_INFO_SIZE= strlen(real_log_name);


    global_sid_lock->rdlock();

    // allocate buffer
    size_t encoded_data_size= gtid_set_excluded->get_encoded_length();
    size_t allocation_size=
            ::BINLOG_FLAGS_INFO_SIZE + ::BINLOG_SERVER_ID_INFO_SIZE +
            ::BINLOG_NAME_SIZE_INFO_SIZE + BINLOG_NAME_INFO_SIZE +
            ::BINLOG_POS_INFO_SIZE + ::BINLOG_DATA_SIZE_INFO_SIZE +
            encoded_data_size + 1;

    if (!(command_buffer= (uchar *) my_malloc(PSI_NOT_INSTRUMENTED,
                                              allocation_size, MYF(MY_WME))))
    {
      error("Got fatal error allocating memory.");
      global_sid_lock->unlock();
      return ERROR_STOP;
    }
    uchar* ptr_buffer= command_buffer;
    int2store(ptr_buffer, get_dump_flags());
    ptr_buffer+= ::BINLOG_FLAGS_INFO_SIZE;
    int4store(ptr_buffer, server_id);
    ptr_buffer+= ::BINLOG_SERVER_ID_INFO_SIZE;
    int4store(ptr_buffer, static_cast<uint32>(BINLOG_NAME_INFO_SIZE));
    ptr_buffer+= ::BINLOG_NAME_SIZE_INFO_SIZE;
    memcpy(ptr_buffer, logname, BINLOG_NAME_INFO_SIZE);
    ptr_buffer+= BINLOG_NAME_INFO_SIZE;
    int8store(ptr_buffer, start_position);
    ptr_buffer+= ::BINLOG_POS_INFO_SIZE;
    int4store(ptr_buffer, static_cast<uint32>(encoded_data_size));
    ptr_buffer+= ::BINLOG_DATA_SIZE_INFO_SIZE;
    gtid_set_excluded->encode(ptr_buffer);
    ptr_buffer+= encoded_data_size;

    global_sid_lock->unlock();

    command_size= ptr_buffer - command_buffer;
    DBUG_ASSERT(command_size == (allocation_size - 1));
  }

  if (simple_command(mysql, command, command_buffer, command_size, 1))
  {
    error("Got fatal error sending the log dump command.");
    my_free(command_buffer);
    return ERROR_STOP;
  }
  my_free(command_buffer);

  unsigned long int total_bytes=0;
  char new_binlog_file_name[FN_REFLEN + 1];
  for (;;)
  {
    len = cli_safe_read(mysql, NULL);
    if (len == packet_error)
    {

      error("Got error reading packet from server: %s", mysql_error(mysql));
      return ERROR_STOP;
    }
    len--;
    if (len < 8 && net->read_pos[0] == 254)
      break; // end of data
//      DBUG_PRINT("info",( "len: %lu  net->read_pos[5]: %d\n",
//			len, net->read_pos[5]));
    /*
      In raw mode We only need the full event details if it is a 
      ROTATE_EVENT or FORMAT_DESCRIPTION_EVENT
    */
    const char* event_buf;
    event_buf= (const char *) net->read_pos + 1;
    if(handle_repl_semi_slave_read_event((void*)binlogRelayIoParam,(char*)net->read_pos+1,len,&event_buf,&len))
    {
      error("call handle_repl_semi_slave_read_event error");
    }
    type=(Log_event_type)event_buf[EVENT_TYPE_OFFSET];

    if (type == binary_log::HEARTBEAT_LOG_EVENT)
    {
      continue;
    }

    if (!raw_mode || (type == binary_log::ROTATE_EVENT) ||
        (type == binary_log::FORMAT_DESCRIPTION_EVENT))
    {
      if (!(ev= Log_event::read_log_event(event_buf,
                                          len, &error_msg,
                                          glob_description_event,
                                          opt_verify_binlog_checksum)))
      {
        error("Could not construct log event object: %s", error_msg);
        return ERROR_STOP;
      }
      /*
        If reading from a remote host, ensure the temp_buf for the
        Log_event class is pointing to the incoming stream.
      */
      ev->register_temp_buf((char*)event_buf);
    }
    if (raw_mode || (type != binary_log::LOAD_EVENT))
    {
      /*
        If this is a Rotate event, maybe it's the end of the requested binlog;
        in this case we are done (stop transfer).
        This is suitable for binlogs, not relay logs (but for now we don't read
        relay logs remotely because the server is not able to do that). If one
        day we read relay logs remotely, then we will have a problem with the
        detection below: relay logs contain Rotate events which are about the
        binlogs, so which would trigger the end-detection below.
      */
      if (type == binary_log::ROTATE_EVENT)
      {
       // error("last total bytes %lu",total_bytes);
        total_bytes =0;
        Rotate_log_event *rev= (Rotate_log_event *)ev;
        /*
          If this is a fake Rotate event, and not about our log, we can stop
          transfer. If this a real Rotate event (so it's not about our log,
          it's in our log describing the next log), we print it (because it's
          part of our log) and then we will stop when we receive the fake one
          soon.
        */
        if (raw_mode)
        {
          if (output_file != 0)
          {
            my_snprintf(log_file_name, sizeof(log_file_name), "%s%s",
                        output_file, rev->new_log_ident);
            memset(new_binlog_file_name,0,(FN_REFLEN + 1));
            my_stpcpy(new_binlog_file_name, rev->new_log_ident);
          }
          else
          {
            my_stpcpy(log_file_name, rev->new_log_ident);
            memset(new_binlog_file_name,0,(FN_REFLEN + 1));
            my_stpcpy(new_binlog_file_name, rev->new_log_ident);
          }
        }

        if (rev->common_header->when.tv_sec == 0)
        {
          if (!to_last_remote_log)
          {
//            if ((rev->ident_len != logname_len) ||
//                memcmp(rev->new_log_ident, logname, logname_len))
//            {
//              reset_temp_buf_and_delete(rev);
//              DBUG_RETURN(OK_CONTINUE);
//            }
            /*
              Otherwise, this is a fake Rotate for our log, at the very
              beginning for sure. Skip it, because it was not in the original
              log. If we are running with to_last_remote_log, we print it,
              because it serves as a useful marker between binlogs then.
            */
            reset_temp_buf_and_delete(rev);
            continue;
          }
          /*
             Reset the value of '# at pos' field shown against first event of
             next binlog file (fake rotate) picked by mysqlbinlog --to-last-log
         */
          old_off= start_position_mot;
          len= 0; // fake Rotate, so don't increment old_off /*ashe note: this len is real buf len to write,so 0*/
        }
      }
      else if (type == binary_log::FORMAT_DESCRIPTION_EVENT)
      {
        /*
          This could be an fake Format_description_log_event that server
          (5.0+) automatically sends to a slave on connect, before sending
          a first event at the requested position.  If this is the case,
          don't increment old_off. Real Format_description_log_event always
          starts from BIN_LOG_HEADER_SIZE position.
        */
        // fake event when not in raw mode, don't increment old_off
        if ((old_off != BIN_LOG_HEADER_SIZE) && (!raw_mode))
          len= 1;
        if (raw_mode)
        {
          if (result_file && (result_file != stdout))
            my_fclose(result_file, MYF(0));
          if (!(result_file = my_fopen(log_file_name, O_WRONLY | O_BINARY,
                                       MYF(MY_WME))))
          {
            error("Could not create log file '%s'", log_file_name);
            return ERROR_STOP;
          }
          DBUG_EXECUTE_IF("simulate_result_file_write_error_for_FD_event",
                          DBUG_SET("+d,simulate_fwrite_error"););
          if (my_fwrite(result_file, (const uchar*) BINLOG_MAGIC,
                        BIN_LOG_HEADER_SIZE, MYF(MY_NABP)))
          {
            error("Could not write into log file '%s'", log_file_name);
            return ERROR_STOP;
          }

          total_bytes+=4; //BINLOG_MAGIC is 4 bytes.

          /*
            Need to handle these events correctly in raw mode too 
            or this could get messy
          */
          delete glob_description_event;
          glob_description_event= (Format_description_log_event*) ev;
          print_event_info->common_header_len= glob_description_event->common_header_len;
          ev->temp_buf= 0;
          ev= 0;
        }
      }
      
      if (type == binary_log::LOAD_EVENT)
      {
        DBUG_ASSERT(raw_mode);
        warning("Attempting to load a remote pre-4.0 binary log that contains "
                "LOAD DATA INFILE statements. The file will not be copied from "
                "the remote server. ");
      }

      if (raw_mode)
      {
        DBUG_EXECUTE_IF("simulate_result_file_write_error",
                        DBUG_SET("+d,simulate_fwrite_error"););
        if (my_fwrite(result_file, (const uchar*)event_buf, len, MYF(MY_NABP)))
        {
          error("Could not write into log file '%s'", log_file_name);
          retval= ERROR_STOP;
        }
        total_bytes += len;
        if (ev)
          reset_temp_buf_and_delete(ev);
      }
      else
      {
        error("Could not recognizer aw_mode %s,%s",__FILE__,__LINE__);
      }

      if (retval != OK_CONTINUE)
      {
        return retval;
      }

    }
    else
    {
      error("Could not recognizer aw_mode %s,%s",__FILE__,__LINE__);
    }

    /*
      Let's adjust offset for remote log as for local log to produce
      similar text and to have --stop-position to work identically.
    */
    old_off+= len-1;
    binlogRelayIoParam->master_log_name = new_binlog_file_name;
    binlogRelayIoParam->master_log_pos = total_bytes;
    handle_repl_semi_slave_queue_event((void*)binlogRelayIoParam,event_buf,0,0);

  }

  return OK_CONTINUE;
}



/* Post processing of arguments to check for conflicts and other setups */
static int args_post_process(void)
{
  DBUG_ENTER("args_post_process");

  if (opt_remote_alias && opt_remote_proto != BINLOG_DUMP_NON_GTID)
  {
    error("The option read-from-remote-server cannot be used when "
          "read-from-remote-master is defined and is not equal to "
          "BINLOG-DUMP-NON-GTIDS");
    DBUG_RETURN(ERROR_STOP);
  }

  if (raw_mode)
  {
    if (one_database)
      warning("The --database option is ignored with --raw mode");

    if (opt_remote_proto == BINLOG_LOCAL)
    {
      error("The --raw flag requires one of --read-from-remote-master or --read-from-remote-server");
      DBUG_RETURN(ERROR_STOP);
    }

    if (opt_include_gtids_str != NULL)
    {
      error("You cannot use --include-gtids and --raw together.");
      DBUG_RETURN(ERROR_STOP);
    }

    if (opt_remote_proto == BINLOG_DUMP_NON_GTID &&
        opt_exclude_gtids_str != NULL)
    {
      error("You cannot use both of --exclude-gtids and --raw together "
            "with one of --read-from-remote-server or "
            "--read-from-remote-master=BINLOG-DUMP-NON-GTID.");
      DBUG_RETURN(ERROR_STOP);
    }
  }
  else if (output_file)
  {
    if (!(result_file = my_fopen(output_file, O_WRONLY | O_BINARY, MYF(MY_WME))))
    {
      error("Could not create log file '%s'", output_file);
      DBUG_RETURN(ERROR_STOP);
    }
  }

  global_sid_lock->rdlock();

  if (opt_include_gtids_str != NULL)
  {
    if (gtid_set_included->add_gtid_text(opt_include_gtids_str) !=
        RETURN_STATUS_OK)
    {
      error("Could not configure --include-gtids '%s'", opt_include_gtids_str);
      global_sid_lock->unlock();
      DBUG_RETURN(ERROR_STOP);
    }
  }

  if (opt_exclude_gtids_str != NULL)
  {
    if (gtid_set_excluded->add_gtid_text(opt_exclude_gtids_str) !=
        RETURN_STATUS_OK)
    {
      error("Could not configure --exclude-gtids '%s'", opt_exclude_gtids_str);
      global_sid_lock->unlock();
      DBUG_RETURN(ERROR_STOP);
    }
  }

  global_sid_lock->unlock();

  if (connection_server_id == 0 && stop_never)
    error("Cannot set --server-id=0 when --stop-never is specified.");
  if (connection_server_id != -1 && stop_never_slave_server_id != -1)
    error("Cannot set --connection-server-id= %lld and"
          "--stop-never-slave-server-id= %lld. ", connection_server_id,
          stop_never_slave_server_id);

  DBUG_RETURN(OK_CONTINUE);
}

/**
   GTID cleanup destroys objects and reset their pointer.
   Function is reentrant.
*/
inline void gtid_client_cleanup()
{
  delete global_sid_lock;
  delete global_sid_map;
  delete gtid_set_excluded;
  delete gtid_set_included;
  global_sid_lock= NULL;
  global_sid_map= NULL;
  gtid_set_excluded= NULL;
  gtid_set_included= NULL;
}

/**
   GTID initialization.

   @return true if allocation does not succeed
           false if OK
*/
inline bool gtid_client_init()
{
  bool res=
    (!(global_sid_lock= new Checkable_rwlock) ||
     !(global_sid_map= new Sid_map(global_sid_lock)) ||
     !(gtid_set_excluded= new Gtid_set(global_sid_map)) ||
     !(gtid_set_included= new Gtid_set(global_sid_map)));
  if (res)
  {
    gtid_client_cleanup();
  }
  return res;
}


int main(int argc, char** argv)
{
  char **defaults_argv;
  Exit_status retval= OK_CONTINUE;
  MY_INIT(argv[0]);
  DBUG_ENTER("main");
  DBUG_PROCESS(argv[0]);
  if(symisync_slave_init())
  {
    error("init semisync_slave error");
    return 1;
  }

  my_init_time(); // for time functions
  tzset(); // set tzname
  /*
    A pointer of type Log_event can point to
     INTVAR
     USER_VAR
     RANDOM
    events.
  */
  buff_ev= new Buff_ev(PSI_NOT_INSTRUMENTED);

//  my_getopt_use_args_separator= TRUE;
//  if (load_defaults("my", load_default_groups, &argc, &argv))
//    exit(1);
//  my_getopt_use_args_separator= FALSE;
//  defaults_argv= argv;
//
//  parse_args(&argc, &argv);
  if(argc != 2)
  {
    error("There are too many parameters.\nusage: virtual_slave virtual_slave.cnf");
    exit(1);
  }

  //read config file.
  Config virtual_slave_config(argv[1]);

  string _s_user = virtual_slave_config.Read("master_user",user);
  user = string_to_char(_s_user);

  string _s_host = virtual_slave_config.Read("master_host",host);
  host = string_to_char(_s_host);

  port = virtual_slave_config.Read("master_port",0);

  string _s_pass = virtual_slave_config.Read("master_password",pass);
  pass = string_to_char(_s_pass);

  heartbeat_period = virtual_slave_config.Read("heartbeat_period",0);
  net_read_time_out = virtual_slave_config.Read("net_read_time_out",0);
  int _int_opt_remote_proto = virtual_slave_config.Read("opt_remote_proto",_int_opt_remote_proto);
  opt_remote_proto = (enum_remote_proto)_int_opt_remote_proto;

  get_start_gtid_mode = virtual_slave_config.Read("get_start_gtid_mode",0);
  connection_server_id = virtual_slave_config.Read("virtual_slave_server_id",0);
  raw_mode = virtual_slave_config.Read("raw_mode",0);
  stop_never = virtual_slave_config.Read("stop_never",0);

  string _s_output_file = virtual_slave_config.Read("binlog_dir",_s_output_file);
  output_file = string_to_char(_s_output_file);
  string _s_opt_exclude_gtids_str = virtual_slave_config.Read("exclude_gtids",_s_opt_exclude_gtids_str);
  opt_exclude_gtids_str = string_to_char(_s_opt_exclude_gtids_str);

  if (gtid_client_init())
  {
    error("Could not initialize GTID structuress.");
    exit(1);
  }

  umask(((~my_umask) & 0666));
  /* Check for argument conflicts and do any post-processing */
  if (args_post_process() == ERROR_STOP)
    exit(1);

  opt_server_id_mask = (opt_server_id_bits == 32)?
    ~ ulong(0) : (1 << opt_server_id_bits) -1;

  my_set_max_open_files(open_files_limit);

  MY_TMPDIR tmpdir;
  tmpdir.list= 0;
  if (!dirname_for_local_load)
  {
    if (init_tmpdir(&tmpdir, 0))
      exit(1);
    dirname_for_local_load= my_strdup(PSI_NOT_INSTRUMENTED,
                                      my_tmpdir(&tmpdir), MY_WME);
  }

  if (dirname_for_local_load)
    load_processor.init_by_dir_name(dirname_for_local_load);
  else
    load_processor.init_by_cur_dir();

  /*
    In case '--idempotent' or '-i' options has been used, we will notify the
    server to use idempotent mode for the following events.
   */
  retval= dump_multiple_logs(argc, argv);

  /*
    We should unset the RBR_EXEC_MODE since the user may concatenate output of
    multiple runs of mysqlbinlog, all of which may not run in idempotent mode.
   */

  if (tmpdir.list)
    free_tmpdir(&tmpdir);
  if (result_file && (result_file != stdout))
    my_fclose(result_file, MYF(0));
  cleanup();

  if (defaults_argv)
    free_defaults(defaults_argv);
  my_free_open_file_info();
  load_processor.destroy();
  /* We cannot free DBUG, it is used in global destructors after exit(). */
  my_end(my_end_arg | MY_DONT_FREE_DBUG);
  gtid_client_cleanup();

  exit(retval == ERROR_STOP ? 1 : 0);
  /* Keep compilers happy. */
  DBUG_RETURN(retval == ERROR_STOP ? 1 : 0);
}

/*
  We must include this here as it's compiled with different options for
  the server
*/

#include "decimal.c"
#include "my_decimal.cc"
#include "log_event.cc"
#include "log_event_old.cc"
#include "rpl_utility.cc"
#include "rpl_gtid_sid_map.cc"
#include "rpl_gtid_misc.cc"
#include "rpl_gtid_set.cc"
#include "rpl_gtid_specification.cc"
#include "rpl_tblmap.cc"


/**
  Faster net_store_length when we know that length is less than 65536.
  We keep a separate version for that range because it's widely used in
  libmysql.

  uint is used as agrument type because of MySQL type conventions:
    - uint for 0..65536
    - ulong for 0..4294967296
    - ulonglong for bigger numbers.
*/

static uchar *net_store_length_fast(uchar *packet, size_t length)
{
  if (length < 251)
  {
    *packet=(uchar) length;
    return packet+1;
  }
  *packet++=252;
  int2store(packet,(uint) length);
  return packet+2;
}

/****************************************************************************
  Functions used by the protocol functions (like net_send_ok) to store
  strings and numbers in the header result packet.
****************************************************************************/

/* The following will only be used for short strings < 65K */

uchar *net_store_data(uchar *to, const uchar *from, size_t length)
{
  to=net_store_length_fast(to,length);
  memcpy(to,from,length);
  return to+length;
}

int register_slave_on_master(MYSQL* mysql,/* Master_info *mi,*/
                             bool *suppress_warnings)
{
  uchar buf[1024], *pos= buf;
  size_t report_host_len=0, report_user_len=0, report_password_len=0;
  DBUG_ENTER("register_slave_on_master");

  *suppress_warnings= FALSE;
  if (report_host)
    report_host_len= strlen(report_host);
  if (report_host_len > HOSTNAME_LENGTH)
  {
    // todo log here
//    sql_print_warning("The length of report_host is %zu. "
//                              "It is larger than the max length(%d), so this "
//                              "slave cannot be registered to the master%s.",
//                      report_host_len, HOSTNAME_LENGTH,
//                      mi->get_for_channel_str());
    DBUG_RETURN(0);
  }

  if (report_user)
    report_user_len= strlen(report_user);
  if (report_user_len > USERNAME_LENGTH)
  {
    //todo log here
//    sql_print_warning("The length of report_user is %zu. "
//                              "It is larger than the max length(%d), so this "
//                              "slave cannot be registered to the master%s.",
//                      report_user_len, USERNAME_LENGTH, mi->get_for_channel_str());
    DBUG_RETURN(0);
  }

  if (report_password)
    report_password_len= strlen(report_password);
  if (report_password_len > MAX_PASSWORD_LENGTH)
  {
    //todo log here
//    sql_print_warning("The length of report_password is %zu. "
//                              "It is larger than the max length(%d), so this "
//                              "slave cannot be registered to the master%s.",
//                      report_password_len, MAX_PASSWORD_LENGTH,
//                      mi->get_for_channel_str());
    DBUG_RETURN(0);
  }

  int4store(pos,connection_server_id); pos+= 4;
  pos= net_store_data(pos, (uchar*) report_host, report_host_len);
  pos= net_store_data(pos, (uchar*) report_user, report_user_len);
  pos= net_store_data(pos, (uchar*) report_password, report_password_len);
  int2store(pos, (uint16) report_port); pos+= 2;
  /*
    Fake rpl_recovery_rank, which was removed in BUG#13963,
    so that this server can register itself on old servers,
    see BUG#49259.
   */
  int4store(pos, /* rpl_recovery_rank */ 0);    pos+= 4;
  /* The master will fill in master_id */
  int4store(pos, 0);                    pos+= 4;

  if (simple_command(mysql, COM_REGISTER_SLAVE, buf, (size_t) (pos- buf), 0))
  {
    if (mysql_errno(mysql) == ER_NET_READ_INTERRUPTED)
    {
      *suppress_warnings= TRUE;                 // Suppress reconnect warning
    }
//    else if (!check_io_slave_killed(mi->info_thd, mi, NULL))
//    {
//      char buf[256];
//      my_snprintf(buf, sizeof(buf), "%s (Errno: %d)", mysql_error(mysql),
//                  mysql_errno(mysql));
//      mi->report(ERROR_LEVEL, ER_SLAVE_MASTER_COM_FAILURE,
//                 ER(ER_SLAVE_MASTER_COM_FAILURE), "COM_REGISTER_SLAVE", buf);
//    }
    DBUG_RETURN(1);
  }

  if(set_heartbeat_period(mysql) !=0)
  {
    //todo log here
    return -1;
  }

  if(set_slave_uuid(mysql) !=0)
  {
    //todo log here
    return -1;
  }
  DBUG_RETURN(0);
}


/**
 * set replication heartbeat period.
 * @param mysql
 * @return -1 failed; 0 successfully.
 */
int set_heartbeat_period(MYSQL* mysql)
{
  char llbuf[22];
  const char query_format[]= "SET @master_heartbeat_period= %s";
  char query[sizeof(query_format) - 2 + sizeof(llbuf)];
  /*
     the period is an ulonglong of nano-secs.
  */
  llstr((ulonglong) (heartbeat_period*1000000000UL), llbuf);
  sprintf(query, query_format, llbuf);
  if(mysql_real_query(mysql,query,static_cast<ulong>(strlen(query))))
  {
    error("%s error %s,%i",query,mysql_error(mysql),mysql_errno(mysql));
    return -1;
  }
  return 0;
}

int set_slave_uuid(MYSQL* mysql)
{
  char* query = new char[100];
  sprintf(query,"SET @slave_uuid= '63cf7450-9829-11e7-8a58-000c2985ca33'");
  if(mysql_real_query(mysql,query,strlen(query)))
  {
    error("%s error %s,%i",query,mysql_error(mysql),mysql_errno(mysql));
    delete[] query;
    return -1;
  }
  return 0;
}

char* string_to_char(string str)
{
  char* p  = new char[str.length()+1];
  for(int i=0;i<str.length();i++)
  {
    p[i]=str[i];
  }
  p[str.length()] = '\0';
  return p;
}