#include <stdlib.h>

#include "common.h"
#include "func.h"
#include "simulator_cfg.h"

static config_list list[]={
    {.type = CFG_INT, .key = "block_size"},//0
	{.type = CFG_INT, .key = "erase_sectors"},//1
    {.type = CFG_INT, .key = "map_reset"},//2
};

int simulator_load_configs(mmc_parser *parser, func_param *param)
{
    simulator_cfg *cfg;
	int ret;

    cfg = malloc(sizeof(simulator_cfg));
    if(cfg == NULL){
        error("ERR: %s malloc cfg fail\n", __func__);
        return -1;
    }

    ret = config_load_list(parser, "Script Simulator", list, sizeof(list)/sizeof(list[0]));
    if(ret){
        error("ERR: %s load Simulator config fail\n", __func__);
        free(cfg);
        return -1;
    }

    cfg->block_size = list[0].val_int;
    cfg->erase_sectors = list[1].val_int;
	cfg->map_reset = list[2].val_int;

	param->cfg = (void *)cfg;

	return 0;
}

int simulator_config_init(simulator_cfg *cfg)
{
	return 0;
}

int simulator_config_deinit(simulator_cfg *cfg)
{
	return 0;
}
