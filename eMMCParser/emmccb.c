#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <sys/stat.h>

#include "emmcparser.h"
#include "emmcxls.h"
#include "common.h"
#include "xlsxwriter.h"
#include "glib.h"

static int classify_req(struct mmc_parser *parser, void *arg);
static int calc_max_delay(struct mmc_parser *parser, void *arg);
static int calc_cmd_counts(struct mmc_parser *parser, void *arg);
static int calc_idle_time(struct mmc_parser *parser, void *arg);
mmc_req_cb cbs[] = {
	{"classify request to different lists", NULL, classify_req, NULL, NULL},
	{"calc read/write max latency/busy time", NULL, calc_max_delay, NULL, NULL},
	{"calc cmd counts", NULL, calc_cmd_counts, NULL, NULL},
	{"calc request's idle time/interval time of previous request", NULL, calc_idle_time, NULL, NULL},
};

mmc_req_cb *alloc_req_cb(char *desc, int (* init)(struct mmc_parser *parser, void *arg), 
											int (* func)(struct mmc_parser *parser, void *arg),
											int (* destroy)(struct mmc_parser *parser, void *arg),
											void *arg)
{
	mmc_req_cb *cb = calloc(1, sizeof(mmc_req_cb));
	if (cb == NULL) {
		perror("alloc mmc request cb failed");
		goto fail;
	}

	//INIT_LIST_HEAD(&cb->cb_node);

	cb->desc = desc;
	cb->init = init;
	cb->func = func;
	cb->destroy = destroy;
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
	if (parser->cur_req->cmd) {
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
	}
	return 0;
}

static int calc_max_delay(struct mmc_parser *parser, void *arg)
{
	int index;

	if (parser->cur_req->cmd && ((parser->has_busy && is_wr_cmd(parser->cur_req->cmd->cmd_index)) || 
		(parser->has_data && is_rd_cmd(parser->cur_req->cmd->cmd_index)))) {
		if (parser->cur_req->delay!=NULL) {
			index = find_maximum(parser->cur_req->delay, parser->cur_req->sectors);
			parser->cur_req->max_delay = parser->cur_req->delay[index];
			dbg(L_DEBUG, "max delay:%d\n", parser->cur_req->max_delay);
		}
	}

	return 0;
}

static int calc_cmd_counts(struct mmc_parser *parser, void *arg)
{
	if (parser->cur_req->cmd) {
		int cmd_index = parser->cur_req->cmd->cmd_index;

		parser->stats.cmds_dist[cmd_index]++;
	}

	if (parser->cur_req->sbc)
		parser->stats.cmds_dist[TYPE_23]++;

	if (parser->cur_req->stop)
		parser->stats.cmds_dist[TYPE_12]++;

	return 0;
}

static int calc_idle_time(struct mmc_parser *parser, void *arg)
{
	if (parser->has_busy && parser->has_data) {
		if (parser->cur_req->sbc) {
			parser->cur_req->idle_time = parser->cur_req->sbc->time.interval_us;
		} else if (parser->cur_req->cmd) {
			parser->cur_req->idle_time = parser->cur_req->cmd->time.interval_us;
		} else if (parser->cur_req->stop) {
			parser->cur_req->idle_time = parser->cur_req->stop->time.interval_us;
		} else {
			error("calc_idle_time cur req is wrong!\n");
		}
	}

	return 0;
}

int mmc_register_req_cb(mmc_parser *parser, mmc_req_cb *cb)
{
	int ret = 0;
	if (cb->init) {
		ret = cb->init(parser, cb->arg);

		if (ret!=0) {
			error("mmc req callback init failed!\n");
			return ret;
		}
	}

	parser->req_cb_list = g_slist_append(parser->req_cb_list, cb);
	//list_add_tail(&cb->cb_node, &parser->cb_list);
	return 0;
}

void destroy_req_cb(mmc_parser *parser, gpointer data)
{
	mmc_req_cb *cb = data;
	if (cb != NULL) {
		if (cb->destroy)
			cb->destroy(parser, cb->arg);
		free(cb);
	}
}

void mmc_destroy_req_cb_list(mmc_parser *parser, GSList *list)
{
	//g_slist_free_full(list, (GDestroyNotify)destroy_req_cb);
	mmc_req_cb *cb;
	GSList *iterator;

	for (iterator = parser->req_cb_list; iterator &&(iterator->next != iterator) ; iterator = iterator->next) {
		cb = (mmc_req_cb *)iterator->data;
		if (cb->destroy)
			cb->destroy(parser, cb->arg);
		//iterator = g_slist_remove(iterator, cb);
		//free(cb);
	}
	g_slist_free(parser->req_cb_list);

	parser->req_cb_list = NULL;
}

int mmc_cb_init(mmc_parser *parser)
{
	int ret = 0;
	int i;

	for (i = 0; i < NELEMS(cbs); i++) {
		//INIT_LIST_HEAD(&cbs[i].cb_node);
		ret = mmc_register_req_cb(parser, &cbs[i]);

		if (ret) {
			return ret;
		}
	}

	return 0;
}

/*======================XLS and Charts Related Functions===========================*/
xls_sheet_config *alloc_sheet_config()
{
	xls_sheet_config *config = calloc(1, sizeof(xls_sheet_config));
	if (config == NULL) {
		perror("alloc mmc xls sheet config failed");
		goto fail;
	}

	return config;

fail:
	return NULL;
}

void destroy_sheet_config(gpointer data)
{
	xls_sheet_config *config = data;
	if (config) {
		free(config);
	}
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
	if (cb) {
		if (cb->config->sheets)
			g_slist_free_full(cb->config->sheets, (GDestroyNotify)destroy_sheet_config);

		if (cb->config) {
			free(cb->config->filename);
			free(cb->config);
		}
		free(cb);
	}
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

int mmc_xls_init(mmc_parser *parser, char *csvpath)
{
	int ret = 0;

	char *dir_name = "out";

	if (access(dir_name, F_OK|R_OK|W_OK|X_OK)) {
		ret = mkdir(dir_name, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
		if (ret) {
			perror("mkdir dir failed");
			dir_name = NULL;
		}
	}

	mmc_xls_init_rw_dist(parser, csvpath, dir_name);
	mmc_xls_init_cmd_dist(parser, csvpath, dir_name);
	mmc_xls_init_sc_dist(parser, csvpath, dir_name);
	mmc_xls_init_addr_dist(parser, csvpath, dir_name);
	mmc_xls_init_idle_dist(parser, csvpath, dir_name);
	mmc_xls_init_seq_throughput(parser, csvpath, dir_name);
	
	return ret;
}

int create_chart(lxw_workbook *workbook, lxw_worksheet *worksheet, xls_sheet_config *config)
{
	//create chart
    lxw_chart *chart = workbook_add_chart(workbook, config->chart_type);
    lxw_chart_series *series = chart_add_series(chart, NULL, NULL);

    chart_series_set_categories(series, config->sheet_name, config->x1_area.first_row, config->x1_area.first_col, config->x1_area.last_row, config->x1_area.last_col);
    chart_series_set_values(series, config->sheet_name, config->y1_area.first_row, config->y1_area.first_col, config->y1_area.last_row, config->y1_area.last_col);
    chart_series_set_name(series, config->serie_name);
    chart_series_set_marker_type(series, LXW_CHART_MARKER_CIRCLE);
    chart_series_set_marker_size(series, 1);

    if (config->serie2_name) {
    	lxw_chart_series *series2 = chart_add_series(chart, NULL, NULL);
    	chart_series_set_categories(series2, config->sheet_name, config->x1_area.first_row, config->x1_area.first_col, config->x1_area.last_row, config->x1_area.last_col);
    	chart_series_set_values(series2, config->sheet_name, config->y2_area.first_row, config->y2_area.first_col, config->y2_area.last_row, config->y2_area.last_col);
    	chart_series_set_name(series2, config->serie2_name);
    }

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

int generate_xls(mmc_parser *parser)
{
	GSList *xls_list = parser->xls_list;
	GSList *iterator = NULL;

	for (iterator = xls_list; iterator; iterator = iterator->next) {
		mmc_xls_cb *xls_cb = (mmc_xls_cb *)iterator->data;
		
		dbg(L_DEBUG, "\ngenerating excel for [%s]\n", xls_cb->desc);

		lxw_workbook *workbook  = workbook_new(xls_cb->config->filename);

		GSList *sheet_list = xls_cb->config->sheets;
		GSList *sheet = NULL;
		//worksheet loop
		for (sheet = sheet_list; sheet; sheet = sheet->next) {
			xls_sheet_config *sheet_config = (xls_sheet_config *)sheet->data;

		    lxw_worksheet *worksheet = workbook_add_worksheet(workbook, sheet_config->sheet_name);
		    //prepare data
		   	void *data = xls_cb->prep_data(parser, sheet_config);
		    //write data to sheet
		    xls_cb->write_data(workbook, worksheet, data, sheet_config);
		    //create charts
		    xls_cb->create_chart(workbook, worksheet, sheet_config);

		    xls_cb->release_data(data);
		}

	    workbook_close(workbook);
	}

	return 0;
}