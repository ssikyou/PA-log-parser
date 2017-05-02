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

#include "common.h"
#include "emmcparser.h"

event_parse_template events[] = {
	/*{TYPE_0, " CMD00(GO_IDLE_STATE)"},
	{TYPE_1, " CMD01(SEND_OP_COND)"},
	{TYPE_2, " CMD02(ALL_SEND_CID)"},
	*/
	{TYPE_6, " CMD06(SWITCH)", parse_cmd_args, NULL, NULL, NULL},
	{TYPE_12, " CMD12(STOP_TRANSMISSION)", parse_cmd_args, NULL, NULL, NULL},
	{TYPE_13, " CMD13(SEND_STATUS)", parse_cmd_args, NULL, NULL, NULL},
	{TYPE_17, " CMD17(READ_SINGLE_BLOCK)", parse_cmd_args, NULL, NULL, NULL},
	{TYPE_18, " CMD18(READ_MULTIPLE_BLOCK)", parse_cmd_args, parse_cmd_sc, NULL, NULL},
	{TYPE_23, " CMD23(SET_BLOCK_COUNT)", parse_cmd_args, NULL, NULL, NULL},
	{TYPE_24, " CMD24(WRITE_SINGLE_BLOCK)", parse_cmd_args, NULL, NULL, NULL},
	{TYPE_25, " CMD25(WRITE_MULTIPLE_BLOCK)", parse_cmd_args, parse_cmd_sc, NULL, NULL},
	{TYPE_R1, "  R1 ", parse_resp_r1, NULL, NULL, NULL},
	{TYPE_R1B, "  R1b", parse_resp_r1, NULL, NULL, NULL},
	{TYPE_WR, "   Write", parse_rw_data, NULL, NULL, NULL},
	{TYPE_BUSY_START, "    BUSY START", NULL, NULL, NULL, NULL},
	{TYPE_BUSY_END, "    BUSY END", NULL, parse_wr_busy, NULL, NULL},
	{TYPE_RD, "   Read", parse_rw_data, parse_rd_waittime, NULL, NULL},


};

int get_temp_nums()
{
	return NELEMS(events);
}

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
	dbg(L_DEBUG, "v:%u\n", v);

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
	char tmp[128];
	memset(tmp, '\0', 30);
	char *str = tmp;
	memcpy(str, data, strlen((char *)data));

	strsep(&str, " ");	//"BUSY" tag
	char *busy_time = strsep(&str, " ");	//unit is us

	dbg(L_DEBUG, "busy_time str:%s\n", busy_time);
	unsigned int v = strtoul(busy_time, NULL, 0);
	dbg(L_DEBUG, "v:%u\n", v);

	*out = v;

	return 0;
}

int parse_rd_waittime(void *data, unsigned int *out)
{
	char tmp[128];
	memset(tmp, '\0', 30);
	char *str = tmp;
	memcpy(str, data, strlen((char *)data));

	strsep(&str, ":");	//"WaitTime" tag
	char *wait_time = str;	//unit is us

	dbg(L_DEBUG, "wait_time str:%s\n", wait_time);
	unsigned int v = strtoul(wait_time, NULL, 0);
	dbg(L_DEBUG, "v:%u\n", v);

	*out = v;

	return 0;
}

int parse_cmd_sc(void *data, unsigned int *out)
{
	char tmp[50];
	memset(tmp, '\0', 50);
	char *str = tmp;
	memcpy(str, data, strlen((char *)data));

	strsep(&str, ":");	//"SC" tag
	char *arg_value = str;

	dbg(L_DEBUG, "arg_value str:%s\n", arg_value);
	unsigned int v = strtoul(arg_value, NULL, 0);
	dbg(L_DEBUG, "v:%u\n", v);

	*out = v;
	return 0;
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