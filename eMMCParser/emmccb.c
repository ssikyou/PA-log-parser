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

static int calc_max_busy(struct mmc_parser *parser, void *arg);
mmc_req_cb cbs[] = {
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

static int calc_max_busy(struct mmc_parser *parser, void *arg)
{

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

void prep_data(mmc_parser *parser, int steps)
{
	int i;
	int max_busy_time = 0;
	//for cmd25 list
	//get max busy time of all req

	/*
	 * calc x axis value nums, e.g. max busy is 200us, steps is 20us
	 * the x axis is [0~20), [20~40), ..., 180~200, [200~220)us
	 */
	int nums = max_busy_time/steps+1;
	for (i = 0; i < nums; i++) {
		

	}

	//for cmd25 list
		//index = req->max_busy/steps+1;
		//get idx from xls_data_entry list
		// if not exist
			//alloc xls_data_entry
			//e->idx=index;
			//e->idx_desc = "[%s~%s)", index*steps, (index+1)*steps
			//e->val=1
			//insert to sorted list
		// else
		//e->val+=1;

}

int generate_xls(mmc_parser *parser)
{

	return 0;
}