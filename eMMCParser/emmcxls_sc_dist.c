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

typedef struct sc_dist_data {
	GSList *list;
	int total_req_counts;
	int max_sectors;
} sc_dist_data;

void *prep_data_sc_dist(mmc_parser *parser, xls_config *config, int type)
{
	int max_sectors = 0;
	int req_counts = 0;
	int sectors = 0;
	mmc_request *req;
	GSList *req_list = NULL;

	if (type == 1)
		req_list = parser->stats.cmd25_list;
	else
		req_list = parser->stats.cmd18_list;

	sc_dist_data *result = calloc(1, sizeof(sc_dist_data));
	GSList *dist_list = NULL;
	GSList *item = NULL, *iterator = NULL;

	for (iterator = req_list; iterator; iterator = iterator->next) {
		req_counts++;
		req = (mmc_request *)iterator->data;
		sectors = req->sectors;
		
		item = g_slist_find_custom(dist_list, &sectors, (GCompareFunc)find_int);
		if (item) {
			dbg(L_DEBUG, "item exsit\n");
			((xls_data_entry *)item->data)->val += 1;
		} else {
			dbg(L_DEBUG, "create new item\n");
			xls_data_entry *entry = calloc(1, sizeof(xls_data_entry));
			entry->idx = sectors;

			entry->val = 1;		//sc counts
			entry->val_1 = 0;	//percentage

			dist_list = g_slist_insert_sorted(dist_list, entry, (GCompareFunc)compare_int_acs);

		}
		
		if (sectors > max_sectors)
			max_sectors = sectors;
	}

	dbg(L_DEBUG, "max sectors: %d, all req counts:%d\n", max_sectors, req_counts);

	//calc percentage
	for (iterator = dist_list; iterator; iterator = iterator->next) {
		xls_data_entry *entry = (xls_data_entry *)iterator->data;
		double per = (double)entry->val/req_counts*100;
		entry->val_1 = per;
	}

	g_slist_foreach(dist_list, (GFunc)print_xls_entry, NULL);

	result->list = dist_list;
	result->total_req_counts = req_counts;
	result->max_sectors = max_sectors;

	return result;
}

void *prep_data_sc_dist_w(mmc_parser *parser, xls_config *config)
{
	return prep_data_sc_dist(parser, config, 1);	//1->write, 0->read
}

void *prep_data_sc_dist_r(mmc_parser *parser, xls_config *config)
{
	return prep_data_sc_dist(parser, config, 0);	//1->write, 0->read
}

int write_data_sc_dist(lxw_workbook *workbook, lxw_worksheet *worksheet, void *data, xls_config *config)
{
	sc_dist_data *result = data;
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

     worksheet_write_string(worksheet, CELL("G1"), "Max Sectors", bold);
    worksheet_write_number(worksheet, CELL("G2"), result->max_sectors, NULL);

 	return 0;
}

void release_data_sc_dist(void *data)
{
	sc_dist_data *result = data;
	GSList *dist_list = result->list;
	g_slist_free_full(dist_list, (GDestroyNotify)destroy_xls_entry);
	free(result);
}

int mmc_xls_init_sc_dist(mmc_parser *parser, char *csvpath, char *dir_name)
{
	GKeyFile* gkf = parser->gkf;
	GError *error;

    if (g_key_file_has_group(gkf, "Write Sector Dist")) {
    	char *file_name_suffix = g_key_file_get_value(gkf, "Write Sector Dist", "file_name_suffix", &error);
    	if (file_name_suffix == NULL) {
    		file_name_suffix = "_w_sc";
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
		cb->desc = "Write Sector Dist";
		cb->prep_data = prep_data_sc_dist_w;
		cb->write_data = write_data_sc_dist;
		cb->create_chart = create_chart;
		cb->release_data = release_data_sc_dist;

		cb->config->filename = strdup(filename);
		cb->config->chart_type = LXW_CHART_COLUMN;
		cb->config->chart_title_name = "CMD25 Sector Distribution";
		cb->config->serie_name = "CMD25 Sector Dist";
		cb->config->chart_x_name = "Sectors";
		cb->config->chart_y_name = "Percentage";
		cb->config->chart_row = lxw_name_to_row("F8");
		cb->config->chart_col = lxw_name_to_col("F8");
		cb->config->chart_x_scale = 3;
		cb->config->chart_y_scale = 2;

		mmc_register_xls_cb(parser, cb);
    }

    if (g_key_file_has_group(gkf, "Read Sector Dist")) {
    	char *file_name_suffix = g_key_file_get_value(gkf, "Read Sector Dist", "file_name_suffix", &error);
    	if (file_name_suffix == NULL) {
    		file_name_suffix = "_r_sc";
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
		cb->desc = "Read Sector Dist";
		cb->prep_data = prep_data_sc_dist_r;
		cb->write_data = write_data_sc_dist;
		cb->create_chart = create_chart;
		cb->release_data = release_data_sc_dist;

		cb->config->filename = strdup(filename);
		cb->config->chart_type = LXW_CHART_COLUMN;
		cb->config->chart_title_name = "CMD18 Sector Distribution";
		cb->config->serie_name = "CMD18 Sector Dist";
		cb->config->chart_x_name = "Sectors";
		cb->config->chart_y_name = "Percentage";
		cb->config->chart_row = lxw_name_to_row("F8");
		cb->config->chart_col = lxw_name_to_col("F8");
		cb->config->chart_x_scale = 3;
		cb->config->chart_y_scale = 2;

		mmc_register_xls_cb(parser, cb);
    }

    return 0;
}