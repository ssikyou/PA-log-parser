#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "emmcparser.h"

#include "common.h"
#include "func.h"
#include "cypress_cfg.h"
#include "file.h"

typedef struct cypress{
    cypress_cfg *cfg;
    int start_value;
	int exec_cmd1;
    file_info   file;
}cypress;


static char* get_write_file_path(mmc_request *req, cypress *press, char *path)
{
    void *data = req->data;
    unsigned short block_count = req->sectors;
    file_info *file = &press->file;
    cypress_cfg *cfg = press->cfg;

    sprintf(path,"%s/%d.bin",file->write_path, file->wid++);
    dbg(L_INFO, "%s:path:%s\n",__func__, path);
    switch(cfg->pattern_type){
        case 0:
            create_random_file(path, block_count);
            break;
        case 1:
            create_pattern_file(path, cfg->pattern_data, cfg->pattern_len, block_count);
            break;
        case 2:
            create_intinc_file(path, &cfg->start_value, block_count);
            break;
        case 3:
            create_intdec_file(path, &cfg->start_value, block_count);
            break;
        case 4:
            create_logdata_file(path, data, req->len, req->len_per_trans, block_count);
            break;
        default:
            create_random_file(path, block_count);
    }
    return to_windows_path(path);
}

static char* get_read_file_path(mmc_request *req, cypress *press, char *path)
{
    file_info *file = &press->file;

    sprintf(path,"%s/%d.bin",file->read_path, file->rid++);
    dbg(L_INFO, "%s:path:%s\n",__func__, path);
    return to_windows_path(path);
}

static int cmd_rsp_mask(mmc_cmd *cmd)
{
    switch(cmd->cmd_index){
        default:
            return 0x140;
    }
    return 0;
}

static int cmd_compare_type(mmc_cmd *cmd)
{
    switch(cmd->cmd_index){
        case 1:
            return 0;
        default:
            return 2;
    }
}

static int format_cmd(mmc_cmd *cmd, cypress *press)
{
    int resp_size = 32, rsp_mask = 0, cmp_type = 0;
    int len = 0;
    char buf[512];

    if(cmd->resp_type == RESP_UND)
        resp_size= 0;
    else if(cmd->resp_type == RESP_R2)
        resp_size = 128;

    rsp_mask = cmd_rsp_mask(cmd);
    cmp_type = cmd_compare_type(cmd);

    len += sprintf(buf + len, "CMD%d,Argu=32'h%08x,RespSize=%d",
			cmd->cmd_index, cmd->arg, resp_size);
    if(resp_size == 32){
        if(cmp_type > 0){
        len += sprintf(buf + len, ",CompType=%d,Resp=32'h%08x", cmp_type, cmd->resp.r1);
            if(rsp_mask != 0)
                len += sprintf(buf + len, ",Mask=32'h%08x", rsp_mask);
        }
    }
    len += sprintf(buf + len,"\n");

    update_shell_file(&press->file, buf, len);
    return 0;
}

static void handle_cmd6(mmc_cmd *cmd, cypress *press)
{
    cypress_cfg *cfg = press->cfg;
	unsigned int arg = cmd->arg;
    unsigned char index, value;
	char buf[255];
	int len = 0;

    index = (arg>>16)&0xff;
    value = (arg>>8)&0xff;

    switch(index){
        case 183://b7 bus width,
            switch(value){
                case 0://1bit data bus
                    cfg->host_io = HOST_IO_1BIT;
                    cfg->data_rate = DATA_RATE_SDR;
                    break;
                case 1://4bit data bus
                    cfg->host_io = HOST_IO_4BIT;
                    cfg->data_rate = DATA_RATE_SDR;
                    break;
                case 2://8bit data bus
                    cfg->host_io = HOST_IO_8BIT;
                    cfg->data_rate = DATA_RATE_SDR;
                    break;
                case 5://4bit ddr
                    cfg->host_io = HOST_IO_4BIT;
                    cfg->data_rate = DATA_RATE_DDR;
					break;
                case 6://8bit ddr
                    cfg->host_io = HOST_IO_8BIT;
                    cfg->data_rate = DATA_RATE_DDR;
					break;
            }
			len += sprintf(buf + len,"SetHost,CLK=%d,IO=%d\n", cfg->host_clk, cfg->host_io);
			update_shell_file(&press->file, buf, len);
            break;
   }
}

static int handle_request(mmc_request *req, cypress *press)
{
    int len = 0;
    int block_count = req->sectors;
    char buf[255],path[255];
    mmc_cmd *cmd = req->cmd;
    cypress_cfg *cfg = press->cfg;

	if((press->exec_cmd1 == 2)&&(cmd->cmd_index == 1))
		return 0;//only two cmd1
	else if(cmd->cmd_index == 1)
		press->exec_cmd1++;
	else
		press->exec_cmd1 = 0;

	format_cmd(cmd, press);

    switch(cmd->cmd_index){
        case 0:
            cfg->host_io = HOST_IO_1BIT;
            cfg->data_rate = DATA_RATE_SDR;
			len += sprintf(buf + len,"SetHost,CLK=400,IO=%d\n", cfg->host_io);
            break;
        case 1:
            len += sprintf(buf + len,"sleep,time=500\n");
            break;
        case 16:
            cfg->block_size = cmd->arg;
            break;
        case 2:
        case 3:
        case 7:
        case 9:
        case 10:
        case 35:
        case 36:
        case 55:
            break;
        //case 2:
        case 6:
            handle_cmd6(cmd, press);
            len += sprintf(buf + len,"DataWaitReady\n");
            break;
        case 5:
        case 12:
        case 13:
        case 28:
        case 29:
        case 38:
            len += sprintf(buf + len,"DataWaitReady\n");
            break;
        case 8:
        case 14:
        case 17:
        case 18:
        case 21:
            len += sprintf(buf + len,"DataR,IO=%d,DataRate=%s,BlockSize=%d,BlockNum=%d,SourceF=%s\n",
				 cfg->host_io, data_rate_str(cfg->data_rate),
				 cfg->block_size, block_count, get_read_file_path(req, press, path));
            break;
		case 49:
        case 19:
        case 24:
        case 25:
        case 26:
        case 27:
            len += sprintf(buf + len,"DataW,IO=%d,DataRate=%s,BlockSize=%d,BlockNum=%d,SourceF=%s\n",
				 cfg->host_io, data_rate_str(cfg->data_rate),
				 cfg->block_size, block_count, get_write_file_path(req, press, path));
            len += sprintf(buf + len,"DataWaitReady\n");
            break;
        default:
            error("Warning: %s, cmd->cmd_index:%d\n", __func__, cmd->cmd_index);
            assert(0);
    }
    update_shell_file(&press->file, buf, len);

    return 0;
}

static int handle_large_request(cypress *press, mmc_request *req)
{
	cypress_cfg *cfg = press->cfg;

	mmc_cmd *cmd = req->cmd;
    char buf[255], path[255];
	int is_write, is_openend = 1, len = 0;

	if(cmd->cmd_index == 25)
		is_write = 1;
	else if (cmd->cmd_index == 18)
		is_write = 0;
	else{
		error("Err %s: cmd_index%d is large cmd?\n", __func__, cmd->cmd_index);
		assert(0);
	}

	if(req->sbc != NULL)
		is_openend = 0;

	len += sprintf(buf + len, "LargeRw,BlockSize=%d,BlockNum=%d,isWrite=%d,isRandData=0,Addr=32'h%08x,isOpenEnd=%d,SourceF=%s\n",
			cfg->block_size, req->sectors, is_write, cmd->arg,
			is_openend,is_write?get_write_file_path(req, press, path):get_read_file_path(req, press, path));

	update_shell_file(&press->file, buf, len);
	return 0;
}

int cypress_init(func* func, func_param *param)
{
    int ret = 0;
    cypress *press = malloc(sizeof(cypress));
	press->cfg = (cypress_cfg *)param->cfg;

    ret = cypress_config_init(press->cfg, param->has_data);
    if(ret){
        free(press);
        return ret;
    }

    ret = file_init(&press->file, func->desc, pattern_type_str(press->cfg->pattern_type), param->log_path, 1);
    if(ret){
        free(press);
        return ret;
    }

	press->exec_cmd1 = 0;
    func->priv = (void*)press;
    return ret;
}



int cypress_request(func *func, mmc_request *req)
{
    cypress *press = (cypress *)func->priv;
	cypress_cfg *cfg = press->cfg;

	if((cfg->self_div) && (req->sectors > cfg->max_sectors)){
		handle_large_request(press, req);
		return 0;
	}
    if(req->sbc){
        format_cmd(req->sbc, press);
    }

    if(req->cmd){
        handle_request(req, press);
    }

    if(req->stop){
        format_cmd(req->stop, press);
    }
    return 0;
}

int cypress_destory(func *func)
{
    cypress *press = (cypress *)func->priv;

    file_deinit(&press->file);
    cypress_config_deinit(press->cfg);
    return 0;
}

func_ops cypress_ops = {
    .desc = "cypress",
    .load_configs = cypress_load_configs,
    .init = cypress_init,
    .request = cypress_request,
    .destory = cypress_destory,
};


