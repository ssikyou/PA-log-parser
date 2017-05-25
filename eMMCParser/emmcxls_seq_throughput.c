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

typedef struct seq_throughput_data {
	GSList *list;
	int total_req_counts;
	int max_delay;
	int max_throughput;
} seq_throughput_data;

void *prep_data_seq_throughput(mmc_parser *parser, xls_sheet_config *config)
{
	int req_counts = 0;
	int max_delay_time = 0;
	int max_throughput = 0;
	mmc_request *req;
	GSList *req_list = NULL;

	if (strcmp(config->sheet_name, "cmd25") == 0)
		req_list = parser->stats.cmd25_list;
	else if (strcmp(config->sheet_name, "cmd18") == 0)
		req_list = parser->stats.cmd18_list;
	else if (strcmp(config->sheet_name, "cmd24") == 0)
		req_list = parser->stats.cmd24_list;
	else if (strcmp(config->sheet_name, "cmd17") == 0)
		req_list = parser->stats.cmd17_list;

	seq_throughput_data *result = calloc(1, sizeof(seq_throughput_data));
	GSList *dist_list = NULL;
	GSList *item = NULL, *iterator = NULL;

	for (iterator = req_list; iterator; iterator = iterator->next) {
		req_counts++;
		req = (mmc_request *)iterator->data;

		xls_data_entry *entry = calloc(1, sizeof(xls_data_entry));
		entry->idx = req->cmd->event_id;

		entry->val = req->max_delay;
		if (req->sectors>0)
			entry->val_2 = req->total_time/req->sectors;

		dist_list = g_slist_append(dist_list, entry);

		if (req->max_delay > max_delay_time)
			max_delay_time = req->max_delay;

		if (entry->val_2 > max_throughput)
			max_throughput = entry->val_2;
	}

	dbg(L_DEBUG, "max delay: %d, max throughput: %d, all req counts:%d\n", max_delay_time, max_throughput, req_counts);

	g_slist_foreach(dist_list, (GFunc)print_xls_entry, NULL);

	result->list = dist_list;
	result->total_req_counts = req_counts;
	result->max_delay = max_delay_time;
	result->max_throughput = max_throughput;

	return result;
}

int write_data_seq_throughput(lxw_workbook *workbook, lxw_worksheet *worksheet, void *data, xls_sheet_config *config)
{
	seq_throughput_data *result = data;
	GSList *dist_list = result->list;
	GSList *iterator = NULL;
	int row=0, col=0;

	lxw_format *bold = workbook_add_format(workbook);
    format_set_bold(bold);

    lxw_format *format = workbook_add_format(workbook);
	format_set_num_format(format, "0.00");

	worksheet_write_string(worksheet, CELL("A1"), config->chart_x_name, bold);
    worksheet_write_string(worksheet, CELL("B1"), "Max Delay(us)", bold);
    worksheet_write_string(worksheet, CELL("C1"), "Throughput(us/sector)", bold);

    row=1;
    //g_slist_foreach(dist_list, (GFunc)write_row, NULL);
	for (iterator = dist_list; iterator; iterator = iterator->next) {
		xls_data_entry *entry = (xls_data_entry *)iterator->data;
  		worksheet_write_number(worksheet, row, col++, entry->idx, NULL);
  		worksheet_write_number(worksheet, row, col++, entry->val, NULL);
  		worksheet_write_number(worksheet, row, col++, entry->val_2, NULL);
  		row++;
  		col=0;
 	}

 	config->x1_area.first_row = 1;
 	config->x1_area.first_col = 0;
 	config->x1_area.last_row = row-1;
 	config->x1_area.last_col = 0;

 	config->y1_area.first_row = 1;
 	config->y1_area.first_col = 1;
 	config->y1_area.last_row = row-1;
 	config->y1_area.last_col = 1;

 	config->y2_area.first_row = 1;
 	config->y2_area.first_col = 2;
 	config->y2_area.last_row = row-1;
 	config->y2_area.last_col = 2;

	//summary
    worksheet_write_string(worksheet, CELL("F1"), "Total Requests", bold);
    worksheet_write_number(worksheet, CELL("F2"), result->total_req_counts, NULL);

    worksheet_write_string(worksheet, CELL("G1"), "Max Delay(us)", bold);
    worksheet_write_number(worksheet, CELL("G2"), result->max_delay, NULL);

    worksheet_write_string(worksheet, CELL("H1"), "Max Throughput(us/sector)", bold);
    worksheet_write_number(worksheet, CELL("H2"), result->max_throughput, NULL);

 	return 0;
}

void release_data_seq_throughput(void *data)
{
	seq_throughput_data *result = data;
	GSList *dist_list = result->list;
	g_slist_free_full(dist_list, (GDestroyNotify)destroy_xls_entry);
	free(result);
}

int mmc_xls_init_seq_throughput(mmc_parser *parser, char *csvpath, char *dir_name)
{
	GKeyFile* gkf = parser->gkf;
	GError *error;

    if ((parser->has_busy || parser->has_data) && g_key_file_has_group(gkf, "Seq Throughput")) {
    	char *file_name_suffix = g_key_file_get_value(gkf, "Seq Throughput", "file_name_suffix", &error);
    	if (file_name_suffix == NULL) {
    		file_name_suffix = "_seq_throughput";
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
		cb->desc = "Seq Throughput";
		cb->prep_data = prep_data_seq_throughput;
		cb->write_data = write_data_seq_throughput;
		cb->create_chart = create_chart;
		cb->release_data = release_data_seq_throughput;
		cb->config->filename = strdup(filename);

		xls_sheet_config *sheet = NULL;

		if (parser->has_busy) {
			xls_sheet_config *sheet = alloc_sheet_config();
			cb->config->sheets = g_slist_append(cb->config->sheets, sheet);

			sheet->sheet_name = "cmd25";
			sheet->chart_type = LXW_CHART_LINE;
			sheet->chart_title_name = "CMD25 Throughput";
			sheet->serie_name = "Max Busy Time";
			sheet->serie2_name = "Time per Sector";
			sheet->chart_x_name = "Command Event ID";
			sheet->chart_y_name = "Busy Time or Time per Sector";
			sheet->chart_row = lxw_name_to_row("F8");
			sheet->chart_col = lxw_name_to_col("F8");
			sheet->chart_x_scale = 8;
			sheet->chart_y_scale = 4;
		}

		if (parser->has_data) {
			xls_sheet_config *sheet = alloc_sheet_config();
			cb->config->sheets = g_slist_append(cb->config->sheets, sheet);

			sheet->sheet_name = "cmd18";
			sheet->chart_type = LXW_CHART_LINE;
			sheet->chart_title_name = "CMD18 Throughput";
			sheet->serie_name = "Max Latency Time";
			sheet->serie2_name = "Time per Sector";
			sheet->chart_x_name = "Command Event ID";
			sheet->chart_y_name = "Latency Time or Time per Sector";
			sheet->chart_row = lxw_name_to_row("F8");
			sheet->chart_col = lxw_name_to_col("F8");
			sheet->chart_x_scale = 8;
			sheet->chart_y_scale = 4;
		}

		if (parser->has_busy) {
			xls_sheet_config *sheet = alloc_sheet_config();
			cb->config->sheets = g_slist_append(cb->config->sheets, sheet);

			sheet->sheet_name = "cmd24";
			sheet->chart_type = LXW_CHART_LINE;
			sheet->chart_title_name = "CMD24 Throughput";
			sheet->serie_name = "Max Busy Time";
			sheet->serie2_name = "Time per Sector";
			sheet->chart_x_name = "Command Event ID";
			sheet->chart_y_name = "Busy Time or Time per Sector";
			sheet->chart_row = lxw_name_to_row("F8");
			sheet->chart_col = lxw_name_to_col("F8");
			sheet->chart_x_scale = 6;
			sheet->chart_y_scale = 3;
		}

		if (parser->has_data) {
			xls_sheet_config *sheet = alloc_sheet_config();
			cb->config->sheets = g_slist_append(cb->config->sheets, sheet);

			sheet->sheet_name = "cmd17";
			sheet->chart_type = LXW_CHART_LINE;
			sheet->chart_title_name = "CMD17 Throughput";
			sheet->serie_name = "Max Latency Time";
			sheet->serie2_name = "Time per Sector";
			sheet->chart_x_name = "Command Event ID";
			sheet->chart_y_name = "Latency Time or Time per Sector";
			sheet->chart_row = lxw_name_to_row("F8");
			sheet->chart_col = lxw_name_to_col("F8");
			sheet->chart_x_scale = 6;
			sheet->chart_y_scale = 3;
		}

		mmc_register_xls_cb(parser, cb);
    }


    return 0;
}