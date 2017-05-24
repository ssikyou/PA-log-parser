#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <assert.h>

#include "emmcparser.h"
#include "common.h"
#include "glib.h"

extern event_parse_template events[];

//FILE *g_log_file;

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
		if (req->sbc) {
			destroy_cmd(req->sbc);
			req->sbc = NULL;
		}
		if (req->cmd) {
			destroy_cmd(req->cmd);
			req->cmd = NULL;
		}
		if (req->stop) {
			destroy_cmd(req->stop);
			req->stop = NULL;
		}
		if (req->sectors && req->delay) {
			// if (req->cmd) {
			// 	error("[%d]\n", req->cmd->event_id);
			// 	fprintf(g_log_file, "[%d]\n", req->cmd->event_id);
			// }
			free(req->delay);
			req->delay = NULL;
		}
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

    // g_log_file = fopen("log.txt", "w+");
    // if (g_log_file < 0) {
    // 	perror("open log.txt failed");
    // 	exit(EXIT_FAILURE);
    // }
    //error("g_log_file: %d\n", g_log_fd);

    parser->pending_list = NULL;
	parser->stats.requests_list = NULL;
	parser->stats.cmd25_list = NULL;
	parser->stats.cmd18_list = NULL;

	parser->req_cb_list = NULL;
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
	mmc_request *req;
	GSList *iterator;
	for (iterator = parser->stats.requests_list; iterator; iterator = iterator->next) {
		req = (mmc_request *)iterator->data;
		destroy_req(req);
	}
	g_slist_free(parser->stats.requests_list);

	if (parser->stats.cmd25_list)
		g_slist_free(parser->stats.cmd25_list);

	if (parser->stats.cmd18_list)
		g_slist_free(parser->stats.cmd18_list);

	if (parser->xls_list)
		mmc_destroy_xls_list(parser->xls_list);

	if (parser->gkf)
		g_key_file_free(parser->gkf);

	if (parser->req_cb_list)
		mmc_destroy_req_cb_list(parser, parser->req_cb_list);

	// if (g_log_file)
	// 	fclose(g_log_file);

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

static void end_request(mmc_parser *parser);
static mmc_request *begin_request(mmc_parser *parser, int is_sbc, mmc_cmd *cmd, int req_type, unsigned int sectors)
{
	/*
	*	The command does not receive response, so we end it here before starting a new req
	*/
	if (parser->cur_req) {
		end_request(parser);
	}
	mmc_request *req = NULL;
	req = alloc_req(parser, sectors);
	req->sectors = sectors;

	if (is_sbc)
		req->sbc = cmd;
	else
		req->cmd = cmd;

	parser->cur_req = req;
	parser->req_type = req_type;
	//list_add_tail(&req->req_node, &parser->stats.requests_list);
	parser->stats.requests_list = g_slist_prepend(parser->stats.requests_list, req);

	return req;
}

/* end current request */
static void end_request(mmc_parser *parser)
{
	if (parser->cur_req) {
		//dbg(L_DEBUG, "\n====end of current request!====\n");

#if 0
		if (parser->cur_req->cmd==NULL && parser->cur_req->sbc) {
			parser->cur_req->cmd = parser->cur_req->sbc;
			parser->cur_req->sbc = NULL;
		}
#endif
		//assert(parser->cur_req->cmd!=NULL);
		//parser->cur_req->cmd maybe NULL, such as single cmd23
		if (parser->cur_req->cmd)
			dbg(L_DEBUG, "\n====end of current request[line: %d][cmd%d]!====\n", parser->cur_req->cmd->event_id, parser->cur_req->cmd->cmd_index);
		else
			dbg(L_DEBUG, "\n====end of current request[cmd is null]!====\n");
		mmc_req_cb *cb;
		GSList *iterator;
		//list_for_each_entry(cb, &parser->cb_list, cb_node) {
		for (iterator = parser->req_cb_list; iterator; iterator = iterator->next) {
			cb = (mmc_req_cb *)iterator->data;
			dbg(L_DEBUG, "doing callback: %s\n", cb->desc);
			cb->func(parser, cb->arg);
		}

		parser->prev_req = parser->cur_req;
		parser->cur_req = NULL;
		clear_parser_status(parser);
	}
}

//current used for cmd13
static void begin_pending(mmc_parser *parser, mmc_cmd *cmd, int req_type)
{
	dbg(L_DEBUG, "\n====start pending(cmd%d)!====\n", cmd->cmd_index);
	//fprintf(g_log_file, "\n====start pending(cmd%d)!====\n", cmd->cmd_index);
	mmc_pending_info *pending = alloc_pending();
	mmc_request *pending_req = alloc_req(parser, 0);

	pending->cmd = cmd;
	pending->req = pending_req;
	pending->req->cmd = cmd;
	pending->req_type = req_type;

	parser->pending_list = g_slist_append(parser->pending_list, pending);
}

/* set pending info to current request */
static void end_pending(mmc_parser *parser, mmc_pending_info *pending)
{
	dbg(L_DEBUG, "\n====end pending(cmd%d)!====\n", pending->req->cmd->cmd_index);
	//fprintf(g_log_file, "\n====end pending(cmd%d)!====\n", pending->req->cmd->cmd_index);
	//set pending req to cur req
	parser->cur_cmd = pending->cmd;
	parser->cur_req = pending->req;
	parser->req_type = pending->req_type;
	//list_add_tail(&parser->cur_req->req_node, &parser->stats.requests_list);
	parser->stats.requests_list = g_slist_prepend(parser->stats.requests_list, parser->cur_req);
	//clean pending info
	//parser->pending->cmd = NULL;
	//parser->pending->req = NULL;
	destroy_pending(pending);
}

static void calc_req_total_time(mmc_request *req, unsigned int end_time_us)
{
	assert(req->cmd!=NULL);
	req->total_time = end_time_us - req->cmd->time.time_us;
}

int mmc_parser_end(mmc_parser *parser)
{
	parser->stats.requests_list = g_slist_reverse(parser->stats.requests_list);
	parser->stats.cmd25_list = g_slist_reverse(parser->stats.cmd25_list);
	parser->stats.cmd18_list = g_slist_reverse(parser->stats.cmd18_list);
	return 0;
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
		cmd->event_id = event_id;
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
			if (!parser->pending_list) {
				if (parser->cur_req)
					parser->cur_req->stop = cmd;
				else
					begin_request(parser, 0, cmd, REQ_NORMAL, 0);
			} else {//cmd23+cmd25+cmd12, there is pending cmd13
				dbg(L_DEBUG, "cur req is not finished, store in pending!\n");
				begin_pending(parser, cmd, REQ_NORMAL);
			}
		break;

		//cmd13 can be sent while the bus is still busy
		case TYPE_13:
		//fprintf(g_log_file, "[%d]event:%s\n", event_id, ept->event_string);
			// if (!parser->cur_req) {	//cur req is finished
			// 	begin_request(parser, 0, cmd, REQ_NORMAL, 0);
			// } else {	//cur req is not finished, is still busy, maybe cmd6 or write
			// 	dbg(L_DEBUG, "cur req is not finished, store in pending!\n");
			// 	//fprintf(g_log_file, "cmd13 start pending\n");
			// 	begin_pending(parser, cmd, REQ_NORMAL);
			// }

			//cur req is not finished, is still busy, maybe cmd6 or write
			if (has_busy && parser->cur_req && (is_wr_cmd(parser->cur_req->cmd->cmd_index) || parser->cur_req->cmd->cmd_index==TYPE_6)) {
				dbg(L_DEBUG, "cur req is not finished, store in pending!\n");
				//fprintf(g_log_file, "cmd13 start pending\n");
				begin_pending(parser, cmd, REQ_NORMAL);
			} else {
				begin_request(parser, 0, cmd, REQ_NORMAL, 0);

				// if (!parser->cur_req) {	//cur req is finished
				// 	begin_request(parser, 0, cmd, REQ_NORMAL, 0);
				// } else {	//cur req is not finished, maybe does not receive response
				// 	end_request(parser);
				// 	begin_request(parser, 0, cmd, REQ_NORMAL, 0);
				// }
			}
		break;

		case TYPE_17:
			begin_request(parser, 0, cmd, REQ_RD, 1);
		break;

		case TYPE_18:
			ept->parse_info(rowFields[COL_INFO], &sectors);

			//pre-defined read
			if (parser->cur_req && parser->cur_req->sbc) {
				dbg(L_DEBUG, "sbc read!\n");
				parser->req_type = REQ_RD;
				parser->cur_req->cmd = cmd;
				if (sectors != parser->cur_req->sectors) {
					error("[%d]actual read sectors[%d] are different from cmd23's argument[%d]!\n", event_id, sectors, parser->cur_req->sectors);
					parser->cur_req->sectors = sectors;
				}
			} else {
				//open-ended read
				//ept->parse_info(rowFields[COL_INFO], &sectors);

				begin_request(parser, 0, cmd, REQ_RD, sectors);

			}

		break;

		case TYPE_21:
			begin_request(parser, 0, cmd, REQ_RD, 1);
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
			ept->parse_info(rowFields[COL_INFO], &sectors);
			//pre-defined write
			if (parser->cur_req && parser->cur_req->sbc) {
				dbg(L_DEBUG, "sbc write!\n");
				parser->req_type = REQ_WR;
				parser->cur_req->cmd = cmd;
				if (sectors != parser->cur_req->sectors) {
					error("[%d]actual write sectors[%d] are different from cmd23's argument[%d]!\n", event_id, sectors, parser->cur_req->sectors);
					parser->cur_req->sectors = sectors;
				}
			} else {
				//open-ended write
				//ept->parse_info(rowFields[COL_INFO], &sectors);
				begin_request(parser, 0, cmd, REQ_WR, sectors);
			}

		break;

		case TYPE_WR:

			if (parser->req_type == REQ_WR) {
				//parse data to req's data area
				if (strlen(rowFields[COL_DATA])/2 < 512) {
					// not enough data
					dbg(L_DEBUG, "no enough data\n");
				}

				//fprintf(g_log_file, "[%d]event:%s\n", event_id, ept->event_string);
				assert(parser->cur_req!=NULL);

				parser->cur_req->len_per_trans = ept->parse_data(rowFields[COL_DATA], (unsigned char*)parser->cur_req->data + parser->cur_req->len);
				parser->cur_req->len += parser->cur_req->len_per_trans;
				dbg(L_DEBUG, "len_per_trans:%d, trans len:%d\n", parser->cur_req->len_per_trans, parser->cur_req->len);

				parser->trans_cnt++;

				//sbc write, has data, no busy
				if (parser->trans_cnt == parser->cur_req->sectors && !has_busy && 
					(parser->cur_req->sbc || parser->cur_req->cmd->cmd_index==TYPE_24)) {
					end_request(parser);
				}
			}
		break;

		case TYPE_BUSY_START:
			if (!has_data && is_wr_cmd(parser->cur_req->cmd->cmd_index) && !parser->pending_list) {
				parser->trans_cnt++;
			}
		break;

		case TYPE_BUSY_END:
		{
			/*
				parse write busy time
			*/
			if (parser->req_type == REQ_WR && parser->trans_cnt > 0) {	//make sure trans_cnt > 0, otherwise array out of bound

				if (parser->trans_cnt > parser->cur_req->sectors) {	//csv file SC info is wrong?
					dbg(L_INFO, "[%d]trans_cnt(%d) > sectors(%d)\n", event_id, parser->trans_cnt, parser->cur_req->sectors);
					parser->cur_req->sectors = 2*parser->cur_req->sectors;
					parser->cur_req->delay = realloc(parser->cur_req->delay, parser->cur_req->sectors*sizeof(int));
				}

				unsigned int busy_time;
				ept->parse_info(rowFields[COL_INFO], &busy_time);
				//init write busy time, cmd12's busy time is added to the last write's busy time
				if (!parser->cur_req->stop && !parser->pending_list && parser->trans_cnt<=parser->cur_req->sectors)
					parser->cur_req->delay[parser->trans_cnt-1] = busy_time;

				assert(parser->trans_cnt<=parser->cur_req->sectors);
				dbg(L_DEBUG, "cur trans count:%d, all sc:%d\n", parser->trans_cnt, parser->cur_req->sectors);
				//sbc write, has busy 
				if (parser->trans_cnt == parser->cur_req->sectors && 
					((parser->cur_req->sbc && parser->cur_req->sectors == (parser->cur_req->sbc->arg&0xffff)) || parser->cur_req->cmd->cmd_index==TYPE_24)) {
					calc_req_total_time(parser->cur_req, time.time_us);

					end_request(parser);
				} else if (parser->cur_req->stop) {	//openended write, has busy
					//receive cmd12's busy end
					parser->cur_req->delay[parser->trans_cnt-1] += busy_time;
					
					//update correct SC
					parser->cur_req->sectors = parser->trans_cnt;

					calc_req_total_time(parser->cur_req, time.time_us);

					end_request(parser);
				} else if (parser->trans_cnt == parser->cur_req->sectors && 
					(parser->cur_req->sbc && parser->cur_req->sectors < (parser->cur_req->sbc->arg&0xffff)) &&
					parser->pending_list) {
					//cmd23+cmd25+cmd12, trans_cnt < sbc arg, should wait for cmd12's busy time

					//receive cmd12's busy end
					parser->cur_req->delay[parser->trans_cnt-1] += busy_time;
					calc_req_total_time(parser->cur_req, time.time_us);
					end_request(parser);

					GSList *list = parser->pending_list;
					GSList *iterator = NULL;
					GSList *last = g_slist_last(list);
					mmc_pending_info *pending_info;

					for (iterator = list; iterator; iterator = iterator->next) {
						pending_info = iterator->data;
						if (iterator != last) {
							end_pending(parser, pending_info);
							end_request(parser);
						} else {//last pending req, check if it is done
							if (pending_info->req->cmd->resp_type!=RESP_UND) {//if pending req has be done!
								end_pending(parser, pending_info);
								end_request(parser);
							} else {
								end_pending(parser, pending_info);
							}
						}
					}
					g_slist_free(list);
					parser->pending_list = NULL;
				}

			} else if (parser->req_type == REQ_NORMAL) {	//such as cmd6
				unsigned int busy_time;
				ept->parse_info(rowFields[COL_INFO], &busy_time);
				assert(parser->cur_req->cmd->cmd_index==TYPE_6);
				parser->cur_req->delay = busy_time;		//store busy time in pointer
				//calc total time here
				calc_req_total_time(parser->cur_req, time.time_us);

				end_request(parser);

				if (parser->pending_list) {
					GSList *list = parser->pending_list;
					GSList *iterator = NULL;
					GSList *last = g_slist_last(list);
					mmc_pending_info *pending_info;

					for (iterator = list; iterator; iterator = iterator->next) {
						pending_info = iterator->data;
						if (iterator != last) {
							end_pending(parser, pending_info);
							end_request(parser);
						} else {//last pending req, check if it is done
							if (pending_info->req->cmd->resp_type!=RESP_UND) {//if pending req has be done!
								end_pending(parser, pending_info);
								end_request(parser);
							} else {
								end_pending(parser, pending_info);
							}
						}
					}
					g_slist_free(list);
					parser->pending_list = NULL;
				}

			}
		}
		break;

		case TYPE_RD:
		{
			if (parser->req_type == REQ_RD && parser->trans_cnt < parser->cur_req->sectors) {
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
				
				assert(parser->trans_cnt<=parser->cur_req->sectors);
				parser->cur_req->delay[parser->trans_cnt-1] = wait_time;

				dbg(L_DEBUG, "cur trans count:%d, all sc:%d\n", parser->trans_cnt, parser->cur_req->sectors);
				//sbc read with data info
				if (parser->trans_cnt == parser->cur_req->sectors && 
					(parser->cur_req->sbc || 
					parser->cur_req->cmd->cmd_index==TYPE_17 || 
					parser->cur_req->cmd->cmd_index==TYPE_8 ||
					parser->cur_req->cmd->cmd_index==TYPE_21)) {
					calc_req_total_time(parser->cur_req, time.time_us);

					end_request(parser);
				} 
			}

		}
		break;

		case TYPE_R1:

			if (parser->cur_cmd == NULL) {
				error("[%d]received response R1 but no cmd parsed, by pass...\n", event_id);
				return -1;
			}

			//fill in cur cmd's resp
			parser->cur_cmd->resp_type = RESP_R1;
			ept->parse_data(rowFields[COL_DATA], &parser->cur_cmd->resp.r1);
			ept->parse_info(rowFields[COL_INFO], &parser->cur_cmd->resp_err);

			switch (parser->cur_cmd->cmd_index) {
				case TYPE_13:
					if (parser->pending_list) {
						dbg(L_DEBUG, "received CMD13 R1 in pending!\n");
						//get req from pending list
						//calc_req_total_time(parser->pending->req, time.time_us);
						
						/*
						if (!parser->cur_req) {	//if cur req has be done!
							end_pending(parser);
							end_request(parser);
						}*/
					} else {
						calc_req_total_time(parser->cur_req, time.time_us);
						end_request(parser);
					}
				break;

				case TYPE_3:
				case TYPE_16:
				case TYPE_35:
				case TYPE_36:
				assert(parser->cur_req!=NULL);
					calc_req_total_time(parser->cur_req, time.time_us);
					end_request(parser);
				break;

				case TYPE_8:
				case TYPE_17:
				case TYPE_21:
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
				error("[%d]received response R1B but no cmd parsed, by pass...\n", event_id);
				return -1;
			}
			//fill in cur cmd's resp
			parser->cur_cmd->resp_type = RESP_R1B;
			ept->parse_data(rowFields[COL_DATA], &parser->cur_cmd->resp.r1b);
			ept->parse_info(rowFields[COL_INFO], &parser->cur_cmd->resp_err);

			switch (parser->cur_cmd->cmd_index) {
				case TYPE_12:
				//openended read, with or without data info 
				//openended write, no busy, with or without data
				//normal cmd12 request
				if (!parser->pending_list) {
					if (parser->req_type == REQ_RD || (parser->req_type == REQ_WR && !has_busy) || parser->req_type == REQ_NORMAL) {
						calc_req_total_time(parser->cur_req, time.time_us);
						end_request(parser);
					}
				} else {//pending cmd12

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
				error("[%d]received response R2 but no cmd parsed, by pass...\n", event_id);
				return -1;
			}

			//fill in cur cmd's resp
			parser->cur_cmd->resp_type = RESP_R2;
			ept->parse_data(rowFields[COL_DATA], parser->cur_cmd->resp.r2);
			ept->parse_info(rowFields[COL_INFO], &parser->cur_cmd->resp_err);

			calc_req_total_time(parser->cur_req, time.time_us);
			end_request(parser);

			end_cmd(parser);
		break;

		case TYPE_R3:
			if (parser->cur_cmd == NULL) {
				error("[%d]received response R3 but no cmd parsed, by pass...\n", event_id);
				return -1;
			}

			//fill in cur cmd's resp
			parser->cur_cmd->resp_type = RESP_R3;
			ept->parse_data(rowFields[COL_DATA], &parser->cur_cmd->resp.r3);
			ept->parse_info(rowFields[COL_INFO], &parser->cur_cmd->resp_err);
			
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
