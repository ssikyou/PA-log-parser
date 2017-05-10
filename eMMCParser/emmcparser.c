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
#include "glib.h"

extern event_parse_template events[];

static mmc_row *alloc_mmc_row()
{
	mmc_row *row = calloc(1, sizeof(mmc_row));
	if (row == NULL) {
		perror("alloc mmc row failed");
	}

	return row;
}

static void destroy_mmc_row(mmc_row *row)
{
	if (row)
		free(row);
}

static mmc_cmd *alloc_cmd()
{
	mmc_cmd *cmd = calloc(1, sizeof(mmc_cmd));
	if (cmd == NULL) {
		perror("alloc mmc cmd failed");
	}

	return cmd;
}

static void destroy_cmd(mmc_cmd *cmd)
{
	if (cmd)
		free(cmd);
}

static mmc_request *alloc_req(mmc_parser *parser, unsigned int sectors)
{
	mmc_request *req = calloc(1, sizeof(mmc_request));
	if (req == NULL) {
		perror("alloc mmc request failed");
		goto fail;
	}

	//alloc data buf
	if (sectors > 0) {
		/*
		req->data = calloc(1, sectors*512);
		if (req->data == NULL) {
			perror("alloc mmc request data failed");
			goto fail_data;
		}*/
		memset(parser->data, 0, DATA_SIZE);
		req->data = parser->data;

		req->delay = calloc(1, sectors*sizeof(int));
		if (req->delay == NULL) {
			perror("alloc mmc request delay failed");
			goto fail_delay;
		}
	}	

	return req;

fail_delay:
	//free(req->data);
fail_data:
	free(req);
fail:
	return NULL;
}

static void destroy_req(mmc_request *req)
{
	if(req) {
		if (req->sbc)
			destroy_cmd(req->sbc);
		if (req->cmd)
			destroy_cmd(req->cmd);
		if (req->stop)
			destroy_cmd(req->stop);
		if (req->sectors && req->delay)
			free(req->delay);
		//if (req->data)
		//	free(req->data);

		free(req);
	}
}

static mmc_pending_info *alloc_pending()
{
	mmc_pending_info *pending = calloc(1, sizeof(mmc_pending_info));
	if (pending == NULL) {
		perror("alloc mmc pending failed");
	}

	return pending;
}

static void destroy_pending(mmc_pending_info *pending)
{
	if(pending) {
		/*
		if (pending->cmd)
			destroy_cmd(pending->cmd);

		if (pending->req)
			destroy_req(pending->req);
		*/
		free(pending);
		pending = NULL;
	}
}

mmc_parser *mmc_parser_init(int has_data, int has_busy, char *config_file)
{
	mmc_parser *parser = calloc(1, sizeof(mmc_parser));
	if (parser == NULL) {
		perror("init mmc parser failed");
		goto fail;
	}

	parser->data = calloc(1, DATA_SIZE);	//for one request data storage
	if (parser->data == NULL) {
		perror("alloc data failed");
		goto fail_data;
	}

	GKeyFile* gkf = g_key_file_new();
	if (!g_key_file_load_from_file(gkf, config_file, G_KEY_FILE_NONE, NULL)){
        perror("could not read config file");
        goto fail_config;
    }
    parser->gkf = gkf;

	INIT_LIST_HEAD(&parser->stats.requests_list);
	parser->stats.cmd25_list = NULL;
	parser->stats.cmd18_list = NULL;

	INIT_LIST_HEAD(&parser->cb_list);
	parser->xls_list = NULL;

	parser->has_data = has_data;
	parser->has_busy = has_busy;

	return parser;

fail_config:
	free(parser->data);
fail_data:
	free(parser);
fail:
	return NULL;
}

void mmc_parser_destroy(mmc_parser *parser)
{
	mmc_request *req, *tmp;

	list_for_each_entry_safe(req, tmp, &parser->stats.requests_list, req_node) {
		list_del(&req->req_node);
		destroy_req(req);
	}

	if (parser->stats.cmd25_list)
		g_slist_free(parser->stats.cmd25_list);

	if (parser->stats.cmd18_list)
		g_slist_free(parser->stats.cmd18_list);

	if (parser->xls_list)
		mmc_destroy_xls_list(parser->xls_list);

	if (parser->gkf)
		g_key_file_free (parser->gkf);

	free(parser->data);
	free(parser);
}

static void clear_parser_status(mmc_parser *parser)
{
	parser->state = STATE_NONE;
	parser->use_sbc = 0;
	parser->req_type = REQ_NONE;
	parser->trans_cnt = 0;
}

static mmc_cmd *begin_cmd(mmc_parser *parser, unsigned char event_type, event_time *time)
{
	mmc_cmd *cmd = NULL;
	cmd = alloc_cmd();
	//fill cmd
	cmd->cmd_index = event_type;
	cmd->resp_type = RESP_UND;
	//ept->parse_data(rowFields[COL_DATA], &cmd->arg);
	//parse_event_time(rowFields[COL_TIME], &cmd->time);
	cmd->time.time_us = time->time_us;
	cmd->time.interval_us = time->interval_us;
	parser->cur_cmd = cmd;

	return cmd;
}

static void end_cmd(mmc_parser *parser)
{
	parser->prev_cmd = parser->cur_cmd;
	parser->cur_cmd = NULL;
}

static mmc_request *begin_request(mmc_parser *parser, int is_sbc, mmc_cmd *cmd, int req_type, unsigned int sectors)
{
	mmc_request *req = NULL;
	req = alloc_req(parser, sectors);
	req->sectors = sectors;

	if (is_sbc)
		req->sbc = cmd;
	else
		req->cmd = cmd;

	parser->cur_req = req;
	parser->req_type = req_type;
	list_add_tail(&req->req_node, &parser->stats.requests_list);

	return req;
}

static void end_request(mmc_parser *parser)
{
	dbg(L_DEBUG, "\n====end of current request!====\n");
	mmc_req_cb *cb;
	//if (parser->cur_req->cmd->cmd_index==TYPE_18)
		//list_add_tail(&parser->cur_req->req_node, &parser->stats.cmd18_list);
	list_for_each_entry(cb, &parser->cb_list, cb_node) {
		dbg(L_DEBUG, "doing callback: %s\n", cb->desc);
		cb->func(parser, cb->arg);
	}

	parser->prev_req = parser->cur_req;
	parser->cur_req = NULL;
	clear_parser_status(parser);
}

//current used for cmd13
static void begin_pending(mmc_parser *parser, mmc_cmd *cmd, int req_type)
{
	dbg(L_DEBUG, "\n====start pending!====\n");
	parser->pending = alloc_pending();
	mmc_request *pending_req = alloc_req(parser, 0);
				
	parser->pending->cmd = cmd;
	parser->pending->req = pending_req;
	parser->pending->req->cmd = cmd;
	parser->pending->req_type = req_type;
}

static void end_pending(mmc_parser *parser)
{
	dbg(L_DEBUG, "\n====end pending!====\n");
	//set pending req to cur req
	parser->cur_cmd = parser->pending->cmd;
	parser->cur_req = parser->pending->req;
	parser->req_type = parser->pending->req_type;
	list_add_tail(&parser->cur_req->req_node, &parser->stats.requests_list);
	//clean pending info
	//parser->pending->cmd = NULL;
	//parser->pending->req = NULL;
	parser->pending->req_type = REQ_NONE;
	destroy_pending(parser->pending);
}

static void calc_req_total_time(mmc_request *req, unsigned int end_time_us)
{
	assert(req->cmd!=NULL);
	req->total_time = end_time_us - req->cmd->time.time_us;
}

int mmc_row_parse(mmc_parser *parser, const char **rowFields, int fieldsNum)
{
	int ret = 0;
	mmc_row *row = NULL;
	mmc_request *req = NULL;
	mmc_cmd *cmd = NULL;
	event_parse_template *ept = NULL;
	unsigned int event_id;
	event_time time;
	int event_type;
	int i;
	unsigned int sectors;
	int has_busy = parser->has_busy;
	int has_data = parser->has_data;

	if (rowFields==NULL || rowFields[COL_ID]==NULL || strcmp(rowFields[COL_ID], "")==0) {
		error("line is empty, skip...\n");
		return 0;
	}

#if 0
	//alloc mmc_row
	row = alloc_mmc_row();
	if (row == NULL) {

	}
#endif
	dbg(L_DEBUG, "\n");

	//parse event id
	ret = parse_event_id(rowFields[COL_ID], &event_id);

	//parse time
	ret = parse_event_time(rowFields[COL_TIME], &time);

	//parse_cmd_args(rowFields[COL_DATA], &row->event_data.arg);

	//search events array to find the event template
	for (i = 0; i < get_temp_nums()/*NELEMS(events)*/; i++) {
		if (strncmp(events[i].event_string,
				rowFields[COL_TYPE], strlen(events[i].event_string)))
			continue;

		ept = &events[i];
		break;
	}

	if (ept == NULL) {
		error("do not find the event template for %s!\n", rowFields[COL_TYPE]);
		return -1;
	}
	dbg(L_INFO, "[%d]event:%s\n", event_id, ept->event_string);

	event_type = ept->event_type;

	if (is_cmd(event_type)) {
		cmd = begin_cmd(parser, event_type, &time);
		ept->parse_data(rowFields[COL_DATA], &cmd->arg);
	}

	switch (event_type) {
		case TYPE_0:
		case TYPE_5:
		case TYPE_55:
		/*
			if (parser->prev_req && parser->prev_req->cmd->cmd_index == TYPE_0) {
				calc_req_total_time(parser->prev_req, time.time_us);
				//dispatch pre request
			}*/

			begin_request(parser, 0, cmd, REQ_NORMAL, 0);
			parser->cur_cmd->resp_type = RESP_UND;
			end_request(parser);
			end_cmd(parser);
		break;

		case TYPE_1:
		/*
			//last cmd0
			if (parser->prev_req && parser->prev_req->cmd->cmd_index == TYPE_0) {
				calc_req_total_time(parser->prev_req, time.time_us);
				//dispatch pre request
			}
			*/

			begin_request(parser, 0, cmd, REQ_NORMAL, 0);
		break;

		case TYPE_2:
		case TYPE_3:
		case TYPE_7:
		case TYPE_9:
		case TYPE_10:
		case TYPE_16:
		case TYPE_35:
		case TYPE_36:
		case TYPE_38:
			begin_request(parser, 0, cmd, REQ_NORMAL, 0);
		break;

		case TYPE_8:
			begin_request(parser, 0, cmd, REQ_RD, 1);
		break;

		case TYPE_6:
			begin_request(parser, 0, cmd, REQ_NORMAL, 0);
		break;

		case TYPE_12:
			if (parser->cur_req)
				parser->cur_req->stop = cmd;
			else
				begin_request(parser, 0, cmd, REQ_NORMAL, 0);
		break;

		//cmd13 can be sent while the bus is still busy
		case TYPE_13:
			if (!parser->cur_req) {	//cur req is finished
				begin_request(parser, 0, cmd, REQ_NORMAL, 0);
			} else {	//cur req is not finished, is still busy, maybe cmd6 or write
				dbg(L_DEBUG, "cur req is not finished, store in pending!\n");
				begin_pending(parser, cmd, REQ_NORMAL);
			}
		break;

		case TYPE_17:
			begin_request(parser, 0, cmd, REQ_RD, 1);
		break;

		case TYPE_18:
			//pre-defined read
			if (parser->cur_req && parser->cur_req->sbc) {
				dbg(L_DEBUG, "sbc read!\n");
				parser->req_type = REQ_RD;
				parser->cur_req->cmd = cmd;
			} else {
				//open-ended read
				ept->parse_info(rowFields[COL_INFO], &sectors);

				begin_request(parser, 0, cmd, REQ_RD, sectors);

			}

		break;

		case TYPE_23:

			sectors = cmd->arg & 0xffff;
			//sbc request
			begin_request(parser, 1, cmd, REQ_NONE, sectors);

		break;

		case TYPE_24:
			begin_request(parser, 0, cmd, REQ_WR, 1);
		break;

		case TYPE_25:
			//pre-defined write
			if (parser->cur_req && parser->cur_req->sbc) {
				dbg(L_DEBUG, "sbc write!\n");
				parser->req_type = REQ_WR;
				parser->cur_req->cmd = cmd;
			} else {
				//open-ended write
				ept->parse_info(rowFields[COL_INFO], &sectors);
				begin_request(parser, 0, cmd, REQ_WR, sectors);
			}

		break;

		case TYPE_WR:
			//parse data to req's data area
			if (strlen(rowFields[COL_DATA])/2 < 512) {
				// not enough data
				dbg(L_DEBUG, "no enough data\n");
			}

			parser->cur_req->len_per_trans = ept->parse_data(rowFields[COL_DATA], (unsigned char*)parser->cur_req->data + parser->cur_req->len);
			parser->cur_req->len += parser->cur_req->len_per_trans;
			dbg(L_DEBUG, "len_per_trans:%d, trans len:%d\n", parser->cur_req->len_per_trans, parser->cur_req->len);

			parser->trans_cnt++;

			//sbc write, has data, no busy
			if (parser->trans_cnt == parser->cur_req->sectors && !has_busy && 
				(parser->cur_req->sbc || parser->cur_req->cmd->cmd_index==TYPE_24)) {
				end_request(parser);
			}
		break;

		case TYPE_BUSY_START:
			if (!has_data && is_wr_cmd(parser->cur_req->cmd->cmd_index)) {
				parser->trans_cnt++;
			}
		break;

		case TYPE_BUSY_END:
		{
			/*
				parse write busy time
			*/
			if (parser->req_type == REQ_WR) {
				unsigned int busy_time;
				ept->parse_info(rowFields[COL_INFO], &busy_time);
				//cmd12's busy time is added to the last write's busy time
				if (!parser->cur_req->stop)
					parser->cur_req->delay[parser->trans_cnt-1] = busy_time;

				dbg(L_DEBUG, "cur trans count:%d, all sc:%d\n", parser->trans_cnt, parser->cur_req->sectors);
				//sbc write, has busy 
				if (parser->trans_cnt == parser->cur_req->sectors && 
					(parser->cur_req->sbc || parser->cur_req->cmd->cmd_index==TYPE_24)) {
					calc_req_total_time(parser->cur_req, time.time_us);

					end_request(parser);
				} else if (parser->cur_req->stop) {	//openended write, has busy
					//receive cmd12's busy end
					parser->cur_req->delay[parser->trans_cnt-1] += busy_time;
					
					calc_req_total_time(parser->cur_req, time.time_us);

					end_request(parser);
				}

			} else if (parser->req_type == REQ_NORMAL) {	//such as cmd6
				unsigned int busy_time;
				ept->parse_info(rowFields[COL_INFO], &busy_time);
				parser->cur_req->delay = busy_time;		//store busy time in pointer
				//calc total time here
				calc_req_total_time(parser->cur_req, time.time_us);

				end_request(parser);

				if (parser->pending) {
					end_pending(parser);
				}
			}
		}
		break;

		case TYPE_RD:
		{
			if (parser->req_type == REQ_RD) {
				//parse data to req's data area
				if (strlen(rowFields[COL_DATA])/2 < 512) {
					// not enough data
					dbg(L_DEBUG, "no enough data\n");
				} 

				parser->cur_req->len_per_trans = ept->parse_data(rowFields[COL_DATA], (unsigned char*)parser->cur_req->data + parser->cur_req->len);
				parser->cur_req->len += parser->cur_req->len_per_trans;
				dbg(L_DEBUG, "len_per_trans:%d, trans len:%d\n", parser->cur_req->len_per_trans, parser->cur_req->len);

				parser->trans_cnt++;

				/*
					parse read delay time, for read the delay time is in data part
				*/
				
				unsigned int wait_time;
				ept->parse_info(rowFields[COL_INFO], &wait_time);
				
				parser->cur_req->delay[parser->trans_cnt-1] = wait_time;

				dbg(L_DEBUG, "cur trans count:%d, all sc:%d\n", parser->trans_cnt, parser->cur_req->sectors);
				//sbc read with data info
				if (parser->trans_cnt == parser->cur_req->sectors && 
					(parser->cur_req->sbc || parser->cur_req->cmd->cmd_index==TYPE_17 || parser->cur_req->cmd->cmd_index==TYPE_8)) {
					calc_req_total_time(parser->cur_req, time.time_us);

					end_request(parser);
				} 
			}

		}
		break;

		case TYPE_R1:

			if (parser->cur_cmd == NULL) {
				error("received response but no cmd parsed, by pass...\n");
				return -1;
			}

			//fill in cur cmd's resp
			parser->cur_cmd->resp_type = RESP_R1;
			ept->parse_data(rowFields[COL_DATA], &parser->cur_cmd->resp.r1);

			switch (parser->cur_cmd->cmd_index) {
				case TYPE_3:
				case TYPE_13:
				case TYPE_16:
				case TYPE_35:
				case TYPE_36:
				assert(parser->cur_req!=NULL);
					calc_req_total_time(parser->cur_req, time.time_us);
					end_request(parser);
				break;

				case TYPE_8:
				case TYPE_17:
					if (!has_data) {	//the total time is not accurate!
						calc_req_total_time(parser->cur_req, time.time_us);
						end_request(parser);
					}
				break;

				case TYPE_18:
					//sbc read but no data info
					if (parser->cur_req->sbc && !has_data) {	//the total time is not accurate!
						calc_req_total_time(parser->cur_req, time.time_us);
						end_request(parser);
					}

				break;

				case TYPE_24:
					//no data, no busy
					if (!has_busy && !has_data) {
						end_request(parser);
					}
				break;

				case TYPE_25:
					//sbc write, no data, no busy
					if (!has_busy && !has_data && parser->cur_req->sbc) {
						end_request(parser);
					}
				break;

			}

			end_cmd(parser);
		break;

		case TYPE_R1B:
			if (parser->cur_cmd == NULL) {
				error("received response but no cmd parsed, by pass...\n");
				return -1;
			}
			//fill in cur cmd's resp
			parser->cur_cmd->resp_type = RESP_R1B;
			ept->parse_data(rowFields[COL_DATA], &parser->cur_cmd->resp.r1b);

			switch (parser->cur_cmd->cmd_index) {
				case TYPE_12:
				//openended read, with or without data info 
				if (parser->req_type == REQ_RD) {
					calc_req_total_time(parser->cur_req, time.time_us);
					end_request(parser);
				} else if (parser->req_type == REQ_WR && !has_busy) {	//openended write, no busy, with or without data 
					calc_req_total_time(parser->cur_req, time.time_us);
					end_request(parser);
				}
				break;

				case TYPE_6:
					if (!has_busy) {
						end_request(parser);
					}
				break;

				case TYPE_38:	//for current, assume cmd38 has no busy info
				case TYPE_7:
					calc_req_total_time(parser->cur_req, time.time_us);
					end_request(parser);
				break;
			}

			end_cmd(parser);
		break;

		case TYPE_R2:
			//cmd2, cmd9, cmd10
			if (parser->cur_cmd == NULL) {
				error("received response but no cmd parsed, by pass...\n");
				return -1;
			}

			//fill in cur cmd's resp
			parser->cur_cmd->resp_type = RESP_R2;
			ept->parse_data(rowFields[COL_DATA], parser->cur_cmd->resp.r2);

			calc_req_total_time(parser->cur_req, time.time_us);
			end_request(parser);

			end_cmd(parser);
		break;

		case TYPE_R3:
			if (parser->cur_cmd == NULL) {
				error("received response but no cmd parsed, by pass...\n");
				return -1;
			}

			//fill in cur cmd's resp
			parser->cur_cmd->resp_type = RESP_R3;
			ept->parse_data(rowFields[COL_DATA], &parser->cur_cmd->resp.r3);

			calc_req_total_time(parser->cur_req, time.time_us);
			end_request(parser);

			end_cmd(parser);
		break;

		default:
			error("unimplemented event type, should not going here...\n");
			exit(EXIT_FAILURE);
		break;
	}

	//destroy_mmc_row(row);

	return 0;
}
