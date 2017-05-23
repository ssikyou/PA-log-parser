#include <stdlib.h>
#include <assert.h>

#include "common.h"
#include "func.h"
#include "config.h"
#include "cypress_cfg.h"
static config_list list[]={
    {.type = CFG_INT, .key = "pattern_type"},
    {.type = CFG_STR, .key = "pattern_data"},
    {.type = CFG_INT, .key = "pattern_len"},
    {.type = CFG_INT, .key = "start_value"},
    {.type = CFG_INT, .key = "block_size"},
	{.type = CFG_INT, .key = "max_sectors"},
    {.type = CFG_INT, .key = "host_io"},
    {.type = CFG_INT, .key = "data_rate"},
    {.type = CFG_INT, .key = "host_clk"},
	{.type = CFG_INT, .key = "self_div"},
	{.type = CFG_INT, .key = "comp_mask"},
};


const char pattern_table[][10]={
    {"random"},
    {"usrpat"},
    {"intinc"},
    {"intdec"},
    {"logpat"},
};

static const char data_rate_table[][10] = {"SDR","DDR"};
const char *data_rate_str(unsigned char data_rate)
{
    assert((data_rate == DATA_RATE_SDR) || (data_rate == DATA_RATE_DDR));
    return data_rate_table[data_rate];
}

const char *pattern_type_str(unsigned char pattern_type)
{
    assert((pattern_type >= PATTERN_RANDOM) && (pattern_type <= PATTERN_FROMLOG));

    dbg(L_INFO,"%s:%s\n", __func__ , pattern_table[pattern_type]);
    return pattern_table[pattern_type];
}

int cypress_load_configs(mmc_parser *parser, func_param *param)
{
	int ret;
    cypress_cfg *cfg;

    cfg = malloc(sizeof(cypress_cfg));
    if(cfg == NULL){
        error("ERR: %s malloc cfg fail\n", __func__);
        return -1;
    }

    ret = config_load_list(parser, "Script Cypress", list, sizeof(list)/sizeof(list[0]));
    if(ret){
        error("ERR: %s load Cypress config fail\n", __func__);
        free(cfg);
        return -1;
    }

    cfg->pattern_type = list[0].val_int;
    cfg->pattern_data = (unsigned char *)list[1].val_char;
    cfg->pattern_len = list[2].val_int;
    cfg->start_value = list[3].val_int;
    cfg->block_size = list[4].val_int;
	cfg->max_sectors = list[5].val_int;
    cfg->host_io = list[6].val_int;
    cfg->data_rate = list[7].val_int;
    cfg->host_clk = list[8].val_int;
	cfg->self_div = list[9].val_int;
	cfg->comp_mask = list[10].val_int;

	param->cfg = (void *)cfg;
	if((cfg->self_div == 0 )&&(cfg->max_sectors > 0))
		param->max_sectors = cfg->max_sectors;

    return 0;
}

int cypress_config_init(cypress_cfg *cfg, int has_data)
{
	assert(cfg);

    if((cfg->pattern_type > PATTERN_FROMLOG) || (cfg->pattern_type < PATTERN_RANDOM)){
        error("ERR:%s pattern_type invalid:%d\n", __func__, cfg->pattern_type);
        return -1;
    }

    if((cfg->pattern_type == PATTERN_USERINPUT) && ((cfg->pattern_data == NULL) || (cfg->pattern_len == 0))){
        printf("ERR: %s data pattern is NULL or pattern_len == 0\n", __func__);
        return -1;
    }

    if((has_data == 0) && (cfg->pattern_type == PATTERN_FROMLOG)) {
        printf("ERR: %s no data field in soure file\n", __func__);
        return -1;
    }

    if(cfg->block_size != 512){
        printf("Warr: %s block_size:%d != 512\n", __func__, cfg->block_size);
        return -1;
    }

    if((cfg->host_io != HOST_IO_1BIT) &&
			(cfg->host_io != HOST_IO_4BIT) &&
			(cfg->host_io != HOST_IO_8BIT)){
        error("ERR: %s invalid host_io:%d\n", __func__, cfg->host_io);
        return -1;
    }

    if((cfg->data_rate != DATA_RATE_SDR) &&
			(cfg->data_rate != DATA_RATE_DDR)){
        error("ERR: %s invalid data_rate:%d\n", __func__, cfg->data_rate);
        return -1;
    }

    if((cfg->host_clk >50000)||(cfg->host_clk < 400)){
        error("ERR: %s host_clk(%d) out of range for cypress\n", __func__, cfg->host_clk);
        return -1;
    }
    //pattern_type 0 random data, 1 user pattern, 2 int increase, 3 int decrease ,4 from log data
    return 0;
}

int cypress_config_deinit(cypress_cfg *cfg)
{
    if(cfg->pattern_data != NULL){
        free(cfg->pattern_data);
        cfg->pattern_data = NULL;
    }
    return 0;
}
