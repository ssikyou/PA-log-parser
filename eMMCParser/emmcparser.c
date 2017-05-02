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

extern event_parse_template events[];
struct list_head g_requests;

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

static mmc_request *alloc_req(unsigned int sectors)
{
	mmc_request *req = calloc(1, sizeof(mmc_request));
	if (req == NULL) {
		perror("alloc mmc request failed");
		goto fail;
	}

	//alloc data buf
	if (sectors > 0) {
		req->data = calloc(1, sectors*512);
		if (req->data == NULL) {
			perror("alloc mmc request data failed");
			goto fail_data;
		}

		req->delay = calloc(1, sectors*sizeof(int));
		if (req->delay == NULL) {
			perror("alloc mmc request delay failed");
			goto fail_delay;
		}
	}	

	return req;

fail_delay:
	free(req->data);
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
		if (req->data)
			free(req->data);

		free(req);
	}
}

static int is_cmd(unsigned char event_type)
{
	int ret = 0;

	if (event_type>=0 && event_type < TYPE_WR)
		ret = 1;

	return ret;
}

static int is_wr_cmd(unsigned char event_type)
{
	int ret = 0;

	if (event_type == TYPE_25 || event_type == TYPE_24)
		ret = 1;

	return ret;
}

static int is_rd_cmd(unsigned char event_type)
{
	int ret = 0;

	if (event_type == TYPE_17 || event_type == TYPE_18)
		ret = 1;

	return ret;
}

mmc_parser *mmc_parser_init(int has_data, int has_busy)
{
	mmc_parser *parser = calloc(1, sizeof(mmc_parser));
	if (parser == NULL) {
		perror("init mmc parser failed");
		goto fail;
	}

	INIT_LIST_HEAD(&g_requests);

	parser->has_data = has_data;
	parser->has_busy = has_busy;

	return parser;

fail:
	return NULL;
}

void mmc_parser_destroy(mmc_parser *parser)
{
	mmc_request *req, *tmp;

	list_for_each_entry_safe(req, tmp, &g_requests, req_node) {
		list_del(&req->req_node);
		destroy_req(req);
	}

	free(parser);
}

void dump_req_list(struct list_head *list)
{
	mmc_request *req;

	list_for_each_entry(req, list, req_node) {

		dump_req(req);
	}

}

void dump_cmd(mmc_cmd *cmd)
{
	dbg(L_DEBUG, "\tcmd index:%d arg:0x%x sendtime:%dus interval:%dus\n", 
		cmd->cmd_index, cmd->arg, cmd->time.time_us, cmd->time.interval_us);
	if (cmd->resp_type == RESP_R1)
		dbg(L_DEBUG, "\t\tresp type R1, value:0x%x\n", cmd->resp.r1);
	else if (cmd->resp_type == RESP_R1B)
		dbg(L_DEBUG, "\t\tresp type R1B, value:0x%x\n", cmd->resp.r1b);
	else if (cmd->resp_type == RESP_R2)
		dbg(L_DEBUG, "\t\tresp type R2, value:0x%x %x %x %x\n", cmd->resp.r2[0], cmd->resp.r2[1], cmd->resp.r2[2], cmd->resp.r2[3]);
	else if (cmd->resp_type == RESP_R3)
		dbg(L_DEBUG, "\t\tresp type R3, value:0x%x\n", cmd->resp.r3);
	else
		dbg(L_DEBUG, "\t\tunknow resp type\n");

}

void dump_req(mmc_request *req)
{
	int i;
	dbg(L_DEBUG, "request total time:%dus\n", req->total_time);
	if (is_wr_cmd(req->cmd->cmd_index) || is_rd_cmd(req->cmd->cmd_index)) {

		if (req->sbc)
			dump_cmd(req->sbc);

		assert(req->cmd!=NULL); 
		dump_cmd(req->cmd);

		if (req->stop)
			dump_cmd(req->stop);

		//dump delay
		dbg(L_DEBUG, "\tbusy or nac:\n");
		for (i=0; i<req->sectors; i++) {
			dbg(L_DEBUG, "\t\t%d", req->delay[i]);
		}
		dbg(L_DEBUG, "\n");
		//dump data?
	} else {

		assert(req->cmd!=NULL);
		dump_cmd(req->cmd);

		if (req->stop)
			dump_cmd(req->stop);

		if (req->cmd->cmd_index == TYPE_6) {
			dbg(L_DEBUG, "\tbusy: %d\n", (int)(req->delay));
		}

	}

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
	req = alloc_req(sectors);
	req->sectors = sectors;

	if (is_sbc)
		req->sbc = cmd;
	else
		req->cmd = cmd;

	parser->cur_req = req;
	parser->req_type = req_type;
	list_add_tail(&req->req_node, &g_requests);

	return req;
}

static void end_request(mmc_parser *parser)
{
	dbg(L_DEBUG, "\n====end of current request!====\n");
	parser->prev_req = parser->cur_req;
	parser->cur_req = NULL;
	clear_parser_status(parser);
}

static void calc_req_total_time(mmc_parser *parser, unsigned int end_time_us)
{
	parser->cur_req->total_time = end_time_us - parser->cur_req->cmd->time.time_us;
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
#if 1
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
	dbg(L_INFO, "event:%s\n", ept->event_string);

	event_type = ept->event_type;

	if (is_cmd(event_type)) {
		cmd = begin_cmd(parser, event_type, &time);
		ept->parse_data(rowFields[COL_DATA], &cmd->arg);
	}

	switch (event_type) {
		case TYPE_6:
			begin_request(parser, 0, cmd, REQ_NORMAL, 0);
		break;

		case TYPE_12:
			parser->cur_req->stop = cmd;	
		break;

		case TYPE_13:
			begin_request(parser, 0, cmd, REQ_NORMAL, 0);
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
				// not enough data, do not parse
				dbg(L_DEBUG, "no enough data, do not parse\n");
			} else {
				ept->parse_data(rowFields[COL_DATA], (unsigned char*)parser->cur_req->data + parser->cur_req->len);
				parser->cur_req->len += 512;
			}

			parser->trans_cnt++;

			//has data, no busy
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
				//check cur quest end here? has busy 
				if (parser->trans_cnt == parser->cur_req->sectors && 
					(parser->cur_req->sbc || parser->cur_req->cmd->cmd_index==TYPE_24)) {
					calc_req_total_time(parser, time.time_us);

					end_request(parser);
				} else if (parser->cur_req->stop) {	//receive cmd12's busy end
					parser->cur_req->delay[parser->trans_cnt-1] += busy_time;
					
					calc_req_total_time(parser, time.time_us);

					end_request(parser);
				}

			} else if (parser->req_type == REQ_NORMAL) {	//such as cmd6
				unsigned int busy_time;
				ept->parse_info(rowFields[COL_INFO], &busy_time);
				parser->cur_req->delay = busy_time;		//store busy time in pointer
				//calc total time here
				calc_req_total_time(parser, time.time_us);

				end_request(parser);
			}
		}
		break;

		case TYPE_RD:
		{
			//parse data to req's data area
			if (strlen(rowFields[COL_DATA])/2 < 512) {
				// not enough data, do not parse
				dbg(L_DEBUG, "no enough data, do not parse\n");
			} else {
				ept->parse_data(rowFields[COL_DATA], (unsigned char*)parser->cur_req->data + parser->cur_req->len);
				parser->cur_req->len += 512;
			}

			parser->trans_cnt++;

			/*
				parse read delay time, for read the delay time is in data part
			*/
			if (parser->req_type == REQ_RD) {
				unsigned int wait_time;
				ept->parse_info(rowFields[COL_INFO], &wait_time);
				
				parser->cur_req->delay[parser->trans_cnt-1] = wait_time;

				dbg(L_DEBUG, "cur trans count:%d, all sc:%d\n", parser->trans_cnt, parser->cur_req->sectors);
				//check cur quest end here? has busy 
				if (parser->trans_cnt == parser->cur_req->sectors && 
					(parser->cur_req->sbc || parser->cur_req->cmd->cmd_index==TYPE_17)) {
					calc_req_total_time(parser, time.time_us);

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
				case TYPE_13:
				{
					calc_req_total_time(parser, time.time_us);
					end_request(parser);
				}
				break;

				case TYPE_25:
					//no data, no busy
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
				//openended read
				if (parser->req_type == REQ_RD) {
					calc_req_total_time(parser, time.time_us);

					end_request(parser);
				}
				break;

				case TYPE_6:
					if (!has_busy) {
						end_request(parser);
					}
				break;
			}

			end_cmd(parser);
		break;

		default:
			error("unimplemented event type...\n");
		break;
	}

#endif


	//destroy_mmc_row(row);

	return 0;
}




int mmc_row_parse2(mmc_parser *parser, const char **rowFields, int fieldsNum)
{
	int ret = 0;
	mmc_row *row;
	mmc_request *req;
	mmc_cmd *cmd;
	event_parse_template *ept = NULL;
	int event_type;
	int i;

	//alloc mmc_row
	row = alloc_mmc_row();
	if (row == NULL) {

	}

	//parse event id
	ret = parse_event_id(rowFields[COL_ID], &row->event_id);

	//parse time
	//ret = parse_event_time(rowFields[COL_TIME], &row->time);

	//parse_cmd_args(rowFields[COL_DATA], &row->event_data.arg);
#if 0
	//search events array to find the event template
	for (i = 0; i < NELEMS(events); i++) {
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

	event_type = ept->event_type;


	if (is_cmd(event_type)) {
		cmd = alloc_cmd();
		//fill cmd
		cmd->cmd_index = event_type;
		ept->parse_data(rowFields[COL_DATA], &cmd->arg);
		parse_event_time(rowFields[COL_TIME], &cmd->time);
		parser->cur_cmd = cmd;
	}

#endif

#if 0

	while () 
	{
		switch (parser->state) {
			case INIT:

			if (event_type == TYPE_23) {
				//fill req
				req = alloc_req();
				req->sbc = cmd;
				req->sectors = cmd->arg;
				parser->cur_req = req;
				parser->use_sbc = 1;
				parser->state = STATE_SBC_WR;
			}


			
			break;

			case PREP:
				
				if r1
				get resp type, set to cur Cmd

				if cmd25 && using sbc
				parser change cur Cmd to cmd25
				transit to MULTI_WR

				if cmd18 && using sbc
				parser change cur Cmd to cmd18
				transit to MULTI_RD
				
			break;

			case NORMAL:


			break;


			case STATE_SBC_WR:
				
			if r1
			if (parser->cur_cmd == NULL) {
				error("received response but no cmd parsed, by pass...\n");
				return -1;
			}

			//fill in cur cmd's resp
			parser->cur_cmd->resp_type = RESP_R1;
			ept->parse_data(rowFields[COL_DATA], &parser->cur_cmd->resp.r1);


			if (parser->cur_cmd.cmd_index == TYPE_13) {
				EventTime time;
				parse_event_time(rowFields[COL_TIME], &time);
				//check status, ready or other?
				if ready {
					//calculate prev req's total time
					if (parser->use_sbc)
					parser->prev_req.total_time = time.time_us -time.interval_us - parser->prev_req->cmd.time.time_us;

					//end of cur and prev request, clear parser status
					parser->prev_req = parser->cur_req;
					parser->cur_req = NULL;
	

				} else {
	
				}
			}


			parser->prev_cmd = parser->cur_cmd;
			parser->cur_cmd = NULL;




				if r1b, cur cmd must be cmd12
				
				if cmd25 && using sbc
				parser change cur Cmd to cmd25

				if "Write"
				parse data, parse interval time

				if cmd12
				parser change cur Cmd to cmd12

				if cmd13
				calc total time, set to cur req



				
			break;

		}
	}
#endif

	return 0;

}