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

extern gint compare_int_acs(gconstpointer item1, gconstpointer item2);
extern gint find_uint(gconstpointer item1, gconstpointer item2);
extern gint compare_per(gconstpointer item1, gconstpointer item2);

typedef struct idle_dist_data {
	GSList *list;
	GSList *per_list;	//per desc list
	int total_req_counts;
	int max_idle_time;
} idle_dist_data;

void *prep_data_idle_dist(mmc_parser *parser, xls_sheet_config *config)
{
	int max_idle_time = 0;
	int req_counts = 0;
	mmc_request *req;
	GSList *req_list = parser->stats.requests_list;

	idle_dist_data *result = calloc(1, sizeof(idle_dist_data));
	GSList *dist_list = NULL;
	GSList *per_list = NULL;
	GSList *item = NULL, *iterator = NULL;

	for (iterator = req_list; iterator; iterator = iterator->next) {
		req_counts++;
		req = (mmc_request *)iterator->data;
		int index = req->idle_time/config->x_steps;
		
		item = g_slist_find_custom(dist_list, &index, (GCompareFunc)find_uint);
		if (item) {
			((xls_data_entry *)item->data)->val += 1;
		} else {
			xls_data_entry *entry = calloc(1, sizeof(xls_data_entry));
			entry->idx = index;
			char tmp[20];
			snprintf(tmp, sizeof(tmp), "[%d~%d)", index*config->x_steps, (index+1)*config->x_steps);
			entry->idx_desc = strdup(tmp);

			entry->val = 1;		//idle_time counts
			entry->val_1 = 0;	//percentage

			dist_list = g_slist_insert_sorted(dist_list, entry, (GCompareFunc)compare_int_acs);

		}

		if (req->idle_time > max_idle_time)
			max_idle_time = req->idle_time;
	}

	dbg(L_DEBUG, "max idle time: %dus, all req counts:%d\n", max_idle_time, req_counts);

	//calc percentage
	for (iterator = dist_list; iterator; iterator = iterator->next) {
		xls_data_entry *entry = (xls_data_entry *)iterator->data;
		double per = (double)entry->val/req_counts*100;
		entry->val_1 = per;
	}

	per_list = g_slist_copy(dist_list);

	per_list = g_slist_sort(per_list, (GCompareFunc)compare_per);

	g_slist_foreach(dist_list, (GFunc)print_xls_entry, NULL);

	result->list = dist_list;
	result->per_list = per_list;
	result->total_req_counts = req_counts;
	result->max_idle_time = max_idle_time;

	return result;
}

int write_data_idle_dist(lxw_workbook *workbook, lxw_worksheet *worksheet, void *data, xls_sheet_config *config)
{
	idle_dist_data *result = data;
	GSList *dist_list = result->list;
	GSList *per_list = result->per_list;
	GSList *iterator = NULL;
	int row=0, col=0;

	lxw_format *bold = workbook_add_format(workbook);
    format_set_bold(bold);

    lxw_format *format = workbook_add_format(workbook);
	format_set_num_format(format, "0.00");

	worksheet_write_string(worksheet, CELL("A1"), "ID", bold);
	worksheet_write_string(worksheet, CELL("B1"), config->chart_x_name, bold);
    worksheet_write_string(worksheet, CELL("C1"), "Counts", bold);
    worksheet_write_string(worksheet, CELL("D1"), config->chart_y_name, bold);

    row=1;
    //g_slist_foreach(dist_list, (GFunc)write_row, NULL);
	for (iterator = dist_list; iterator; iterator = iterator->next) {
		xls_data_entry *entry = (xls_data_entry *)iterator->data;
  		worksheet_write_number(worksheet, row, col++, entry->idx, NULL);
  		worksheet_write_string(worksheet, row, col++, entry->idx_desc, NULL);
  		worksheet_write_number(worksheet, row, col++, entry->val, NULL);
  		worksheet_write_number(worksheet, row, col++, entry->val_1, format);
  		row++;
  		col=0;
 	}

 	config->x1_area.first_row = 1;
 	config->x1_area.first_col = 1;
 	config->x1_area.last_row = row-1;
 	config->x1_area.last_col = 1;

 	config->y1_area.first_row = 1;
 	config->y1_area.first_col = 3;
 	config->y1_area.last_row = row-1;
 	config->y1_area.last_col = 3;

	//summary
    worksheet_write_string(worksheet, CELL("F1"), "Total Requests", bold);
    worksheet_write_number(worksheet, CELL("F2"), result->total_req_counts, NULL);
    worksheet_write_string(worksheet, CELL("G1"), "Max Idle Time/us", bold);
    worksheet_write_number(worksheet, CELL("G2"), result->max_idle_time, NULL);

 	worksheet_write_string(worksheet, CELL("H1"), "Top percentage", bold);
 	worksheet_write_string(worksheet, CELL("I1"), config->chart_x_name, bold);
    worksheet_write_string(worksheet, CELL("J1"), config->chart_y_name, bold);
 	row=1;
 	int start_col = lxw_name_to_col("I");
 	col = start_col;
	for (iterator = per_list; row<16 && iterator; iterator = iterator->next) {
		xls_data_entry *entry = (xls_data_entry *)iterator->data;
  		worksheet_write_string(worksheet, row, col++, entry->idx_desc, NULL);
  		worksheet_write_number(worksheet, row, col++, entry->val_1, format);
  		row++;
  		col = start_col;
 	}

 	char tmp[16];
	memset(tmp, 0, sizeof(tmp));
	snprintf(tmp, sizeof(tmp), "=SUM(J2:J%d)", row);
 	worksheet_write_formula(worksheet, row, lxw_name_to_col("J"), tmp, NULL);

 	return 0;
}

void release_data_idle_dist(void *data)
{
	idle_dist_data *result = data;
	GSList *dist_list = result->list;
	g_slist_free_full(dist_list, (GDestroyNotify)destroy_xls_entry);
	g_slist_free(result->per_list);
	free(result);
}

int mmc_xls_init_idle_dist(mmc_parser *parser, char *csvpath, char *dir_name)
{
	GKeyFile* gkf = parser->gkf;
	GError *error;

    if (parser->has_busy && parser->has_data && g_key_file_has_group(gkf, "Request Idle Distribution")) {
    	char *file_name_suffix = g_key_file_get_value(gkf, "Request Idle Distribution", "file_name_suffix", &error);
    	if (file_name_suffix == NULL) {
    		file_name_suffix = "_idle_dist";
    	}
    	int x_steps = g_key_file_get_integer(gkf, "Request Idle Distribution", "x_steps", &error);
    	if (x_steps == 0) {
    		x_steps = 20;
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
		cb->desc = "Request Idle Dist";
		cb->prep_data = prep_data_idle_dist;
		cb->write_data = write_data_idle_dist;
		cb->create_chart = create_chart;
		cb->release_data = release_data_idle_dist;
		cb->config->filename = strdup(filename);

		xls_sheet_config *sheet = alloc_sheet_config();
		cb->config->sheets = g_slist_append(cb->config->sheets, sheet);

		sheet->sheet_name = "sheet1";
		sheet->x_steps = x_steps;
		sheet->chart_type = LXW_CHART_COLUMN;
		sheet->chart_title_name = "Request Idle Time Distribution";
		sheet->serie_name = "Request Idle Dist";
		sheet->chart_x_name = "Idle Time/us";
		sheet->chart_y_name = "Percentage";
		sheet->chart_row = lxw_name_to_row("F22");
		sheet->chart_col = lxw_name_to_col("F22");
		sheet->chart_x_scale = 6;
		sheet->chart_y_scale = 3;

		mmc_register_xls_cb(parser, cb);
    }


    return 0;
}