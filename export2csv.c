/************************************************************************
  export2csv

 Connects to database through specified DSN.
 Extracts specified table, view, or query into a text file.
 Can specify delimiters and (if necessary) qoute the individual fields.

 Multiple resultsets can be handled. The first one is stored in the
 specified file, the second and all other are stored in files with names
 like "name-XX.ext" where 'name' and 'ext' are taken from the original
 output file.

  Author: George Brink <siberianowl@gmail.com>
  History: 07/14/2016 - initial release
  04/17/2017 - ready for deployment to production
  08/24/2017 - added support for multiple resultsets
************************************************************************/

#ifdef _WIN32
	#include <windows.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sqlext.h>
#define __USE_BSD
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sql.h>

// for convenience
#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE !FALSE
#endif

FILE *logfile;           // pointers to the log file
FILE *datafile;          // file with data
SQLHENV henv = 0;        // handle for ODBC environment
SQLHDBC hdbc = 0;        // handle for ODBC database connection
SQLHSTMT hstmt = 0;      // handle for ODBC SQL statement
SQLRETURN retcode;       // return codes from ODBC functions
time_t process_start_time; // when the whole process started?
char buf[4096];          // various usage buffer

/* hard-coded for testing */
char *dsn = NULL;
char *userid = NULL;
char *password = NULL;
int column_names = FALSE;
int quote_strings = FALSE;
int trim_strings = TRUE;
int verbose = FALSE;
char field_delimiter[16];
char record_delimiter[16];
char *source_query =  NULL;
char *data_file_name = NULL;
char *query_file_name = NULL;
SQLCHAR *query_string = NULL;

typedef struct {
	SQLCHAR name[128];
	SQLSMALLINT name_length;
	SQLSMALLINT data_type;
	SQLULEN column_size;
	SQLSMALLINT decimal_digits;
	SQLSMALLINT nullable;
} Field;
Field *fields;



/*** function to remind list of acceptable parameters */
void usage() {
	printf("usage: export2csv [options] \"table|view|query|file.sql\" \"output_file\"\n"
	       "-D text   DSN used to connect to database server\n"
	       "-U text   ID to connect\n"
	       "-P text   password to connect\n"
	       "-l text   define name for log file (stdout)\n"
	       "-f text   delimiter between fields ('\\t')\n"
	       "-r text   delimiter between records ('\\n')\n"
	       "-n bool   include column names? (no)\n"
	       "-q bool   surround text values with double quote? (no)\n"
	       "-t bool   trim char fields padded with spaces? (yes)\n"
	       "-v bool   verbose? (no)\n"
	       "\n");
	exit(1);
}



/*** make a timed record in the log */
void write_to_log(char *message) {
	time_t tt = time(0);
	struct tm *tim = localtime(&tt);
	fprintf(logfile, "%02d:%02d:%02d %s\n", tim->tm_hour, tim->tm_min, tim->tm_sec, message);
	fflush(logfile);
}

/*** close process with the record in the log */
void finish_process(char *reason, int code) {
	time_t process_end_time = time(0);
	sprintf(buf, "process ended (in %d seconds): %s", (int)(process_end_time - process_start_time), reason);
	write_to_log(buf);
	if(datafile) fclose(datafile);
	exit(code);
}

/*** Helper function for gracefull shutdown of ODBC interaction */
void odbc_cleanup() {
	if(hstmt) {
		SQLCancel(hstmt);
		SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
	}
	if(hdbc) {
		SQLDisconnect(hdbc);
		SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
	}
	if(henv) {
		SQLFreeHandle(SQL_HANDLE_ENV, henv);
	}
}

/*** Helper function to report ODBC errors */
void print_odbc_error(SQLSMALLINT handle_type, int critical) {
	SQLHANDLE hndl;          // Just to avoid passing the actual handle into this function.
	SQLCHAR sql_state[6];    // SQLSTATE is always 5 characters. google "msdn ODBC Error Codes" for code explanations
	SQLINTEGER native_error; // error code as reported by the driver. Driver specific, therefore ignored here.
	SQLCHAR message_text[2048];
	SQLSMALLINT message_length;

	// choose the appropriate handle
	switch(handle_type) {
	case SQL_HANDLE_ENV: hndl=henv; break;
	case SQL_HANDLE_DBC: hndl=hdbc; break;
	case SQL_HANDLE_STMT: hndl=hstmt; break;
	default: finish_process("Incorrect usage of print_odbc_error().", 2);
	}

	// Since any call to ODBC and/or database can return multiple messages, we have to iterate through them
	SQLSMALLINT record_number=1;
	while(TRUE) {
		retcode = SQLGetDiagRec(handle_type, hndl, record_number,
				sql_state, &native_error, message_text, sizeof(message_text), &message_length);
		if(retcode != SQL_SUCCESS) break;
		if( message_text[--message_length]=='\n') message_text[message_length]=0;
		sprintf(buf, "[SQLSTATE=%s] %s", sql_state, message_text);
		write_to_log(buf);
		record_number++;
	}

	if(critical) {
		odbc_cleanup();
		finish_process("Unrecoverable ODBC error", 2);
	}
}


/*** main function */
int main(int argc, char **argv) {
	char log_file_name[128];
	process_start_time = time(0);
	strcpy(log_file_name, "-");
	datafile = NULL;
	strcpy(field_delimiter, "\x09");
	strcpy(record_delimiter, "\n");

	/// decypher parameters
	for(int argn=1; argn<argc; argn++) {
		//printf("%d: %s\n", argn, argv[argn]);
		if(argv[argn][0] == '-' && argv[argn][1]) {
			char *parameter_value = NULL;
			int bool_value;
			char key = argv[argn][1];

			/// find parameter for the key
			if (argv[argn][2]) {
				if(argv[argn][2] == '=') {
					parameter_value = &(argv[argn][3]);
				} else {
					parameter_value = &(argv[argn][2]);
				}
			} else if((argn+1)<argc) {
				if(argv[argn+1][0] != '-') parameter_value = argv[++argn];
			}

			if(!parameter_value && strchr("DUPfrl", key)) {
				printf("Key -%c requires parameter\n\n", key);
				usage();
			}

			/// decrypt key
			switch(key) {
			case '?': case 'h': usage();
			case 'D': dsn = parameter_value; break;
			case 'U': userid = parameter_value; break;
			case 'P': password = parameter_value; break;
			case 'f': sscanf(parameter_value, "%7s", field_delimiter); break;
			case 'r': sscanf(parameter_value, "%7s", record_delimiter); break;
			case 'l': strncpy(log_file_name, parameter_value, sizeof(log_file_name)); break;
			case 'n': case 'q': case 't': case 'v':
				bool_value = TRUE;
				if(parameter_value) {
					if(strncasecmp(parameter_value, "OFF", 4)==0 || strncasecmp(parameter_value, "FALSE", 6)==0 ||
				   	   strncasecmp(parameter_value, "NO", 4)==0 || strncasecmp(parameter_value, "1", 2)==0) {
						bool_value = FALSE;
					}
				}
				if(key == 'n') {
					column_names = bool_value;
				} else if(key == 'q') {
					quote_strings = bool_value;
				} else if(key == 't') {
					trim_strings = bool_value;
				} else {
					verbose = bool_value;
				}
				break;
			default:
				printf("Key -%c not understood\n\n", key);
				usage();
			}
		} else {
			if(!source_query) {
				source_query = argv[argn];
			} else if(!data_file_name) {
				data_file_name = argv[argn];
			} else {
				printf("Argument %s not understood\n\n", argv[argn]);
				usage();
			}
		}
	}

	/// check if all necessary parameters supplied
	if( !dsn || !userid || !password || !data_file_name || !source_query) {
		printf("One or more of necessary parameters omitted\n");
		usage();
	}

	/// open log file
	if (strncmp(log_file_name, "-", 2)==0) {
		logfile = stdout;
	} else if ((logfile = fopen(log_file_name, "a+")) == NULL) {
		sprintf(buf, "Error opening the log file %s", log_file_name);
		perror(buf);
		exit(1);
	}


	/// process header in the log
	sprintf(buf, "%s started", argv[0]);
	write_to_log(buf);
	sprintf(buf, "Connecting to DSN {%s}", dsn);
	write_to_log(buf);

	if (verbose) write_to_log("Attempting to initialise ODBC");
	/// Setup ODBC environment
	retcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
	if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) {
		finish_process("Failed to initialize ODBC interface", 1);
	}
	retcode = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER*)SQL_OV_ODBC3, 0);
	if (!SQL_SUCCEEDED(retcode)) print_odbc_error(SQL_HANDLE_ENV, TRUE);

	/// Connect to database
	retcode = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);
	if (!SQL_SUCCEEDED(retcode)) print_odbc_error(SQL_HANDLE_DBC, TRUE);

	/// Set attributes
	retcode = SQLSetConnectAttr(hdbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
	if (!SQL_SUCCEEDED(retcode)) print_odbc_error(SQL_HANDLE_DBC, TRUE);

	/*if (verbose) {
		sprintf(buf, "Connection string is set to: DSN=%s;UID=%s;PWD=%s;", dsn, userid, password);
		write_to_log(buf);
	}*/
	sprintf(buf,"DSN=%s;UID=%s;PWD=%s;", dsn, userid, password);
	retcode = SQLDriverConnect(hdbc, 0, (SQLCHAR*)buf, SQL_NTS, 0, 0, 0, SQL_DRIVER_NOPROMPT);
	if (!SQL_SUCCEEDED(retcode)) print_odbc_error(SQL_HANDLE_DBC, TRUE);

	retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
	if (!SQL_SUCCEEDED(retcode)) print_odbc_error(SQL_HANDLE_DBC, TRUE);

	retcode = SQLSetStmtAttr(hstmt, SQL_ATTR_CURSOR_TYPE, (SQLPOINTER)SQL_CURSOR_FORWARD_ONLY , 0);
	if (!SQL_SUCCEEDED(retcode)) print_odbc_error(SQL_HANDLE_STMT, TRUE);

	if (verbose) write_to_log("Connection sucessfull");


	/// try to understand what the requested query is...
	size_t source_query_length = strlen(source_query);
	if(source_query_length>4 && strncasecmp(source_query+source_query_length-4, ".sql", 5)==0) {
		/// check, is the requested query a file, and if yes, read it
		if (verbose) write_to_log("Query ends with .sql, assume it is a file.");
		FILE *qfp = fopen(source_query, "r");
		if(!qfp) {
			sprintf(buf, "Error opening the query file %s", source_query);
			perror(buf);
			exit(1);
		}
		fseek(qfp, 0, SEEK_END);
		size_t query_length=ftell(qfp);
		query_string = malloc(sizeof(SQLCHAR) * query_length + 1);
		fseek(qfp, 0, SEEK_SET);
		fread(query_string, sizeof(SQLCHAR), query_length, qfp);
		query_string[query_length] = 0;
		fclose(qfp);
		sprintf(buf, "Executing script from %s", source_query);
		write_to_log(buf);


	} else if (!strchr(source_query, ' ')) {
		/// If the query string does not have space in it, assume it is not
		/// a fully specified query, but a name of a table, view or stored procedure.
		/// Check what is it in reality and construct a proper query.
		if (verbose) write_to_log("One-word query, is this an object?");
		char database[32], owner[32], object_name[256];
		char *s, *t;
		int ts;
		for(s=source_query, ts=0, t=database; *s; s++) {
			if(*s == '.') {
				*t=0;
				ts++;
				if(ts==1) {
					t = owner;
				} else {
					t = object_name;
				}
			} else {
				*t = *s;
				t++;
			}
		}
		*t=0;
		unsigned char type[8] = "";
		sprintf(buf, "select o.type from %s.dbo.sysobjects o "
		             "join %s.dbo.sysusers u on o.uid=u.uid "
		             "where u.name='%s' and o.name='%s'",
		              database, database, owner, object_name);
		if(verbose) write_to_log(buf);

		if(SQLExecDirect(hstmt, (SQLCHAR*)buf, SQL_NTS) != SQL_SUCCESS) print_odbc_error(SQL_HANDLE_STMT, TRUE);
		SQLSMALLINT column_count = 0;
		while(column_count==0) {
			if(SQLNumResultCols(hstmt, &column_count) != SQL_SUCCESS) print_odbc_error(SQL_HANDLE_STMT, TRUE);
			if(column_count==0) {
				if(SQLMoreResults(hstmt) != SQL_SUCCESS) print_odbc_error(SQL_HANDLE_STMT, TRUE);
			}
		}
		while ((retcode=SQLFetch(hstmt)) == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
			SQLGetData(hstmt, 1, SQL_C_CHAR, type, sizeof(type), NULL);
		}
		SQLCloseCursor(hstmt);
		if(type[0]=='P') {
			sprintf(buf, "Executing procedure %s", source_query);
			write_to_log(buf);
			sprintf(buf, "execute %s.%s.%s", database, owner, object_name);
		} else if(type[0]=='U' || type[0]=='V') {
			sprintf(buf, "Selecting from %s", source_query);
			write_to_log(buf);
			sprintf(buf, "select * from %s.%s.%s", database, owner, object_name);
		} else {
			sprintf(buf, "Object %s was not found, aborting", source_query);
			write_to_log(buf);
			odbc_cleanup();
			finish_process("Unrecoverable error", 3);
		}
		query_string = malloc(strlen(buf)+1);
		strcpy((char*)query_string, buf);
	} else {
		sprintf(buf, "Executing direct statement %s", source_query);
		write_to_log(buf);
		query_string = malloc(source_query_length+1);
		strcpy((char*)query_string, source_query);
	}


	SQLLEN rowcount=0;
	/// And now execute the actual query
	retcode=SQLExecDirect(hstmt, query_string, SQL_NTS);
	//printf("SQLExecDirect retcode = %d\n", retcode);
	if(retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) print_odbc_error(SQL_HANDLE_STMT, TRUE);
	if(verbose && retcode == SQL_SUCCESS_WITH_INFO) print_odbc_error(SQL_HANDLE_STMT, FALSE);
	int has_more=TRUE;
	unsigned int resultset_count=0;

	do { // while(has_more)
		SQLSMALLINT column_count = 0;
		while((column_count==0) && (has_more)) {
			retcode=SQLNumResultCols(hstmt, &column_count);
			//printf("SQLNumResultCols retcode = %d   column_count=%d\n", retcode, column_count);
			if(retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) print_odbc_error(SQL_HANDLE_STMT, TRUE);
			if(verbose && retcode == SQL_SUCCESS_WITH_INFO) print_odbc_error(SQL_HANDLE_STMT, FALSE);

			/// MS SQL can return some information before the actual resultset
			if(column_count==0) {
				retcode = SQLMoreResults(hstmt);
				if(retcode == SQL_NO_DATA) has_more=FALSE;
				else if(!SQL_SUCCEEDED(retcode)) print_odbc_error(SQL_HANDLE_STMT, TRUE);
				if(verbose) {
					print_odbc_error(SQL_HANDLE_STMT, FALSE);
					SQLRowCount(hstmt, &rowcount);
					if(rowcount != -1) { // select statement returns -1
						sprintf(buf, "%ld rows affected", rowcount);
						write_to_log(buf);
					}
				}
			} else if(verbose) {
				sprintf(buf, "Query returned %d columns", column_count);
				write_to_log(buf);
			}
		}
		resultset_count ++;
		if(has_more == FALSE) break;

		/// get names of fields
		fields = malloc(sizeof(Field) * (column_count+1));
		char **row = malloc(sizeof(char*) * (column_count+1));
		SQLLEN *row_lengths = malloc(sizeof(SQLLEN) * (column_count+1));
		for(SQLSMALLINT cnt = 1; cnt <= column_count; cnt++) {
			SQLDescribeCol(hstmt, cnt, fields[cnt].name, sizeof(fields[cnt].name), &fields[cnt].name_length,
			               &fields[cnt].data_type, &fields[cnt].column_size, &fields[cnt].decimal_digits, &fields[cnt].nullable);
			row[cnt] = malloc(fields[cnt].column_size << 1);
			retcode = SQLBindCol(hstmt, cnt, SQL_C_CHAR, row[cnt], fields[cnt].column_size << 1, &row_lengths[cnt]);
			if(retcode != SQL_SUCCESS) {
				sprintf(buf, "Binding of field %s failed", fields[cnt].name);
				write_to_log(buf);
				print_odbc_error(SQL_HANDLE_STMT, FALSE);
			}
		}

		/// Prepare output file
		char numbered_file_name[1024];
		if(resultset_count>1) {
			char *pos_of_dot = strrchr(data_file_name, '.');
			if(!pos_of_dot) pos_of_dot = data_file_name + strlen(data_file_name);
			int index_of_dot = pos_of_dot - data_file_name;
			strncpy(numbered_file_name, data_file_name, index_of_dot);
			sprintf(numbered_file_name+index_of_dot, "-%02d", resultset_count);
			strncpy(numbered_file_name+index_of_dot+3, data_file_name+index_of_dot, sizeof(numbered_file_name)-index_of_dot-3);
		} else {
			strncpy(numbered_file_name, data_file_name, sizeof(numbered_file_name));
		}
		sprintf(buf, "Output is set to %s", numbered_file_name);
		write_to_log(buf);

		datafile = fopen(numbered_file_name, "w");
		if(!datafile)  {
			sprintf(buf, "Error opening the output file %s", numbered_file_name);
			perror(buf);
			exit(1);
		}

		/// if needed, write column names first
		if(column_names) {
			if(verbose) write_to_log("Writing column names");
			for(int cnt=1; cnt<=column_count; cnt++) {
				if(cnt>1) fputs(field_delimiter, datafile);
				if(quote_strings) fputc('"', datafile);
				fputs((const char*)fields[cnt].name, datafile);
				if(quote_strings) fputc('"', datafile);
			}
			fputs(record_delimiter, datafile);
		}

		if(verbose) write_to_log("Starting to write records...");
		rowcount = 0;
		/// and now, iterate over resultset
		while( ((retcode=SQLFetch(hstmt)) == SQL_SUCCESS) || (retcode == SQL_SUCCESS_WITH_INFO) ) {
			for(SQLSMALLINT cnt = 1; cnt <= column_count; cnt++) {
				if(cnt>1) fputs(field_delimiter, datafile);
				if(row_lengths[cnt]>0) {
					/// check if a field has delimiter inside, if yes - quote the field
					int has_delimiter = FALSE;
					if(strstr(row[cnt], field_delimiter)) {
						has_delimiter = TRUE;
					}
					if(quote_strings || has_delimiter) fputc('"', datafile);


    				if(trim_strings) {
						char *s = row[cnt];
						while(*s) s++;
						while(*(--s)==' ') *s=0;
					}

					/// special case numbers starting with dot (.1234) should start with zero (0.1234)
					if(row[cnt][0] == '.') {
						int all_digits = TRUE;
						char *s= &(row[cnt][1]);
						while(*s) {
							if((*s < '0') || (*s > '9')) {
								all_digits = FALSE;
								break;
							}
							s++;
						}
						if(all_digits) {
							fputc('0', datafile);
						}
					}
					fputs(row[cnt], datafile);
					if(quote_strings || has_delimiter) fputc('"', datafile);
				}
			}
			fputs(record_delimiter, datafile);
			rowcount++;
			if((rowcount % 1000) == 0) {
				fflush(datafile);
				if(verbose) {
					sprintf(buf, "%lu rows done", rowcount);
					write_to_log(buf);
				}
			}
		}
		if(retcode!=SQL_NO_DATA) {
			print_odbc_error(SQL_HANDLE_STMT, FALSE);
		}

		sprintf(buf, "%lu rows returned", rowcount);
		write_to_log(buf);


		/// free resources for completed resultset
		for(SQLSMALLINT cnt = 1; cnt <= column_count; cnt++) {
			free(row[cnt]);
		}
		free(row);
		free(fields);
		free(row_lengths);
		fclose(datafile);
		datafile=NULL;

		/// switch to next resultset after successful one
		retcode = SQLMoreResults(hstmt);
		if(retcode == SQL_NO_DATA) has_more=FALSE;
	} while(has_more);

	SQLCloseCursor(hstmt);

	odbc_cleanup();
	finish_process("Ok", 0);
}
