#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdlib.h>
#include <limits.h>
#include <time.h>
#include <ctype.h>
#include <assert.h>
#include <libgen.h>
#include <getopt.h>
#include "cas_cci.h"
#include "cci_applier.h"
#include "log_applier.h"

#define free_and_init(ptr) \
        do { \
          free ((ptr)); \
          (ptr) = NULL; \
        } while (0)

#define CA_IS_DML(stmt) \
  ((stmt) == CUBRID_STMT_UPDATE \
   || (stmt) == CUBRID_STMT_DELETE \
   || (stmt) == CUBRID_STMT_INSERT)

#define PROG_NAME                       "cci_applier"
#define CA_DEFAULT_COMMIT_INTERVAL      5000
#define CA_MAX_SAMPLE_FILE_SIZE         (100 * 1024 * 1024)

#define FILE_ID_FORMAT  "%d"
#define SQL_ID_FORMAT   "%010u"
#define CATALOG_FORMAT  FILE_ID_FORMAT " | " SQL_ID_FORMAT
#define ALTER_SERIAL_PREFIX    "ALTER SERIAL"
#define IS_ALTER_SERIAL(stmt) \
  (strncmp (stmt, ALTER_SERIAL_PREFIX, strlen (ALTER_SERIAL_PREFIX)) == 0)
#define DOES_TRAN_START(stmt) \
  (strncmp (stmt, CA_MARK_TRAN_START, strlen (CA_MARK_TRAN_START)) == 0)
#define DOES_TRAN_END(stmt) \
  (strncmp (stmt, CA_MARK_TRAN_END, strlen (CA_MARK_TRAN_END)) == 0)

typedef struct con_info CA_CON_INFO;
struct con_info
{
  char *db_name;
  char *hostname;
  int port;
  char db_user[4];
  char *password;
};

typedef struct ca_info CA_INFO;
struct ca_info
{
  int curr_file_id;
  unsigned int last_applied_sql_id;

  /* SQL meta info */
  unsigned int meta_sql_id;
  int meta_sql_length;
  int meta_sample_length;

  /* info generated by applylogdb */
  int src_file_id;
  unsigned int src_last_inserted_sql_id;

  int commit_interval;
  int sampling_rate;

  int sample_file_count;

  bool ignore_serial;
  bool retain_log;
};

CA_INFO ca_Info;

static char base_log_path[PATH_MAX];
static char ca_catalog_path[PATH_MAX];
static char applylogdb_catalog_path[PATH_MAX];
static char err_file_path[PATH_MAX];
static char sample_file_path_base[PATH_MAX];

static struct option cci_applier_options[] = {
  {"host", 1, 0, 'h'},
  {"port", 1, 0, 'P'},
  {"database", 1, 0, 'd'},
  {"passwd", 1, 0, 'p'},
  {"log-path", 1, 0, 'L'},
  {"sample-rate", 1, 0, 's'},
  {"commit-interval", 1, 0, 'c'},
  {"ignore-serial", 0, 0, 'i'},
  {"retain-log", 0, 0, 'r'},
  {0, 0, 0, 0}
};

static void init_con_info (CA_CON_INFO * con_info);
static void init_ca_Info (void);
static void set_file_path (CA_CON_INFO * con_info, char *repl_log_path);
static FILE *open_sample_file (void);

static void er_log (int error_code, const char *query, const char *err_msg);
static void er_log_cci (int res, const char *query, T_CCI_ERROR * error);

static int apply_sql_logs (int conn);
static int commit_sql_logs (int conn, T_CCI_ERROR * error);

static int execute_sql_query (int conn, char *query, T_CCI_ERROR * error);
static int read_sql_query (FILE * fp, char **query, int length);

static int read_catalog_file (int *file_id, unsigned int *sql_id, char *path, bool need_to_create);
static int read_ca_catalog (void);
static int read_src_catalog (void);
static int update_ca_catalog (void);
static int read_sql_meta_info (FILE * fp);

static void print_usage_and_exit (void);
static void print_progress (unsigned int count, int elapsed_time);
static void print_result (int fail_count);

static void skip_sample_query (FILE * fp, int length);
static void skip_sql_query (FILE * fp, int length);
static int process_sql_log_file (FILE * fp, int conn);
static int process_sample_query (FILE * fp, char **sample, int length);

/*
 * update_ca_catalog() - update progress into file
 *
 *   return: error code
 */
static int
update_ca_catalog (void)
{
  FILE *fp = NULL;
  fp = fopen (ca_catalog_path, "w");
  fseek (fp, 0, SEEK_SET);

  fprintf (fp, CATALOG_FORMAT, ca_Info.curr_file_id, ca_Info.last_applied_sql_id);
  fclose (fp);

  return ER_CA_NO_ERROR;
}

/*
 * read_src_catalog() - read catalog created by applylogdb
 *
 *   return: error code
 */
static int
read_src_catalog (void)
{
  return read_catalog_file (&ca_Info.src_file_id, &ca_Info.src_last_inserted_sql_id, applylogdb_catalog_path, false);
}

/*
 * read_ca_catalog() - read catalog created by cci_applier
 *
 *   return: error code
 */
static int
read_ca_catalog (void)
{
  return read_catalog_file (&ca_Info.curr_file_id, &ca_Info.last_applied_sql_id, ca_catalog_path, true);
}

/*
 * read_catalog_file() - read catalog file
 *   return: error code
 *
 *   file_id(out): sql log file id
 *   sql_id(out): sql id
 *   path(in): path to catalog file
 *   need_to_create(in): create if a file does not exists
 */
static int
read_catalog_file (int *file_id, unsigned int *sql_id, char *path, bool need_to_create)
{
  FILE *fp = NULL;
  char buf[LINE_MAX];
  char err_msg[LINE_MAX];

  assert (path != NULL);

  fp = fopen (path, "r");
  if (fp == NULL)
    {
      if (need_to_create == true)
	{
	  *file_id = 0;
	  *sql_id = 0;

	  fp = fopen (path, "w");
	  fprintf (fp, CATALOG_FORMAT, *file_id, *sql_id);
	  goto end_read_catalog;
	}
      else
	{
	  snprintf (err_msg, LINE_MAX, "Cannot find %s", path);
	  er_log (ER_CA_FILE_IO, NULL, err_msg);

	  return ER_CA_FILE_IO;
	}
    }
  else
    {
      if (fgets (buf, LINE_MAX, fp))
	{
	  if (sscanf (buf, CATALOG_FORMAT, file_id, sql_id) == 2)
	    {
	      goto end_read_catalog;
	    }
	}
      snprintf (err_msg, LINE_MAX, "Failed to read catalog info in %s", path);
      er_log (ER_CA_FILE_IO, NULL, err_msg);

      fclose (fp);
      return ER_CA_FILE_IO;
    }

end_read_catalog:
  fclose (fp);
  return ER_CA_NO_ERROR;
}

/*
 * read_sql_meta_info() - read meta info of each SQL
 *   return: error code
 *
 *   fp(in): file to read
 */
static int
read_sql_meta_info (FILE * fp)
{
  /* -- datetime | sql_id | sample length | query length "-- %19s | %u | %d | %d\n" */
  unsigned int sql_id;
  int query_length, sample_length;

  char meta_info[LINE_MAX];

  if (fgets (meta_info, LINE_MAX, fp) == NULL)
    {
      if (feof (fp))
	{
	  return EOF;
	}
      else
	{
	  return ER_CA_FILE_IO;
	}
    }


  if (sscanf (meta_info, "-- %*[^|]| %u | %d | %d\n", &sql_id, &sample_length, &query_length) != 3)
    {
      return ER_CA_FILE_IO;
    }

  ca_Info.meta_sql_id = sql_id;
  ca_Info.meta_sql_length = query_length;
  ca_Info.meta_sample_length = sample_length;

  return ER_CA_NO_ERROR;
}

/*
 * process_sample_query() - read sample query
 *   return: error code
 *
 *   in(in): file to read
 *   sample(out): sample select query
 *   length(in): length of sample select query
 */
static int
process_sample_query (FILE * fp, char **sample, int length)
{
  FILE *out;
  fpos_t pos;

  assert (*sample == NULL);
  assert (length > 0);

  fgetpos (fp, &pos);

  *sample = (char *) malloc (sizeof (char) * length + 1);
  if (*sample == NULL)
    {
      goto read_sample_error;
    }

  /* skip "-- " in the front */
  fseek (fp, 3, SEEK_CUR);

  if (fread (*sample, sizeof (char), length, fp) != (size_t) length)
    {
      goto read_sample_error;
    }
  /* skip newline at the end */
  fseek (fp, 1, SEEK_CUR);

  if ((out = open_sample_file ()) == NULL)
    {
      free_and_init (*sample);
      return ER_CA_FAILED;
    }

  fwrite (*sample, sizeof (char), length, out);
  fputc ('\n', out);

  fflush (out);
  fclose (out);

  free_and_init (*sample);

  return ER_CA_NO_ERROR;

read_sample_error:

  fsetpos (fp, &pos);
  skip_sample_query (fp, length);

  free_and_init (*sample);

  return ER_CA_FAILED;
}

/*
 * skip_sample_query() - skip sample select query
 *   return: error code
 *
 *   fp(in): file to read
 *   length(in): length of sample select query
 */
static void
skip_sample_query (FILE * fp, int length)
{
  if (length > 0)
    {
      /* skip "-- sample_query\n" */
      fseek (fp, 3 + length + 1, SEEK_CUR);
    }
}

/*
 * skip_sql_query() - skip query
 *   return: error code
 *
 *   fp(in): file to read
 *   length(in): length of query
 */
static void
skip_sql_query (FILE * fp, int length)
{
  if (length > 0)
    {
      /* skip "query\n" */
      fseek (fp, length + 1, SEEK_CUR);
    }
}

/*
 * read_sql_query() - read SQL query
 *   return: error code
 *
 *   fp(in): file to read
 *   query(out): retrieved SQL query string
 *   length(in): length of query string
 */
static int
read_sql_query (FILE * fp, char **query, int length)
{
  *query = (char *) malloc (sizeof (char) * (length + 1));
  if (*query == NULL)
    {
      return ER_CA_FAILED_TO_ALLOC;
    }
  memset (*query, 0, length + 1);

  if (fread (*query, sizeof (char), length, fp) != (size_t) length)
    {
      free_and_init (*query);
      return ER_CA_FILE_IO;
    }

  /* skip newline at the end */
  fseek (fp, 1, SEEK_CUR);

  return ER_CA_NO_ERROR;
}

/*
 * execute_sql_query() - apply SQL query into target DB
 *   return: cci error code
 *
 *   conn(in):
 *   query(in): query to be applied
 *   error(out):
 */
static int
execute_sql_query (int conn, char *query, T_CCI_ERROR * error)
{
  int req, res;
  T_CCI_CUBRID_STMT stmt_type;
  char err_msg[LINE_MAX];

  req = cci_prepare_and_execute (conn, query, 0, &res, error);

  while (req < 0 && CA_RETRY_ON_ERROR (error->err_code))
    {
      snprintf (err_msg, LINE_MAX, "attempts to try applying failed SQL log again - %s", error->err_msg);
      er_log (error->err_code, query, err_msg);

      sleep (10);

      req = cci_prepare_and_execute (conn, query, 0, &res, error);
    }

  if (req < 0)
    {
      er_log_cci (req, query, error);
      return req;
    }

  cci_get_result_info (req, &stmt_type, NULL);

  if (res == 0 && CA_IS_DML (stmt_type))
    {
      er_log (res, query, "No record has been applied");
    }

  cci_close_req_handle (req);

  return CCI_ER_NO_ERROR;
}

/*
 * process_sql_log_file() - process each SQL log file created by applylogdb
 *   return: error code
 *
 *   fp(in):
 *   conn(in):
 */
static int
process_sql_log_file (FILE * fp, int conn)
{
  T_CCI_ERROR cci_error;
  int error;
  int count = 0;
  int sampling_count = 0, fail_count = 0;
  char *query = NULL, *sample = NULL;
  char err_msg[LINE_MAX];
  bool is_committed = false;
  time_t start_time, commit_time;
  int elapsed_time;
  bool delay_commit = false;

  start_time = time (NULL);
  while (!feof (fp))
    {
      /* wait until more logs to be written */
      while (ca_Info.curr_file_id == ca_Info.src_file_id
	     && ca_Info.last_applied_sql_id == ca_Info.src_last_inserted_sql_id)
	{
	  if (is_committed == false && delay_commit == false)
	    {
	      if (commit_sql_logs (conn, &cci_error) != ER_CA_NO_ERROR)
		{
		  goto process_sql_error;
		}
	      is_committed = true;
	      print_progress (0, 0);
	      fprintf (stdout, "Waiting for more logs to be written...\n");
	    }
	  sleep (3);

	  error = read_src_catalog ();
	  if (error != ER_CA_NO_ERROR)
	    {
	      snprintf (err_msg, LINE_MAX, "Failed to read applylogdb catalog: %s", applylogdb_catalog_path);
	      er_log (error, NULL, err_msg);

	      return error;
	    }
	}
      is_committed = false;

      error = read_sql_meta_info (fp);
      if (error < 0)
	{
	  if (error == EOF)
	    {
	      break;
	    }
	  else
	    {
	      snprintf (err_msg, LINE_MAX,
			"Failed to read SQL meta info (last retrieved SQL ID: " SQL_ID_FORMAT " in file " FILE_ID_FORMAT
			")", ca_Info.last_applied_sql_id, ca_Info.curr_file_id);
	      er_log (error, NULL, err_msg);

	      return ER_CA_FAILED;
	    }
	}

      if (ca_Info.meta_sql_id <= ca_Info.last_applied_sql_id)
	{
	  skip_sample_query (fp, ca_Info.meta_sample_length);
	  skip_sql_query (fp, ca_Info.meta_sql_length);

	  continue;
	}

      if (ca_Info.meta_sql_id - ca_Info.last_applied_sql_id > 1)
	{
	  snprintf (err_msg, LINE_MAX,
		    "Read unexpected SQL with ID " SQL_ID_FORMAT " in file " FILE_ID_FORMAT " (expected ID: "
		    SQL_ID_FORMAT ")", ca_Info.meta_sql_id, ca_Info.curr_file_id, ca_Info.last_applied_sql_id + 1);
	  er_log (ER_CA_DISCREPANT_INFO, NULL, err_msg);

	  return ER_CA_DISCREPANT_INFO;
	}

      /* start processing SQL log */
      if (ca_Info.sampling_rate > 0 && ca_Info.meta_sample_length > 0)
	{
	  if (ca_Info.sampling_rate == sampling_count)
	    {
	      process_sample_query (fp, &sample, ca_Info.meta_sample_length);
	      sampling_count = 0;
	    }
	  else
	    {
	      skip_sample_query (fp, ca_Info.meta_sample_length);
	    }
	  sampling_count++;
	}
      else
	{
	  skip_sample_query (fp, ca_Info.meta_sample_length);
	}

      error = read_sql_query (fp, &query, ca_Info.meta_sql_length);
      if (error != ER_CA_NO_ERROR)
	{
	  snprintf (err_msg, LINE_MAX, "Failed to read SQL id: " SQL_ID_FORMAT " in file " FILE_ID_FORMAT,
		    ca_Info.meta_sql_id, ca_Info.curr_file_id);
	  er_log (error, NULL, err_msg);

	  return error;
	}

      if (ca_Info.ignore_serial == false || IS_ALTER_SERIAL (query) == false)
	{
	  error = execute_sql_query (conn, query, &cci_error);
	  if (error < 0)
	    {
	      fail_count++;
	    }

	  if (CA_STOP_ON_ERROR (error, cci_error.err_code))
	    {
	      snprintf (err_msg, LINE_MAX, "%s will be terminated.", PROG_NAME);
	      er_log ((error == CCI_ER_DBMS) ? cci_error.err_code : error, NULL, err_msg);

	      goto process_sql_error;
	    }
	  count++;
	}

      /* to execute [set param -> ddl -> restore param] within a transaction */
      if (DOES_TRAN_START (query) == true)
	{
	  assert (delay_commit == false);
	  delay_commit = true;
	}
      else if (DOES_TRAN_END (query) == true)
	{
	  assert (delay_commit == true);
	  delay_commit = false;
	}

      ca_Info.last_applied_sql_id = ca_Info.meta_sql_id;

      if (count >= ca_Info.commit_interval && delay_commit == false)
	{
	  if (commit_sql_logs (conn, &cci_error) != ER_CA_NO_ERROR)
	    {
	      goto process_sql_error;
	    }

	  commit_time = time (NULL);
	  elapsed_time = (int) (commit_time - start_time);

	  print_progress (count, elapsed_time);

	  start_time = commit_time;
	  count = 0;
	}
      free_and_init (query);
    }

  if (commit_sql_logs (conn, &cci_error) != ER_CA_NO_ERROR)
    {
      goto process_sql_error;
    }
  else
    {
      print_result (fail_count);
    }

  return ER_CA_NO_ERROR;

process_sql_error:
  free_and_init (query);
  return ER_CA_FAILED;
}

/*
 * commit_sql_logs() - commit applied queries
 *   return: error code
 *
 *   conn(in):
 *   error(out):
 */
static int
commit_sql_logs (int conn, T_CCI_ERROR * error)
{
  int res;
  res = cci_end_tran (conn, CCI_TRAN_COMMIT, error);

  if (res != ER_CA_NO_ERROR)
    {
      er_log_cci (res, NULL, error);
      return ER_CA_FAILED;
    }
  update_ca_catalog ();
  return ER_CA_NO_ERROR;
}

/*
 * apply_sql_logs() - apply log files to target DB
 *   return: error code
 *
 *   conn(in):
 */
static int
apply_sql_logs (int conn)
{
  FILE *sql_log_fp;
  char sql_log_path[PATH_MAX];
  char err_msg[LINE_MAX];
  int error = ER_CA_NO_ERROR;

  /* read catalog info to find out the right log file to read */
  if (read_ca_catalog () != ER_CA_NO_ERROR)
    {
      fprintf (stderr, "Read catalog file: FAILED (%s)\n", ca_catalog_path);
      return ER_CA_FAILED;
    }

  /* read applylogdb's catalog info */
  if (read_src_catalog () != ER_CA_NO_ERROR)
    {
      fprintf (stderr, "Read catalog file: FAILED (%s)\n", applylogdb_catalog_path);
      return ER_CA_FAILED;
    }

  while (ca_Info.curr_file_id <= ca_Info.src_file_id)
    {
      snprintf (sql_log_path, PATH_MAX, "%s.%d", base_log_path, ca_Info.curr_file_id);

      sql_log_fp = fopen (sql_log_path, "r");
      if (sql_log_fp == NULL)
	{
	  snprintf (err_msg, LINE_MAX, "Could not find %s", sql_log_path);
	  er_log (ER_CA_FILE_IO, NULL, err_msg);

	  return ER_CA_FILE_IO;
	}

      fprintf (stdout, "\n*** Start applying %s\n", sql_log_path);

      error = process_sql_log_file (sql_log_fp, conn);
      if (error == ER_CA_NO_ERROR)
	{
	  fclose (sql_log_fp);

	  ca_Info.curr_file_id++;
	  ca_Info.last_applied_sql_id = 0;

	  if (ca_Info.retain_log == false)
	    {
	      unlink (sql_log_path);
	      fprintf (stdout, "*** %s has been deleted.\n", sql_log_path);
	    }
	}
      else
	{
	  if (error == ER_CA_DISCREPANT_INFO)
	    {
	      snprintf (err_msg, LINE_MAX, "Discrepant catalog info in either %s or %s", applylogdb_catalog_path,
			ca_catalog_path);
	      er_log (error, NULL, err_msg);
	    }
	  fclose (sql_log_fp);

	  return error;
	}

      if (ca_Info.curr_file_id > ca_Info.src_file_id)
	{
	  if (read_src_catalog () != ER_CA_NO_ERROR)
	    {
	      fprintf (stderr, "Read catalog file: FAILED (%s)\n", applylogdb_catalog_path);
	      return ER_CA_FAILED;
	    }
	}
    }
  /* cci_applier should always wait for more logs to be accumulated */
  assert (false);

  return ER_CA_FAILED;
}

/*
 * open_sample_file() - open sample file to leave sample select queries
 *
 *   return: error code
 */
static FILE *
open_sample_file (void)
{
  FILE *fp;
  char cur_sample_file_path[PATH_MAX];
  char err_msg[LINE_MAX];

  do
    {
      snprintf (cur_sample_file_path, PATH_MAX, "%s.%03d", sample_file_path_base, ca_Info.sample_file_count);

      fp = fopen (cur_sample_file_path, "a");
      if (fp == NULL)
	{
	  snprintf (err_msg, LINE_MAX, "Failed to open or create %s. Log sampling is disabled", cur_sample_file_path);
	  er_log (ER_CA_FAILED, NULL, err_msg);

	  ca_Info.sampling_rate = 0;
	}
      else if (ftell (fp) > CA_MAX_SAMPLE_FILE_SIZE)
	{
	  fclose (fp);
	  fp = NULL;

	  ca_Info.sample_file_count++;
	}
    }
  while (fp == NULL && ca_Info.sampling_rate != 0);

  return fp;
}

/*
 * er_log_cci() - log cci error
 *   return:
 *
 *   res(in):
 *   query(in): query that makes an error on applying it
 *   error(out):
 */
static void
er_log_cci (int res, const char *query, T_CCI_ERROR * error)
{
  char err_msg[255];

  if (res == CCI_ER_DBMS)
    {
      er_log (error->err_code, query, error->err_msg);
      return;
    }
  else
    {
      if (cci_get_err_msg (res, err_msg, sizeof (err_msg)) < 0)
	{
	  er_log (res, query, "UNKNOWN");
	}
      else
	{
	  er_log (res, query, err_msg);
	}
      return;
    }
}

/*
 * er_log() - log error
 *   return:
 *
 *   error_code(in):
 *   query(in): query that makes an error on applying it
 *   format(in):
 */
static void
er_log (int error_code, const char *query, const char *err_msg)
{
  time_t cur_time;
  char time_buf[20];
  FILE *fp;
  char *tmp_err_msg;
  char *p, *token;

  cur_time = time (NULL);
  strftime (time_buf, sizeof (time_buf), "%Y-%m-%d %H:%M:%S", localtime (&cur_time));

  fp = fopen (err_file_path, "a");

  tmp_err_msg = strdup (err_msg);

  fprintf (fp, "# Time: %s - ERROR CODE = %d\n", time_buf, error_code);

  if (tmp_err_msg != NULL)
    {
      for (p = tmp_err_msg;; p = NULL)
	{
	  token = strtok (p, "\n");
	  if (token == NULL)
	    {
	      break;
	    }

	  fprintf (fp, "# %s\n", token);
	}

      free_and_init (tmp_err_msg);
    }

  if (query != NULL)
    {
      fprintf (fp, "%s\n", query);
    }
  fputc ('\n', fp);

  fflush (fp);
  fclose (fp);

  return;
}

/*
 * print_progress() - print progress
 *   return:
 *
 *   count(in):
 *   elapsed_time(in):
 */
static void
print_progress (unsigned int count, int elapsed_time)
{
  unsigned int curr_sql_id;

  curr_sql_id = count ? ca_Info.meta_sql_id : ca_Info.last_applied_sql_id;

  fprintf (stdout, "SQL [" SQL_ID_FORMAT "] in file [" FILE_ID_FORMAT "] applied\n", curr_sql_id, ca_Info.curr_file_id);
  if (elapsed_time)
    {
      fprintf (stdout, "QPS for the last %u queries: %.2f\n\n", count, (float) count / elapsed_time);
    }
  return;
}

/*
 * print_result() - print result
 *   return:
 *
 *   fail_count(in):
 */
static void
print_result (int fail_count)
{
  fprintf (stdout, "*** File [" FILE_ID_FORMAT "] completed.\n", ca_Info.curr_file_id);
  fprintf (stdout, "*** Total failure count for file [" FILE_ID_FORMAT "]: %d\n", ca_Info.curr_file_id, fail_count);
  return;
}

/*
 * init_ca_Info() - initialize ca_Info
 *
 *   return:
 */
static void
init_ca_Info (void)
{
  memset (&ca_Info, 0, sizeof (CA_INFO));

  ca_Info.commit_interval = CA_DEFAULT_COMMIT_INTERVAL;
  ca_Info.ignore_serial = false;
  ca_Info.retain_log = false;

  return;
}

/*
 * init_con_info() - initialize connection info
 *
 *   return:
 *
 *   con_info(out):
 */
static void
init_con_info (CA_CON_INFO * con_info)
{
  memset (con_info, 0, sizeof (CA_CON_INFO));
  strncpy (con_info->db_user, "dba", sizeof (con_info->db_user) - 1);

  return;
}

/*
 * validate_args() - validate options given by user
 *
 *   return:
 *
 *   con_info(in):
 *   repl_log_path(in): path to replication log created by copylogdb
 */
static void
validate_args (CA_CON_INFO * con_info, char *repl_log_path)
{
  char resolved_path[PATH_MAX];

  if (con_info->db_name == NULL || con_info->hostname == NULL || con_info->password == NULL || con_info->port == -1
      || repl_log_path == NULL || repl_log_path[0] == '\0')
    {
      fprintf (stderr, "Some of arguments are missing.\n");
      print_usage_and_exit ();
    }

  if (ca_Info.commit_interval < 1 || ca_Info.sampling_rate < 0)
    {
      fprintf (stderr, "Some of arguments have invalid value.\n");
      print_usage_and_exit ();
    }

  if (realpath (repl_log_path, resolved_path) == NULL)
    {
      fprintf (stderr, "Failed to resolve references to %s\n", repl_log_path);
      exit (-1);
    }
  else
    {
      strncpy (repl_log_path, resolved_path, PATH_MAX - 1);
    }

  return;
}

/*
 * set_file_path() - set file path
 *
 *   return:
 *
 *   con_info(in):
 *   repl_log_path(in): path to replication log created by copylogdb
 */
static void
set_file_path (CA_CON_INFO * con_info, char *repl_log_path)
{
  assert (repl_log_path != NULL && repl_log_path[0] != '\0');

  snprintf (err_file_path, PATH_MAX, "%s@%s_%s.err", con_info->db_name, con_info->hostname, PROG_NAME);
  snprintf (ca_catalog_path, PATH_MAX, "%s@%s_%s.sql.info", con_info->db_name, con_info->hostname, PROG_NAME);
  snprintf (base_log_path, PATH_MAX, "%s/sql_log/%s.sql.log", repl_log_path, basename (repl_log_path));
  snprintf (applylogdb_catalog_path, PATH_MAX, "%s/%s_applylogdb.sql.info", repl_log_path, con_info->db_name);

  if (ca_Info.sampling_rate > 0)
    {
      snprintf (sample_file_path_base, PATH_MAX, "%s@%s_%s.sql.sample", con_info->db_name, con_info->hostname,
		PROG_NAME);
    }

  return;
}

/*
 * print_usage_and_exit() -
 *
 *   return:
 */
static void
print_usage_and_exit (void)
{
  fprintf (stdout, "usage: %s [OPTIONS]\n", PROG_NAME);
  fprintf (stdout, "  options: \n");
  fprintf (stdout, "\t -h [host name]\n");
  fprintf (stdout, "\t -P [port]\n");
  fprintf (stdout, "\t -d [database name]\n");
  fprintf (stdout, "\t -p [DBA password]\n");
  fprintf (stdout, "\t -L [replication log path]\n");
  fprintf (stdout, "\t -s [sampling rate] (default: 0)\n");
  fprintf (stdout, "\t -c [commit interval] (default: 5000)\n");
  fprintf (stdout, "\t %-20s ignore \"ALTER SERIAL\" logs\n", "--ignore-serial");
  fprintf (stdout, "\t %-20s do not delete a SQL log after applying it\n", "--retain-log");

  exit (-1);
}

/*
 * main() - cci_applier main
 */
int
main (int argc, char *argv[])
{
  int conn;
  int opt, opt_index = 0;
  int error = ER_CA_NO_ERROR;
  int res;
  bool ignore_serial = false;
  bool retain_log = false;
  char tmp_repl_log_path[PATH_MAX];
  char repl_log_path[PATH_MAX];

  CA_CON_INFO con_info;

  init_ca_Info ();
  init_con_info (&con_info);

  while ((opt = getopt_long (argc, argv, "h:P:d:p:L:c:s:ir", cci_applier_options, &opt_index)) != -1)
    {
      switch (opt)
	{
	case 'h':
	  con_info.hostname = optarg;
	  break;
	case 'P':
	  con_info.port = atoi (optarg);
	  break;
	case 'd':
	  con_info.db_name = optarg;
	  break;
	case 'p':
	  con_info.password = strdup (optarg);
#if defined (LINUX)
	  memset (optarg, '*', strlen (optarg));
#endif
	  break;
	case 'c':
	  ca_Info.commit_interval = atoi (optarg);
	  break;
	case 's':
	  ca_Info.sampling_rate = atoi (optarg);
	  break;
	case 'L':
	  strncpy (repl_log_path, optarg, PATH_MAX - 1);
	  break;
	case 'i':
	  ca_Info.ignore_serial = true;
	  break;
	case 'r':
	  ca_Info.retain_log = true;
	  break;
	default:
	  print_usage_and_exit ();
	  break;
	}
    }

  validate_args (&con_info, repl_log_path);
  set_file_path (&con_info, repl_log_path);

  /* open up a connection with target db */
  if ((conn =
       cci_connect (con_info.hostname, con_info.port, con_info.db_name, con_info.db_user, con_info.password)) < 0)
    {
      fprintf (stderr, "Failed to open up a connection with %s:%d for %s.(%d)\n", con_info.hostname, con_info.port,
	       con_info.db_name, conn);
      res = ER_CA_FAILED;
      goto end;
    }

  cci_set_autocommit (conn, CCI_AUTOCOMMIT_FALSE);

  fprintf (stdout, "Apply SQL log: STARTED\n");

  if ((error = apply_sql_logs (conn)) != ER_CA_NO_ERROR)
    {
      fprintf (stderr, "Apply SQL log: FAILED\n");
      res = ER_CA_FAILED;
      goto end;
    }

  fprintf (stdout, "Apply SQL log: FINISHED\n");

  res = ER_CA_NO_ERROR;

end:
  if (con_info.password != NULL)
    {
      free (con_info.password);
    }

  return res;
}
