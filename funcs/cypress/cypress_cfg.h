#ifndef _CYPRESS_CFG_H_
#define _CYPRESS_CFG_H_

#define HOST_IO_1BIT    1
#define HOST_IO_4BIT    4
#define HOST_IO_8BIT    8

#define DATA_RATE_SDR   0
#define DATA_RATE_DDR   1

#define PATTERN_RANDOM      0
#define PATTERN_USERINPUT   1
#define PATTERN_INTINC      2
#define PATTERN_INTDEC      3
#define PATTERN_FROMLOG     4

typedef struct cypress_cfg{
    unsigned char pattern_type;// 0 random data, 1 user pattern, 2 int increase, 3 int decrease ,4 from log data
    unsigned char *pattern_data;//save user data pattern
    int pattern_len;//user pattern data len
    unsigned int start_value;//for pattern increase or decrease, save user set int value.
    int block_size;
	int max_sectors;// >0 use max_sectors
    unsigned char host_io;//1,4,8
    unsigned char data_rate;//0 sdr,1 ddr
    unsigned int host_clk;//unit KHz
	int self_div;
	int comp_mask;
}cypress_cfg;

const char *data_rate_str(unsigned char data_rate);
const char *pattern_type_str(unsigned char pattern_type);
int cypress_load_configs(mmc_parser *parser, func_param *param);
int cypress_config_init(cypress_cfg *cfg, int has_data);
int cypress_config_deinit(cypress_cfg *cfg);

#endif
