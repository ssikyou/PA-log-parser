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

/* read/write delay distribution start */
void print_xls_entry(gpointer item, gpointer user_data) {
	xls_data_entry *entry = item;
	dbg(L_DEBUG, "[%d] (%s) %d %f\n", entry->idx, entry->idx_desc, entry->val, entry->val_1);
}

/* find the value of type int */
gint find_int(gconstpointer item1, gconstpointer item2) {
	xls_data_entry *entry = item1;

	if (entry->idx == *(int *)item2)
		return 0;
	else
		return -1;
}

/* compare int value, acs ordering, item1 is list item, item2 is user data*/
gint compare_int_acs(gconstpointer item1, gconstpointer item2) {
	xls_data_entry *entry = item1;
	xls_data_entry *user = item2;

	if (entry->idx > user->idx)
		return 1;
	else if (entry->idx < user->idx)
		return -1;
	else
		return 0;
}

/* for percentage compare */
gint compare_per(gconstpointer item1, gconstpointer item2) {
	xls_data_entry *entry = item1;
	xls_data_entry *user = item2;

	if (entry->val_1 > user->val_1)
		return -1;
	else if (entry->val_1 < user->val_1)
		return 1;
	else
		return 0;
}

typedef struct rw_dist_data
{
	GSList *list;
	GSList *per_list;	//per desc list
	int req_counts;
	int max_delay_time;
} rw_dist_data;

void *prep_data_rw_dist(mmc_parser *parser, xls_config *config, int type)
{
	int max_delay_time = 0;
	int req_counts = 0;
	mmc_request *req;
	GSList *req_list = NULL;
	if (type == 1)
		req_list = parser->stats.cmd25_list;
	else
		req_list = parser->stats.cmd18_list;

	rw_dist_data *result = calloc(1, sizeof(rw_dist_data));
	GSList *dist_list = NULL;
	GSList *per_list = NULL;
	GSList *item = NULL, *iterator = NULL;

	/*
	 * calc x axis value nums, e.g. max busy is 200us, steps is 20us
	 * the x axis is [0~20), [20~40), ..., 180~200, [200~220)us
	 * int nums = max_delay_time/steps+1;
	 */

	for (iterator = req_list; iterator; iterator = iterator->next) {
		req_counts++;
		req = (mmc_request *)iterator->data;
		int index = req->max_delay/config->x_steps;
		
		dbg(L_DEBUG, "index:%d\n", index);
		item = g_slist_find_custom(dist_list, &index, (GCompareFunc)find_int);
		if (item) {
			dbg(L_DEBUG, "item exsit\n");
			((xls_data_entry *)item->data)->val += 1;
		} else {
			dbg(L_DEBUG, "create new item\n");
			xls_data_entry *entry = calloc(1, sizeof(xls_data_entry));
			entry->idx = index;
			char tmp[20];
			snprintf(tmp, sizeof(tmp), "[%d~%d)", index*config->x_steps, (index+1)*config->x_steps);
			entry->idx_desc = strdup(tmp);

			entry->val = 1;		//req counts
			entry->val_1 = 0;	//percentage

			dist_list = g_slist_insert_sorted(dist_list, entry, (GCompareFunc)compare_int_acs);

		}
		
		if (req->max_delay > max_delay_time)
			max_delay_time = req->max_delay;
	}

	dbg(L_DEBUG, "max delay time: %dus, all req counts:%d\n", max_delay_time, req_counts);
	
	//calc percentage
	for (iterator = dist_list; iterator; iterator = iterator->next) {
		xls_data_entry *entry = (xls_data_entry *)iterator->data;
		double per = (double)entry->val/req_counts*100;
		//char tmp[10];
		//memset(tmp, 0, sizeof(tmp));
		//snprintf(tmp, sizeof(tmp), "%.2f", per);
		//entry->val_desc = strdup(tmp);
		//sscanf(tmp, "%f", &entry->val_1);
		entry->val_1 = per;
	}

	per_list = g_slist_copy(dist_list);

	per_list = g_slist_sort(per_list, (GCompareFunc)compare_per);

	g_slist_foreach(dist_list, (GFunc)print_xls_entry, NULL);

	result->list = dist_list;
	result->per_list = per_list;
	result->req_counts = req_counts;
	result->max_delay_time = max_delay_time;

	return result;
}

void *prep_data_w_busy(mmc_parser *parser, xls_config *config)
{
	return prep_data_rw_dist(parser, config, 1);	//1->write, 0->read
}

void *prep_data_r_latency(mmc_parser *parser, xls_config *config)
{
	return prep_data_rw_dist(parser, config, 0);	//1->write, 0->read
}

int write_data_rw_dist(lxw_workbook *workbook, lxw_worksheet *worksheet, void *data, xls_config *config)
{
	rw_dist_data *result = data;
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
    worksheet_write_string(worksheet, CELL("C1"), "Request Counts", bold);
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
    worksheet_write_number(worksheet, CELL("F2"), result->req_counts, NULL);
    worksheet_write_string(worksheet, CELL("G1"), "Max Delay Time/us", bold);
    worksheet_write_number(worksheet, CELL("G2"), result->max_delay_time, NULL);

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

int create_chart(lxw_workbook *workbook, lxw_worksheet *worksheet, xls_config *config)
{
	//create chart
    lxw_chart *chart = workbook_add_chart(workbook, config->chart_type);
    lxw_chart_series *series = chart_add_series(chart, NULL, NULL);

    chart_series_set_categories(series, "Sheet1", config->x1_area.first_row, config->x1_area.first_col, config->x1_area.last_row, config->x1_area.last_col);
    chart_series_set_values(series, "Sheet1", config->y1_area.first_row, config->y1_area.first_col, config->y1_area.last_row, config->y1_area.last_col);
    chart_series_set_name(series, config->serie_name);

    chart_title_set_name(chart, config->chart_title_name);
    chart_axis_set_name(chart->x_axis, config->chart_x_name);
    chart_axis_set_name(chart->y_axis, config->chart_y_name);

	lxw_image_options options = {	.x_offset = 0,
									.y_offset = 0,
	                         		.x_scale  = config->chart_x_scale, 
	                         		.y_scale  = config->chart_y_scale,
	                         	};

    worksheet_insert_chart_opt(worksheet, config->chart_row, config->chart_col, chart, &options);

    return 0;
}

void destroy_xls_entry(gpointer data)
{
	xls_data_entry *entry = data;
	if (entry->idx_desc)
		free(entry->idx_desc);
	if (entry->val_desc)
		free(entry->val_desc);
	free(entry);
}

void release_data_rw_dist(void *data)
{
	rw_dist_data *result = data;
	GSList *dist_list = result->list;
	g_slist_free_full(dist_list, (GDestroyNotify)destroy_xls_entry);
	g_slist_free(result->per_list);
	free(result);
}

int mmc_xls_init_rw_dist(mmc_parser *parser, char *csvpath, char *dir_name)
{
	GKeyFile* gkf = parser->gkf;
	GError *error;

	if (parser->has_busy && g_key_file_has_group(gkf, "Write Busy Dist")) {
    	char *file_name_suffix = g_key_file_get_value(gkf, "Write Busy Dist", "file_name_suffix", &error);
    	if (file_name_suffix == NULL) {
    		file_name_suffix = "_w_busy";
    	}
    	int x_steps = g_key_file_get_integer(gkf, "Write Busy Dist", "x_steps", &error);
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
		cb->desc = "Write Busy Dist";
		cb->prep_data = prep_data_w_busy;
		cb->write_data = write_data_rw_dist;
		cb->create_chart = create_chart;
		cb->release_data = release_data_rw_dist;

		cb->config->filename = strdup(filename);
		cb->config->x_steps = x_steps;
		cb->config->chart_type = LXW_CHART_COLUMN;
		cb->config->chart_title_name = "CMD25 Max Busy Distribution";
		cb->config->serie_name = "Busy Dist";
		cb->config->chart_x_name = "Busy Range/us";
		cb->config->chart_y_name = "Percentage";
		cb->config->chart_row = lxw_name_to_row("F22");
		cb->config->chart_col = lxw_name_to_col("F22");
		cb->config->chart_x_scale = 8;
		cb->config->chart_y_scale = 4;

		mmc_register_xls_cb(parser, cb);
    }

    if (parser->has_data && g_key_file_has_group(gkf, "Read Latency Dist")) {
    	char *file_name_suffix = g_key_file_get_value(gkf, "Read Latency Dist", "file_name_suffix", &error);
    	if (file_name_suffix == NULL) {
    		file_name_suffix = "_r_latency";
    	}
    	int x_steps = g_key_file_get_integer(gkf, "Read Latency Dist", "x_steps", &error);
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
		cb->desc = "Read Latency Dist";
		cb->prep_data = prep_data_r_latency;
		cb->write_data = write_data_rw_dist;
		cb->create_chart = create_chart;
		cb->release_data = release_data_rw_dist;

		cb->config->filename = strdup(filename);
		cb->config->x_steps = x_steps;
		cb->config->chart_type = LXW_CHART_COLUMN;
		cb->config->chart_title_name = "CMD18 Max Latency Distribution";
		cb->config->serie_name = "Latency Dist";
		cb->config->chart_x_name = "Latency Range/us";
		cb->config->chart_y_name = "Percentage";
		cb->config->chart_row = lxw_name_to_row("F22");
		cb->config->chart_col = lxw_name_to_col("F22");
		cb->config->chart_x_scale = 8;
		cb->config->chart_y_scale = 4;

		mmc_register_xls_cb(parser, cb);
    }

    return 0;
}
/* read/write delay distribution end */