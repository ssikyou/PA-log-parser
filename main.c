#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/time.h>
#include <getopt.h>
#include <string.h>

#include "common.h"
#include "csvparser.h"
#include "emmcparser.h"

#include "xlsxwriter.h"
#include "glib.h"

//int g_debug_level = L_DEBUG;
int g_debug_level = L_INFO;

int exec_shell(char *str)
{
	int ret = 0;
    int status; 
  
    status = system(str);  
  
    if (-1 == status) {  
    	ret = -1;
        error("system error!");  
    } else {  
        dbg(L_DEBUG, "exit status value = [0x%x]\n", status);  
  
        if (WIFEXITED(status)) {  
            if (0 == WEXITSTATUS(status)) {  
            	ret = 0;
                dbg(L_DEBUG, "run shell script successfully.\n");  
            } else {  
            	ret = -1;
                dbg(L_DEBUG, "run shell script fail, script exit code: %d\n", WEXITSTATUS(status));  
            }
        } else {  
        	ret = -1;
            error("exit status = [%d]\n", WEXITSTATUS(status));  
        }  
    }  

  	return ret;
}

void search_csv(char *file, int *has_data, int *has_busy)
{
	char busy_str[256] = "grep -q -i -m 1 '\\\"    BUSY START\\\"' ";
	char read_str[256] = "grep -q -i -m 1 '\\\"   Read\\\"' ";
	char write_str[256] = "grep -q -i -m 1 '\\\"   Write\\\"' ";

	strcat(busy_str, file);
	strcat(read_str, file);
	strcat(write_str, file);
	dbg(L_DEBUG, "search busy: %s\n", busy_str);
	*has_busy = (exec_shell(busy_str)==0)?1:0;

	dbg(L_DEBUG, "search read: %s\n", read_str);
	//exec_shell(read_str);

	dbg(L_DEBUG, "search write: %s\n", write_str);
	//exec_shell(write_str);

	*has_data = ((exec_shell(read_str)==0)?1:0) || ((exec_shell(write_str)==0)?1:0);

	dbg(L_DEBUG, "has busy %d, has data %d\n", *has_busy, *has_data);	
}

static struct option cmd_options[] = {
	{ "help",	0, 0, 'h' },
	{ "line",	1, 0, 'l' },
	{ "debug",	0, 0, 'd' },
	{ "quite",	0, 0, 'q' },
	{ 0, 0, 0, 0 }
};

static const char *cmd_help =
	"Usage:\n"
	"\tPAlogparser [--line=n] [--debug] [--quite] </path/to/csvfile>\n"
	"arguments:\n"
	"\tcsvfile: csv file path\n"
	"options:\n"
	"\t--debug: print debug messages\n"
	"\t--quite: only print error messages\n"
	"\t--line: only parse first n lines, if not set, all lines will be parsed\n"
	"Example:\n"
	"\tPAlogparser --line=100 test.csv\n";

int main(int argc, char **argv)
{
	int opt;
	int ret = 0;

	unsigned int parse_lines = UINT_MAX;
	int has_data = 0;
	int has_busy = 0;
	//int dbg_level = L_INFO;

    while ((opt=getopt_long(argc, argv, "hl:dq", cmd_options, NULL)) != -1) {
		switch (opt) {
			case 'd':
				//dbg_level = strtol(optarg, NULL, 0);
				g_debug_level = L_DEBUG;
				break;
			case 'q':
				//dbg_level = strtol(optarg, NULL, 0);
				g_debug_level = L_ERROR;
				break;

			case 'l':
				parse_lines = strtol(optarg, NULL, 0);
				break;			

			case 'h':
			default:
				printf("%s", cmd_help);
				return 0;
		}
	}

	argc -= optind;
	argv += optind;

	if(helper_arg(1, 1, &argc, &argv, cmd_help) != 0)
		return -1;

	dbg(L_DEBUG, "parse start!!!\n");

	search_csv(argv[0], &has_data, &has_busy);

	if (!has_data)
		error("The log file has no data info, we can not parse the read latency time!\n");
	if (!has_busy)
		error("The log file has no busy info, we can not parse the write busy time and the request's total time is not accurate!\n");

	mmc_parser *parser = mmc_parser_init(has_data, has_busy, "parser.conf");

	//register request callback
	ret = mmc_cb_init(parser);
	if (ret) {
		goto parser_destroy;
	}

	//register xls callback
	mmc_xls_init(parser, argv[0]);

#if 1
	unsigned int cur_line = 1;
	//int i =  0;
	
    //                                   file, delimiter, first_line_is_header?
    CsvParser *csvparser = CsvParser_new(argv[0], ",", 0);
    CsvRow *row;

    while ((row = CsvParser_getRow(csvparser)) && cur_line < parse_lines ) {
    	/*
    	printf("==NEW LINE: %d ==\n", cur_line);
        const char **rowFields = CsvParser_getFields(row);
        for (i = 0 ; i < CsvParser_getNumFields(row) ; i++) {
        	if (strcmp(rowFields[i], "") != 0)
            	printf("FIELD: %s\n", rowFields[i]);
        }
		printf("\n");
		*/
		mmc_row_parse(parser, CsvParser_getFields(row), CsvParser_getNumFields(row));


		cur_line++;
        CsvParser_destroy_row(row);
    }

    //g_debug_level = L_DEBUG;
    dump_req_list(&parser->stats.requests_list);

    generate_xls(parser);

    CsvParser_destroy(csvparser);

parser_destroy:
	mmc_parser_destroy(parser);
#endif

	dbg(L_DEBUG, "parse end!!!\n");
	return ret;
}

