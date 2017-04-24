#ifndef _MMCPARSER_H_
#define _MMCPARSER_H_

#include "list.h"

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

//Read
#define TYPE_17		17
#define TYPE_18		18

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



typedef struct CMD25Info {
	unsigned int total_time;
	unsigned int address;
	unsigned int sectors;
	unsigned short max_busy_t;
	unsigned short busy_t[];
} CMD25Info;


typedef struct CMD18Info {
	unsigned int total_time;
	unsigned int address;
	unsigned int sectors;
	unsigned short max_nac;
	unsigned short nac[];
} CMD18Info;


typedef struct stats {
	struct list_head all_events;
	struct list_head cmd25_list;
	struct list_head cmd18_list;
	struct list_head cmd24_list;
	struct list_head cmd17_list;

} stats;


#define RESP_R1  	0
#define RESP_R1B  	1
#define RESP_R2  	2
#define RESP_R3  	3

typedef struct mmc_cmd {
	unsigned short cmd_index;
	unsigned int arg;
	event_time time;

	unsigned char resp_type;
	union {
		unsigned int r1;
		unsigned int r2[4];
		unsigned int r3;
	} resp;

} mmc_cmd;


typedef struct mmc_request {
	struct list_head req_node;
	mmc_cmd *sbc;
	mmc_cmd *cmd;
	mmc_cmd *stop;
	unsigned short sectors;
	unsigned int total_time;	//includes delay time
	unsigned int *delay;	//for wr busy, for rd Nac

	void *data;
	unsigned int len;

} mmc_request;


#define STATE_OPEN_WR	0
#define STATE_OPEN_RD	1
#define STATE_SBC_WR	2
#define STATE_SBC_RD	3
#define STATE_SINGLE_WR	4
#define STATE_SINGLE_RD	5

#define DIR_RD	0
#define DIR_WR	1

typedef struct mmc_parser {
	int state;
	mmc_cmd *prev_cmd;
	mmc_cmd *cur_cmd;
	mmc_request * prev_req;
	mmc_request * cur_req;
	int use_sbc;
	int dir;	//direction, rd 0, wr 1
	int parse_data;		//wether parse data or not
	int wr_cnt;


} mmc_parser;





mmc_parser *mmc_parser_init();
int parse_event_id(void *data, unsigned int *out);
int parse_event_time(void *data, event_time *out);
int mmc_row_parse(mmc_parser *parser, const char **rowFields, int fieldsNum);

#endif