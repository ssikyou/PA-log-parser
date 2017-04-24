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

#include "emmcparser.h"
#include "common.h"

struct list_head g_requests;


int parse_cmd_args(void *data, unsigned int *out)
{
	char tmp[50];
	memset(tmp, '\0', 50);
	char *str = tmp;
	memcpy(str, data, strlen((char *)data));

	char *arg_part = strsep(&str, " ");
	strsep(&arg_part, ":");
	char *arg_value = arg_part;

	dbg(L_DEBUG, "arg_value str:%s\n", arg_value);
	unsigned int v = strtoul(arg_value, NULL, 16);
	dbg(L_DEBUG, "v:%d\n", v);

	*out = v;
	return 0;
}

int parse_resp_r1(void *data, unsigned int *out)
{
	char tmp[50];
	memset(tmp, '\0', 50);
	char *str = tmp;
	memcpy(str, data, strlen((char *)data));

	char *resp_part = strsep(&str, " ");
	strsep(&resp_part, ":");
	char *resp_value = resp_part;

	dbg(L_DEBUG, "resp_value str:%s\n", resp_value);
	unsigned long long int v = strtoull(resp_value, NULL, 16);
	unsigned int status = v >> 8 & 0xffffffff;
	dbg(L_DEBUG, "status:0x%x\n", status);

	*out = status;
	return 0;
}

//assue string length is 1024 that is 512 bytes, a block size
int parse_rw_data(void *data, void *out)
{
	//convert two chars to a byte

	return 0;
}

int parse_wr_busy(void *data, unsigned int *out)
{
	char tmp[30];
	memset(tmp, '\0', 30);
	char *str = tmp;
	memcpy(str, data, strlen((char *)data));

	strsep(&str, " ");	//"BUSY" tag
	char *busy_time = strsep(&str, " ");	//unit is us

	dbg(L_DEBUG, "busy_time str:%s\n", busy_time);
	unsigned int v = strtoul(busy_time, NULL, 0);
	dbg(L_DEBUG, "v:%d\n", v);

	*out = v;

	return 0;
}


event_parse_template events[] = {
	/*{TYPE_0, " CMD00(GO_IDLE_STATE)"},
	{TYPE_1, " CMD01(SEND_OP_COND)"},
	{TYPE_2, " CMD02(ALL_SEND_CID)"},
	*/
	{TYPE_13, " CMD13(SEND_STATUS)", parse_cmd_args, NULL, NULL, NULL},
	{TYPE_23, " CMD23(SET_BLOCK_COUNT)", parse_cmd_args, NULL, NULL, NULL},
	{TYPE_25, " CMD25(WRITE_MULTIPLE_BLOCK)", parse_cmd_args, NULL, NULL, NULL},
	{TYPE_R1, "  R1 ", parse_resp_r1, NULL, NULL, NULL},
	{TYPE_WR, "   Write", parse_rw_data, NULL, NULL, NULL},
	{TYPE_BUSY_START, "    BUSY START", NULL, NULL, NULL, NULL},
	{TYPE_BUSY_START, "    BUSY END", parse_wr_busy, NULL, NULL, NULL},


};


mmc_parser *mmc_parser_init()
{
	mmc_parser *parser = calloc(1, sizeof(mmc_parser));
	if (parser == NULL) {
		perror("init mmc parser failed");
	}

	INIT_LIST_HEAD(&g_requests);

	return parser;
}



int parse_event_id(void *data, unsigned int *out)
{
	*out = strtoul((char*)data, NULL, 0);
	dbg(L_DEBUG, "event id:%d\n", *out);

	return 0;
}

int parse_event_time(void *data, event_time *out)
{
	char tmp[30];
	memset(tmp, '\0', 30);
	char *str = tmp;
	memcpy(str, data, strlen((char *)data));
	//char *str = strdup((char *)data);
	//dbg(L_DEBUG, "str:%s\n", str);

	char *time_part = strsep(&str, " ");
	char *interval_part = strsep(&str, " ");
	char *interval_unit = skip_spaces(str);	//left part is interval unit

	char *tmp_s = strsep(&time_part, ":");
	char *tmp_ms = strsep(&time_part, ":");
	char *tmp_us = strsep(&time_part, ":");

	int s = strtol(tmp_s, NULL, 10);
	int ms = strtol(tmp_ms, NULL, 10);
	int us = strtol(tmp_us, NULL, 10);
	int interval = strtol(interval_part, NULL, 10);

	out->time_us = 1000000*s + 1000*ms + us;

	if (strcmp(interval_unit, "s")==0)
		out->interval_us = 1000000*interval;
	else if (strcmp(interval_unit, "ms")==0)
		out->interval_us = 1000*interval;
	else if (strcmp(interval_unit, "us")==0)
		out->interval_us = interval;

	dbg(L_DEBUG, "s:%d, ms:%d, us:%d interval:%d, time_us:%d, interval_us:%d\n", 
		s, ms, us, interval, out->time_us, out->interval_us);


	return 0;
}

static mmc_row *alloc_mmc_row()
{
	mmc_row *row = calloc(1, sizeof(mmc_row));
	if (row == NULL) {
		perror("alloc mmc row failed");
	}

	return row;
}

static mmc_cmd *alloc_cmd()
{
	mmc_cmd *cmd = calloc(1, sizeof(mmc_cmd));
	if (cmd == NULL) {
		perror("alloc mmc cmd failed");
	}

	return cmd;
}

static mmc_request *alloc_req(unsigned int sectors)
{
	mmc_request *req = calloc(1, sizeof(mmc_request));
	if (req == NULL) {
		perror("alloc mmc request failed");
		goto fail;
	}

	//alloc data buf
	req->data = calloc(1, sectors*512);
	if (req->data == NULL) {
		perror("alloc mmc request failed");
		goto fail_data;
	}	

	return req;

fail_data:
	free(req);
fail:
	return NULL;
}

static int is_cmd(unsigned char event_type)
{
	int ret = 0;

	if (event_type>=0 && event_type < TYPE_WR)
		ret = 1;

	return ret;
}


int mmc_row_parse(mmc_parser *parser, const char **rowFields, int fieldsNum)
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
#if 1
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

	switch (event_type) {
		case TYPE_23:

			//fill req
			req = alloc_req();
			req->sbc = cmd;
			req->sectors = cmd->arg;
			parser->cur_req = req;
			parser->use_sbc = 1;


		break;


		case TYPE_25:
			parser->dir = DIR_WR;
			if (parser->use_sbc) {
				req->cmd = cmd;
			} else {
				req = alloc_req();
				req->cmd = cmd;
			}

		break;

		case TYPE_WR:
			/*
				parse write busy time
			*/
			EventTime time;
			if (parser->wr_cnt > 0)
				parseEventTime(rowFields[COL_TIME], &time);

			parser->cur_req->delay[parser->wr_cnt-1] = time.interval_us;

			//parse data to req's data area
			if (strlen(rowFields[COL_DATA])/2 < 512) {
				// not enough data, do not parse
			} else {
				ept->parse_data(rowFields[COL_DATA], (unsigned char*)parser->cur_req->data + parser->cur_req->len);
				parser->cur_req->len += 512;
			}


			parser->wr_cnt++;
		break;

		case TYPE_BUSY_START:

		break;

		case TYPE_BUSY_END:
			/*
				parse write busy time
			*/
			unsigned int busy_time;
			ept->parse_data(COL_INFO, &busy_time);
			parser->cur_req->delay[parser->wr_cnt-1] = busy_time;

			//check cur quest end here?
		break;

		case TYPE_13:
			req = alloc_req();
			req->cmd = cmd;

		break;

#if 0
		case TYPE_12:
			//
			if (parser->use_sbc) {
				req->stop = cmd;
			}
		break;
#endif
		case TYPE_R1:

			if (parser->cur_cmd == NULL) {
				error("received response but no cmd parsed, by pass...\n");
				return -1;
			}

			//fill in cur cmd's resp
			parser->cur_cmd->resp_type = RESP_R1;
			ept->parse_data(rowFields[COL_DATA], &parser->cur_cmd->resp.r1);

#if 0
			if (parser->cur_cmd.cmd_index == TYPE_13) {
				EventTime time;
				parseEventTime(rowFields[COL_TIME], &time);
				//check status, ready or other?
				if ready {
					//calculate prev req's total time
					
					parser->prev_req.total_time = time.time_us - parser->prev_req->cmd.time.time_us;

					//end of cur and prev request, clear parser status
					parser->prev_req = parser->cur_req;
					parser->cur_req = NULL;
					...

				} else {
					//if busy, calc the last delay time, add cur cmd interval time and cur resp interval time
					if write
					parser->cur_req->delay[parser->wr_cnt-1] += parser->cur_cmd.time.interval_us + time.interval_us;	
				}
			}
#endif

			parser->prev_cmd = parser->cur_cmd;
			parser->cur_cmd = NULL;
		break;
#if 0
		case TYPE_R1B:
			//fill in cur cmd's resp
			parser->cur_cmd.resp_type = R1B;
			ept->parseData(rowFields[COL_DATA], &parser->cur_cmd.resp.R1);

			if (parser->cur_cmd.cmd_index == TYPE_12) {
				EventTime time;
				parseEventTime(rowFields[COL_TIME], &time);

				//calc the last delay time, add cur cmd interval time and cur resp interval time
				if write
					parser->cur_req->delay[parser->wr_cnt-1] += parser->cur_cmd.time.interval_us + time.interval_us;

				//end of request, clear parser status
				parser->prev_req = parser->cur_req;
				parser->cur_req = NULL;
			}

			parser->prev_cmd = parser->cur_cmd;
			parser->cur_cmd = NULL;
		break;

		case TYPE_6:

		break;

#endif
	}

#endif





	return 0;


}











/*

	while () 
	{
		switch (state) {
			case INIT:
			/*
			if cmd23
			parse arg
			alloc request
			set parser state, using sbc
			set parser state, current Cmd, current req
			transit to PREP



			if cmd25 and not using sbc
			parse arg
			alloc request
			set parser state, current Cmd, current req
			transit to MULTI_WR
			



			if cmd 18, transit to MULTI_RD
			....


			
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

			case SINGLE_RD:
			break;
			case MULTI_RD:
			break;
			case SINGLE_WR:
			break;
			case MULTI_WR:
				/*
				if r1
				get resp type, set to cur Cmd
					if cur cmd is 13, check status. 
						if not busy, transit to DEFAULT
						if busy, go on.

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
*/
