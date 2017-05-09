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
static int calc_max_busy(struct mmc_parser *parser, void *arg);
mmc_req_cb cbs[] = {
	{{NULL, NULL}, "classify request to different lists", classify_req, NULL},
	{{NULL, NULL}, "calc write max busy time", calc_max_busy, NULL},

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
		break;

		case TYPE_25:
			//O(1), do we need to reverse the list?
			parser->stats.cmd25_list = g_slist_prepend(parser->stats.cmd25_list, parser->cur_req);
		break;
	}

	return 0;
}

static int calc_max_busy(struct mmc_parser *parser, void *arg)
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



typedef struct xls_data_entry
{
	int idx;
	char *idx_desc;
	int val;
	char *val_desc;
} xls_data_entry;

void print_xls_entry(gpointer item, gpointer user_data) {
	xls_data_entry *entry = item;
	dbg(L_DEBUG, "[%d] (%s) %d\n", entry->idx, entry->idx_desc, entry->val);
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


void *prep_data(mmc_parser *parser, int steps)
{
	int max_delay_time = 0;
	mmc_request *req;
	//struct list_head *list = &parser->stats.cmd25_list;
	GSList *cmd25_list = parser->stats.cmd25_list;
	GSList *busy_list = NULL;
	GSList *item = NULL, *iterator = NULL;

	/*
	 * calc x axis value nums, e.g. max busy is 200us, steps is 20us
	 * the x axis is [0~20), [20~40), ..., 180~200, [200~220)us
	 * int nums = max_delay_time/steps+1;
	 */
	//list_for_each_entry(req, list, req_node) {
	for (iterator = cmd25_list; iterator; iterator = iterator->next) {

		req = (mmc_request *)iterator->data;
		int index = req->max_delay/steps;
		
		dbg(L_DEBUG, "index:%d\n", index);
		item = g_slist_find_custom(busy_list, &index, (GCompareFunc)find_index);
		if (item) {
			dbg(L_DEBUG, "item exsit\n");
			((xls_data_entry *)item->data)->val += 1;
		} else {
			dbg(L_DEBUG, "create new item\n");
			xls_data_entry *entry = calloc(1, sizeof(xls_data_entry));
			entry->idx = index;
			char tmp[16];
			snprintf(tmp, sizeof(tmp), "[%d~%d)", index*steps, (index+1)*steps);
			entry->idx_desc = strdup(tmp);

			entry->val = 1;

			busy_list = g_slist_insert_sorted(busy_list, entry, (GCompareFunc)find_acs);

		}
		
		if (req->max_delay > max_delay_time)
			max_delay_time = req->max_delay;
	}

	dbg(L_DEBUG, "max delay time: %dus\n", max_delay_time);
	g_slist_foreach(busy_list, (GFunc)print_xls_entry, NULL);

	return busy_list;
}

void destroy_xls_entry(gpointer data)
{
	xls_data_entry *entry = data;
	if (entry->idx_desc)
		free(entry->idx_desc);
}

void destroy_data(void *data)
{
	GSList *busy_list = data;
	g_slist_free_full(busy_list, (GDestroyNotify)destroy_xls_entry);
}

typedef struct xls_area {
	int first_row;
	int first_col;
	int last_row;
	int last_col;
} xls_area;
void write_worksheet_data(lxw_worksheet *worksheet, void *data, xls_area *x_area, xls_area *y_area)
{
	GSList *busy_list = data;
	GSList *iterator = NULL;
	int row=0, col=0;
	char x_desc[] = "Busy Range/us";
	char y_desc[] = "Counts";

	worksheet_write_string(worksheet, CELL("A1"), "ID", NULL);
    worksheet_write_string(worksheet, CELL("B1"), x_desc, NULL);
    worksheet_write_string(worksheet, CELL("C1"), y_desc, NULL);
    row=1;
    //g_slist_foreach(busy_list, (GFunc)write_row, NULL);
	for (iterator = busy_list; iterator; iterator = iterator->next) {
		xls_data_entry *entry = (xls_data_entry *)iterator->data;
  		worksheet_write_number(worksheet, row, col++, entry->idx, NULL);
  		worksheet_write_string(worksheet, row, col++, entry->idx_desc, NULL);
  		worksheet_write_number(worksheet, row, col++, entry->val, NULL);
  		row++;
  		col=0;
 	}

 	x_area->first_row = 1;
 	x_area->first_col = 1;
 	x_area->last_row = row-1;
 	x_area->last_col = 1;

 	y_area->first_row = 1;
 	y_area->first_col = 2;
 	y_area->last_row = row-1;
 	y_area->last_col = 2;

}

int generate_xls_busy_dist(mmc_parser *parser, char *filename, int steps)
{
	lxw_workbook  *workbook  = workbook_new(filename);
    lxw_worksheet *worksheet = workbook_add_worksheet(workbook, NULL);

    xls_area xa, ya;

    //prepare data
   	void *data = prep_data(parser, steps);
    //write data to sheet
    write_worksheet_data(worksheet, data, &xa, &ya);

    //create chart
    lxw_chart *chart = workbook_add_chart(workbook, LXW_CHART_COLUMN);
    lxw_chart_series *series = chart_add_series(chart, NULL, NULL);

    chart_series_set_categories(series, "Sheet1", xa.first_row, xa.first_col, xa.last_row, xa.last_col);
    chart_series_set_values(series,     "Sheet1", ya.first_row, ya.first_col, ya.last_row, ya.last_col);

    chart_title_set_name(chart, "CMD25 Max Busy Distribution");
    chart_axis_set_name(chart->x_axis, "Busy Range");
    chart_axis_set_name(chart->y_axis, "Request Counts");

	lxw_image_options options = {	.x_offset = 0,
									.y_offset = 0,
	                         		.x_scale  = 8, 
	                         		.y_scale  = 4,
	                         	};

    worksheet_insert_chart_opt(worksheet, CELL("F4"), chart, &options);

    workbook_close(workbook);

    destroy_data(data);

	return 0;
}