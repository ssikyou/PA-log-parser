#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>

#include "emmcparser.h"
#include "emmcxls.h"
#include "common.h"
#include "xlsxwriter.h"
#include "glib.h"

/* commands distribution start */
typedef struct cmd_dist_data {
	GSList *list;
	int total_cmd_counts;
} cmd_dist_data;

void *prep_data_cmd_dist(mmc_parser *parser, xls_sheet_config *config)
{
	int i = 0;
	int *cmd_counts = parser->stats.cmds_dist;
	int total_cmd_counts = 0;

	cmd_dist_data *result = calloc(1, sizeof(cmd_dist_data));
	GSList *dist_list = NULL;
	GSList *item = NULL, *iterator = NULL;

	for (i=0; i<MAX_CMD_NUM; i++) {
		//dbg(L_DEBUG, "create new item\n");
		if (cmd_counts[i] != 0) {
			xls_data_entry *entry = calloc(1, sizeof(xls_data_entry));
			entry->idx = i;
			entry->val = cmd_counts[i];		//cmd counts

			dist_list = g_slist_append(dist_list, entry);
			total_cmd_counts += cmd_counts[i];
		}
	}

	dbg(L_DEBUG, "all command counts:%d\n", total_cmd_counts);

	//calc percentage
	for (iterator = dist_list; iterator; iterator = iterator->next) {
		xls_data_entry *entry = (xls_data_entry *)iterator->data;
		double per = (double)entry->val/total_cmd_counts*100;
		entry->val_1 = per;
	}

	g_slist_foreach(dist_list, (GFunc)print_xls_entry, NULL);

	result->list = dist_list;
	result->total_cmd_counts = total_cmd_counts;

	return result;
}

int write_data_cmd_dist(lxw_workbook *workbook, lxw_worksheet *worksheet, void *data, xls_sheet_config *config)
{
	cmd_dist_data *result = data;
	GSList *dist_list = result->list;
	GSList *iterator = NULL;
	int row=0, col=0;

	lxw_format *bold = workbook_add_format(workbook);
    format_set_bold(bold);

    lxw_format *format = workbook_add_format(workbook);
	format_set_num_format(format, "0.00");

	worksheet_write_string(worksheet, CELL("A1"), config->chart_x_name, bold);
    worksheet_write_string(worksheet, CELL("B1"), "Counts", bold);
    worksheet_write_string(worksheet, CELL("C1"), config->chart_y_name, bold);

    row=1;
    //g_slist_foreach(dist_list, (GFunc)write_row, NULL);
	for (iterator = dist_list; iterator; iterator = iterator->next) {
		xls_data_entry *entry = (xls_data_entry *)iterator->data;
  		worksheet_write_number(worksheet, row, col++, entry->idx, NULL);
  		worksheet_write_number(worksheet, row, col++, entry->val, NULL);
  		worksheet_write_number(worksheet, row, col++, entry->val_1, format);
  		row++;
  		col=0;
 	}

 	config->x1_area.first_row = 1;
 	config->x1_area.first_col = 0;
 	config->x1_area.last_row = row-1;
 	config->x1_area.last_col = 0;

 	config->y1_area.first_row = 1;
 	config->y1_area.first_col = 2;
 	config->y1_area.last_row = row-1;
 	config->y1_area.last_col = 2;

	//summary
    worksheet_write_string(worksheet, CELL("F1"), "Total Commands", bold);
    worksheet_write_number(worksheet, CELL("F2"), result->total_cmd_counts, NULL);

 	return 0;
}

void release_data_cmd_dist(void *data)
{
	cmd_dist_data *result = data;
	GSList *dist_list = result->list;
	g_slist_free_full(dist_list, (GDestroyNotify)destroy_xls_entry);
	free(result);
}

int mmc_xls_init_cmd_dist(mmc_parser *parser, char *csvpath, char *dir_name)
{
	GKeyFile* gkf = parser->gkf;
	GError *error;

    if (g_key_file_has_group(gkf, "Command Distribution")) {
    	char *file_name_suffix = g_key_file_get_value(gkf, "Command Distribution", "file_name_suffix", &error);
    	if (file_name_suffix == NULL) {
    		file_name_suffix = "_cmd_dist";
    	}

    	char filename[256];
		memset(filename, 0, sizeof(filename));
		char **tokens = g_strsplit_set(csvpath, "/.", -1);
		int nums = g_strv_length(tokens);
		if (dir_name) {
			strcat(filename, dir_name);
			strcat(filename, "/");
		}
		strcat(filename, tokens[nums-2]);
		strcat(filename, file_name_suffix);
		strcat(filename, ".xlsx");
		g_strfreev(tokens);
	    dbg(L_DEBUG, "filename:%s nums:%d\n", filename, nums);

		//alloc xls callback
		mmc_xls_cb *cb = alloc_xls_cb();
		//init
		cb->desc = "Command Distribution";
		cb->prep_data = prep_data_cmd_dist;
		cb->write_data = write_data_cmd_dist;
		cb->create_chart = create_chart;
		cb->release_data = release_data_cmd_dist;
		cb->config->filename = strdup(filename);

		xls_sheet_config *sheet = alloc_sheet_config();
		cb->config->sheets = g_slist_append(cb->config->sheets, sheet);

		sheet->sheet_name = "sheet1";
		sheet->chart_type = LXW_CHART_COLUMN;
		sheet->chart_title_name = "Command Distribution";
		sheet->serie_name = "Command Dist";
		sheet->chart_x_name = "Command Index";
		sheet->chart_y_name = "Percentage";
		sheet->chart_row = lxw_name_to_row("F8");
		sheet->chart_col = lxw_name_to_col("F8");
		sheet->chart_x_scale = 2;
		sheet->chart_y_scale = 2;

		mmc_register_xls_cb(parser, cb);
    }

    return 0;
}
/* commands distribution end */