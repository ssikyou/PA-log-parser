#ifndef _SIMULATOR_CFG_H_
#define _SIMULATOR_CFG_H_

typedef struct simulator_cfg{
	int erase_sectors;
	int map_reset;//0 not filt, 1 filt, bit0 filt illegal cmd between cmd0 and cmd1, bit1 filt no response cmd
	int show_id;
}simulator_cfg;

int simulator_load_configs(mmc_parser *parser, func_param *param);
int simulator_config_init(simulator_cfg *cfg);
int simulator_config_deinit(simulator_cfg *cfg);

#endif
