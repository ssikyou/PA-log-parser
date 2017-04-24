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

int g_debug_level = L_DEBUG;

static struct option cmd_options[] = {
	{ "help",	0, 0, 'h' },
	{ "line",	1, 0, 'l' },
	{ 0, 0, 0, 0 }
};

static const char *cmd_help =
	"Usage:\n"
	"\tPAlogparser [--line=n] </path/to/csvfile>\n"
	"arguments:\n"
	"\tcsvfile: csv file path\n"
	"options:\n"
	"\t--line: only parse lines\n"
	"Example:\n"
	"\tPAlogparser --line=100 test.csv\n";

int main(int argc, char **argv)
{
	int opt;
	int ret = 0;

	unsigned int parse_lines = UINT_MAX;

    while ((opt=getopt_long(argc, argv, "hl:f:", cmd_options, NULL)) != -1) {
		switch (opt) {
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

	mmc_parser *parser = mmc_parser_init();

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
    CsvParser_destroy(csvparser);
	

	return ret;
}

