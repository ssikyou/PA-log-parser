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
#include <assert.h>

#include "emmcparser.h"
#include "common.h"
#include "xlsxwriter.h"
#include "glib.h"

static int classify_req(struct mmc_parser *parser, void *arg);
static int calc_max_delay(struct mmc_parser *parser, void *arg);
mmc_req_cb cbs[] = {
	{{NULL, NULL}, "classify request to different lists", classify_req, NULL},
	{{NULL, NULL}, "calc read/write max latency/busy time", calc_max_delay, NULL},

};

static mmc_req_cb *alloc_req_cb(char *desc, int (* func)(struct mmc_parser *parser, void *arg), void *arg)
{
	mmc_req_cb *cb = calloc(1, sizeof(mmc_req_cb));
	if (cb == NULL) {
		perror("alloc mmc request cb failed");
		goto fail;
	}

	INIT_LIST_HEAD(&cb->cb_node);

	cb->desc = desc;
	cb->func = func;
	cb->arg = arg;

	return cb;

fail:
	return NULL;
}

static int find_maximum(unsigned int *array, int n)
{
	unsigned int max;
	int i, index;

	max = array[0];
	index = 0;

	for (i = 1; i < n; i++) {
		if (array[i] > max) {
		   index = i;
		   max = array[i];
		}
	}

	return index;
}

static int classify_req(struct mmc_parser *parser, void *arg)
{
	int cmd_index = parser->cur_req->cmd->cmd_index;

	switch (cmd_index) {
		case TYPE_18:
			parser->stats.cmd18_list = g_slist_prepend(parser->stats.cmd18_list, parser->cur_req);
		break;

		case TYPE_25:
			//O(1), do we need to reverse the list?
			parser->stats.cmd25_list = g_slist_prepend(parser->stats.cmd25_list, parser->cur_req);
		break;
	}

	return 0;
}

static int calc_max_delay(struct mmc_parser *parser, void *arg)
{
	int index;

	if (is_wr_cmd(parser->cur_req->cmd->cmd_index) || is_rd_cmd(parser->cur_req->cmd->cmd_index)) {
		index = find_maximum(parser->cur_req->delay, parser->cur_req->sectors);
		parser->cur_req->max_delay = parser->cur_req->delay[index];
		dbg(L_DEBUG, "max delay:%d\n", parser->cur_req->max_delay);
	}

	return 0;
}

int mmc_register_req_cb(mmc_parser *parser, mmc_req_cb *cb)
{
	list_add_tail(&cb->cb_node, &parser->cb_list);
	return 0;
}

int mmc_cb_init(mmc_parser *parser)
{
	int i;

	for (i = 0; i < NELEMS(cbs); i++) {
		INIT_LIST_HEAD(&cbs[i].cb_node);
		mmc_register_req_cb(parser, &cbs[i]);
	}

	return 0;
}


/*======================XLS and Charts Related Functions===========================*/
typedef struct xls_data_entry
{
	int idx;
	char *idx_desc;
	int val;
	double val_1;
	char *val_desc;
} xls_data_entry;

typedef struct xls_area {
	int first_row;
	int first_col;
	int last_row;
	int last_col;
} xls_area;

typedef struct xls_config {
	char *filename;
	int x_steps;

	unsigned char chart_type;
	char *chart_title_name;
	char *serie_name;
	char *chart_x_name;
	char *chart_y_name;
	xls_area x1_area;	//chart x asix's data cell range
	xls_area y1_area;	//chart y asix's data cell range
	xls_area x2_area;
	xls_area y2_area;
} xls_config;

typedef struct mmc_xls_cb {
	char *desc;
	void *(* prep_data)(mmc_parser *parser, xls_config *config);
	int (* write_data)(lxw_workbook *workbook, lxw_worksheet *worksheet, void *data, xls_config *config);
	int (* create_chart)(lxw_workbook *workbook, lxw_worksheet *worksheet, xls_config *config);
	void (* release_data)(void *data);

	xls_config *config;
} mmc_xls_cb;

void print_xls_entry(gpointer item, gpointer user_data) {
	xls_data_entry *entry = item;
	dbg(L_DEBUG, "[%d] (%s) %d %f\n", entry->idx, entry->idx_desc, entry->val, entry->val_1);
}

gint find_index(gconstpointer item1, gconstpointer item2) {
	xls_data_entry *entry = item1;

	if (entry->idx == *(int *)item2)
		return 0;
	else
		return -1;
}

/* item1 is list item, item2 is user data*/
gint find_acs(gconstpointer item1, gconstpointer item2) {
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
		item = g_slist_find_custom(dist_list, &index, (GCompareFunc)find_index);
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

			dist_list = g_slist_insert_sorted(dist_list, entry, (GCompareFunc)find_acs);

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
	                         		.x_scale  = 8, 
	                         		.y_scale  = 4,
	                         	};

    worksheet_insert_chart_opt(worksheet, CELL("F22"), chart, &options);

    return 0;
}

void destroy_xls_entry(gpointer data)
{
	xls_data_entry *entry = data;
	if (entry->idx_desc)
		free(entry->idx_desc);
	if (entry->val_desc)
		free(entry->val_desc);
}

void release_data_rw_dist(void *data)
{
	rw_dist_data *result = data;
	GSList *dist_list = result->list;
	g_slist_free_full(dist_list, (GDestroyNotify)destroy_xls_entry);
	g_slist_free(result->per_list);
}

mmc_xls_cb *alloc_xls_cb()
{
	mmc_xls_cb *cb = calloc(1, sizeof(mmc_xls_cb));
	if (cb == NULL) {
		perror("alloc mmc xls cb failed");
		goto fail;
	}

	xls_config *config = calloc(1, sizeof(xls_config));
	if (config == NULL) {
		perror("alloc mmc xls config failed");
		goto fail_config;
	}
	cb->config = config;


	return cb;

fail_config:
	free(cb);
fail:
	return NULL;
}

void destroy_xls_cb(gpointer data)
{
	mmc_xls_cb *cb = data;
	if (cb->config)
		free(cb->config);
	free(cb);
}

int mmc_register_xls_cb(mmc_parser *parser, mmc_xls_cb *cb)
{
	parser->xls_list = g_slist_append(parser->xls_list, cb);
	return 0;
}

void mmc_destroy_xls_list(GSList *list)
{
	g_slist_free_full(list, (GDestroyNotify)destroy_xls_cb);
}

int mmc_xls_init(mmc_parser *parser)
{
	GKeyFile* gkf = parser->gkf;
	GError *error;

	if (parser->has_busy && g_key_file_has_group(gkf, "Write Busy Dist")) {
    	char *filename = g_key_file_get_value(gkf, "Write Busy Dist", "file_name", &error);
    	if (filename == NULL) {
    		filename = "default_w_busy.xlsx";
    	}
    	int x_steps = g_key_file_get_integer(gkf, "Write Busy Dist", "x_steps", &error);
    	if (x_steps == 0) {
    		x_steps = 20;
    	}

		//alloc xls callback
		mmc_xls_cb *cb = alloc_xls_cb();
		//init
		cb->desc = "Write Busy Dist";
		cb->prep_data = prep_data_w_busy;
		cb->write_data = write_data_rw_dist;
		cb->create_chart = create_chart;
		cb->release_data = release_data_rw_dist;

		cb->config->filename = filename;
		cb->config->x_steps = x_steps;
		cb->config->chart_type = LXW_CHART_COLUMN;
		cb->config->chart_title_name = "CMD25 Max Busy Distribution";
		cb->config->serie_name = "Busy Dist";
		cb->config->chart_x_name = "Busy Range/us";
		cb->config->chart_y_name = "Percentage";

		mmc_register_xls_cb(parser, cb);
    }

    if (parser->has_data && g_key_file_has_group(gkf, "Read Latency Dist")) {
    	char *filename = g_key_file_get_value(gkf, "Read Latency Dist", "file_name", &error);
    	if (filename == NULL) {
    		filename = "default_r_latency.xlsx";
    	}
    	int x_steps = g_key_file_get_integer(gkf, "Read Latency Dist", "x_steps", &error);
    	if (x_steps == 0) {
    		x_steps = 20;
    	}

		//alloc xls callback
		mmc_xls_cb *cb = alloc_xls_cb();
		//init
		cb->desc = "Read Latency Dist";
		cb->prep_data = prep_data_r_latency;
		cb->write_data = write_data_rw_dist;
		cb->create_chart = create_chart;
		cb->release_data = release_data_rw_dist;

		cb->config->filename = filename;
		cb->config->x_steps = x_steps;
		cb->config->chart_type = LXW_CHART_COLUMN;
		cb->config->chart_title_name = "CMD18 Max Latency Distribution";
		cb->config->serie_name = "Latency Dist";
		cb->config->chart_x_name = "Latency Range/us";
		cb->config->chart_y_name = "Percentage";

		mmc_register_xls_cb(parser, cb);
    }

}

int generate_xls(mmc_parser *parser)
{
	GSList *xls_list = parser->xls_list;
	GSList *iterator = NULL;

	for (iterator = xls_list; iterator; iterator = iterator->next) {
		mmc_xls_cb *xls_cb = (mmc_xls_cb *)iterator->data;
		
		lxw_workbook *workbook  = workbook_new(xls_cb->config->filename);
	    lxw_worksheet *worksheet = workbook_add_worksheet(workbook, NULL);

	    //prepare data
	   	void *data = xls_cb->prep_data(parser, xls_cb->config);
	    //write data to sheet
	    xls_cb->write_data(workbook, worksheet, data, xls_cb->config);
	    //create charts
	    xls_cb->create_chart(workbook, worksheet, xls_cb->config);

	    workbook_close(workbook);

	    xls_cb->release_data(data);
	}

	return 0;
}