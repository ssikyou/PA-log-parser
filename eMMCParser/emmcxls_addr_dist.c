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

extern gint find_int(gconstpointer item1, gconstpointer item2);
extern gint compare_int_acs(gconstpointer item1, gconstpointer item2);

typedef struct addr_dist_data {
	GSList *list;
	int total_req_counts;
} addr_dist_data;

/* find the value of type unsigned int */
gint find_uint(gconstpointer item1, gconstpointer item2) {
	xls_data_entry *entry = item1;

	if (entry->idx == *(unsigned int *)item2)
		return 0;
	else
		return -1;
}

void *prep_data_addr_dist(mmc_parser *parser, xls_sheet_config *config)
{
	int req_counts = 0;
	unsigned int address = 0;
	mmc_request *req;
	GSList *req_list = NULL;

	if (strcmp(config->sheet_name, "cmd25") == 0)
		req_list = parser->stats.cmd25_list;
	else
		req_list = parser->stats.cmd18_list;

	addr_dist_data *result = calloc(1, sizeof(addr_dist_data));
	GSList *dist_list = NULL;
	GSList *item = NULL, *iterator = NULL;

	for (iterator = req_list; iterator; iterator = iterator->next) {
		req_counts++;
		req = (mmc_request *)iterator->data;
		address = req->cmd->arg;
		
		item = g_slist_find_custom(dist_list, &address, (GCompareFunc)find_uint);
		if (item) {
			((xls_data_entry *)item->data)->val += 1;
		} else {
			xls_data_entry *entry = calloc(1, sizeof(xls_data_entry));
			entry->idx = address;

			entry->val = 1;		//addr counts
			entry->val_1 = 0;	//percentage

			dist_list = g_slist_insert_sorted(dist_list, entry, (GCompareFunc)compare_int_acs);

		}

	}

	//calc percentage
	for (iterator = dist_list; iterator; iterator = iterator->next) {
		xls_data_entry *entry = (xls_data_entry *)iterator->data;
		double per = (double)entry->val/req_counts*100;
		entry->val_1 = per;
	}

	g_slist_foreach(dist_list, (GFunc)print_xls_entry, NULL);

	result->list = dist_list;
	result->total_req_counts = req_counts;

	return result;
}

int write_data_addr_dist(lxw_workbook *workbook, lxw_worksheet *worksheet, void *data, xls_sheet_config *config)
{
	addr_dist_data *result = data;
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
    worksheet_write_string(worksheet, CELL("F1"), "Total Requests", bold);
    worksheet_write_number(worksheet, CELL("F2"), result->total_req_counts, NULL);

 	return 0;
}

void release_data_addr_dist(void *data)
{
	addr_dist_data *result = data;
	GSList *dist_list = result->list;
	g_slist_free_full(dist_list, (GDestroyNotify)destroy_xls_entry);
	free(result);
}

int mmc_xls_init_addr_dist(mmc_parser *parser, char *csvpath, char *dir_name)
{
	GKeyFile* gkf = parser->gkf;
	GError *error;

    if (g_key_file_has_group(gkf, "Address Distribution")) {
    	char *file_name_suffix = g_key_file_get_value(gkf, "Address Distribution", "file_name_suffix", &error);
    	if (file_name_suffix == NULL) {
    		file_name_suffix = "_addr_dist";
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
		cb->desc = "Address Distribution";
		cb->prep_data = prep_data_addr_dist;
		cb->write_data = write_data_addr_dist;
		cb->create_chart = create_chart;
		cb->release_data = release_data_addr_dist;
		cb->config->filename = strdup(filename);

		//sheet1
		xls_sheet_config *sheet = alloc_sheet_config();
		cb->config->sheets = g_slist_append(cb->config->sheets, sheet);

		sheet->sheet_name = "cmd25";
		sheet->chart_type = LXW_CHART_SCATTER;
		sheet->chart_title_name = "CMD25 Address Distribution";
		sheet->serie_name = "CMD25 Address Dist";
		sheet->chart_x_name = "Address";
		sheet->chart_y_name = "Percentage";
		sheet->chart_row = lxw_name_to_row("F8");
		sheet->chart_col = lxw_name_to_col("F8");
		sheet->chart_x_scale = 3;
		sheet->chart_y_scale = 2;

		//sheet2
		sheet = alloc_sheet_config();
		cb->config->sheets = g_slist_append(cb->config->sheets, sheet);

		sheet->sheet_name = "cmd18";
		sheet->chart_type = LXW_CHART_SCATTER;
		sheet->chart_title_name = "CMD18 Address Distribution";
		sheet->serie_name = "CMD18 Address Dist";
		sheet->chart_x_name = "Address";
		sheet->chart_y_name = "Percentage";
		sheet->chart_row = lxw_name_to_row("F8");
		sheet->chart_col = lxw_name_to_col("F8");
		sheet->chart_x_scale = 3;
		sheet->chart_y_scale = 2;

		mmc_register_xls_cb(parser, cb);
    }

    return 0;
}