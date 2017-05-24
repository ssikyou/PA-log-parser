#ifndef _MMCPARSER_H_
#define _MMCPARSER_H_

#include "list.h"
#include "glib.h"

#define COL_ID 		0
#define COL_TIME 	1
#define COL_TYPE 	2
#define COL_DATA 	3
#define COL_INFO 	4
#define COL_BUS 	5
#define COL_CLOCK 	6

typedef struct event_time {
	unsigned int time_us;
	unsigned int interval_us;
} event_time;

typedef struct mmc_row {
	unsigned int event_id;
	event_time time;
	unsigned char event_type;

	union {
		unsigned int arg;
		unsigned int r1;
		unsigned int r2[4];
		unsigned int r3;
		char *data;
	} event_data;

	union {
		unsigned short sc;
		char *info;
	} event_info;

	union {
		unsigned char bus_bit;
		unsigned short bus_freq;
	} bus;

	union {
		unsigned short nac;
		unsigned short nwr;
		unsigned short ncr;
		unsigned short nrc;
	} clock;

} mmc_row;

//Base cmd
#define TYPE_0		0
#define TYPE_1		1
#define TYPE_2		2
#define TYPE_3		3
#define TYPE_4		4
#define TYPE_5		5
#define TYPE_6		6
#define TYPE_7		7
#define TYPE_8		8
#define TYPE_9		9
#define TYPE_10		10
#define TYPE_12		12
#define TYPE_13		13
#define TYPE_16		16

//Read
#define TYPE_17		17
#define TYPE_18		18
#define TYPE_21		21

//Write
#define TYPE_23		23
#define TYPE_24		24
#define TYPE_25		25

//Erase
#define TYPE_35		35
#define TYPE_36		36
#define TYPE_38		38

//App cmd
#define TYPE_55		55
#define TYPE_56		56

//Other types
#define TYPE_WR		64
#define TYPE_RD		65
#define TYPE_R1		66
#define TYPE_R1B	67
#define TYPE_R2		68
#define TYPE_R3		69
#define TYPE_BUSY_START		70
#define TYPE_BUSY_END		71

#define TYPE_END	255

typedef struct event_parse_template {
	unsigned char event_type;
	char *event_string;

	int (* parse_data)(void *data, void *out);
	int (* parse_info)(void *info, void *out);
	int (* parse_bus)(void *bus, void *out);
	int (* parse_clock)(void *clock, void *out);
} event_parse_template;

#define MAX_CMD_NUM 70
typedef struct mmc_stats {
	GSList *requests_list;
	GSList *cmd25_list;
	GSList *cmd18_list;

	int cmds_dist[MAX_CMD_NUM];	//for cmd distribution, 0~63 for normal cmds, 64~69 for alternative cmds
} mmc_stats;

#define RESP_R1  	0
#define RESP_R1B  	1
#define RESP_R2  	2
#define RESP_R3  	3
#define RESP_UND	255

#define ERR_NONE	0
#define ERR_CRC7	1
#define ERR_CRC16	2

typedef struct mmc_cmd {
	unsigned int event_id;
	unsigned short cmd_index;
	unsigned int arg;
	event_time time;

	unsigned char resp_type;
	unsigned char resp_err;		//0: no error, 1: crc7 error
	union {
		unsigned int r1;
		unsigned int r1b;
		unsigned int r2[4];
		unsigned int r3;
	} resp;

} mmc_cmd;

typedef struct mmc_request {
	mmc_cmd *sbc;
	mmc_cmd *cmd;
	mmc_cmd *stop;
	unsigned short sectors;
	void *data;
	unsigned int len;
	unsigned int len_per_trans;

	//for performance analysis
	unsigned int total_time;	//includes delay time
	unsigned int *delay;		//for wr busy, for rd Nac
	unsigned int max_delay;
	unsigned int idle_time;		//idle time from previous request
} mmc_request;

struct mmc_parser;
typedef struct mmc_req_cb {
	char *desc;
	int (* init)(struct mmc_parser *parser, void *arg);
	int (* func)(struct mmc_parser *parser, void *arg);
	int (* destroy)(struct mmc_parser *parser, void *arg);
	void *arg;
} mmc_req_cb;

#define STATE_NONE		-1
#define STATE_OPEN_WR	0
#define STATE_OPEN_RD	1
#define STATE_SBC_WR	2
#define STATE_SBC_RD	3
#define STATE_SINGLE_WR	4
#define STATE_SINGLE_RD	5

#define REQ_NONE	-1
#define REQ_NORMAL	0
#define REQ_WR		1
#define REQ_RD		2

#define DATA_SIZE 1024*512

typedef struct mmc_pending_info {
	mmc_cmd *cmd;
	mmc_request *req;
	int req_type;
} mmc_pending_info;

typedef struct mmc_parser {
	int state;
	mmc_cmd *prev_cmd;
	mmc_cmd *cur_cmd;
	mmc_request *prev_req;
	mmc_request *cur_req;
	//mmc_pending_info *pending;	//if cur req is not finished, store next req in pending
	GSList *pending_list;
	void *data;
	int use_sbc;
	int req_type;		//
	int has_data;		//has write or read data event
	int has_busy;		//has busy event
	int trans_cnt;		//already transfered sector count

	GKeyFile* gkf;		//read config file
	mmc_stats stats;
	GSList *req_cb_list;
	GSList *xls_list;
} mmc_parser;

/* emmc parser functions */
mmc_parser *mmc_parser_init(int parse_data, int parse_busy, char *config_file);
void mmc_parser_destroy(mmc_parser *parser);
int mmc_row_parse(mmc_parser *parser, const char **rowFields, int fieldsNum);
int mmc_parser_end(mmc_parser *parser);
mmc_req_cb *alloc_req_cb(char *desc, int (* init)(struct mmc_parser *parser, void *arg),
											int (* func)(struct mmc_parser *parser, void *arg),
											int (* destroy)(struct mmc_parser *parser, void *arg),
											void *arg);
int mmc_cb_init(mmc_parser *parser);
int mmc_register_req_cb(mmc_parser *parser, mmc_req_cb *cb);
int mmc_xls_init(mmc_parser *parser, char *csvpath);
int generate_xls(mmc_parser *parser);

int parse_event_id(void *data, unsigned int *out);
int parse_event_time(void *data, event_time *out);

#endif
