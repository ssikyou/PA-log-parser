#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "emmcparser.h"

#include "common.h"
#include "func.h"
#include "cypress_cfg.h"
#include "file.h"
#include "spec.h"

typedef struct cypress{
    cypress_cfg *cfg;
	void *spec;
    int start_value;
	int exec_cmd0;
	int exec_cmd1;
    file_info   file;
    unsigned char host_io;//1,4,8
    unsigned char data_rate;//0 sdr,1 ddr
    unsigned int curr_clk;//unit KHz
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

static int cmd_rsp_mask(mmc_cmd *cmd, cypress *press)
{
    cypress_cfg *cfg = press->cfg;
		
	if(cfg->comp_mask)
		return cfg->comp_mask;

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
		case 13:
            return 0;
        default:
            return 2;
    }
}

static int filt_illegal(mmc_cmd *cmd, cypress *press)
{
    cypress_cfg *cfg = press->cfg;

	if(cfg->filt_illegal){
		if((press->exec_cmd0 == 1)&&(cmd->cmd_index != 1))
			return 1;// do not exec class ops
		else if(cmd->cmd_index == 0){
			press->exec_cmd0 = 1;
		}else if(cmd->cmd_index == 1)
			press->exec_cmd0 = 0;
	}

	return 0;
}

static int filt_cmd1(mmc_cmd *cmd, cypress *press)
{
	//send two command 1
	if((press->exec_cmd1 == 2)&&(cmd->cmd_index == 1))
		return 1;//only two cmd1
	else if(cmd->cmd_index == 1){
		press->exec_cmd1++;
	}else
		press->exec_cmd1 = 0;

	return 0;
}

static int get_rsp_size(mmc_cmd *cmd, int spec_resp_size)
{
	int resp_size = 32;

    if(cmd->resp_type == RESP_UND)
        resp_size= 0;
    else if(cmd->resp_type == RESP_R2)
        resp_size = 128;

	if(spec_resp_size != resp_size){
		error("Warn:%s event_id:%d, cmd:%d, resp size:%d, not equal to spec :%d\n", __func__, cmd->event_id, cmd->cmd_index, resp_size, spec_resp_size);
	}
	return resp_size;
}

static int format_cmd(mmc_cmd *cmd, cypress *press)
{
    int resp_size = 32, spec_resp_size = 0, rsp_mask = 0, cmp_type = 0;
    int len = 0;
    char buf[512];
	int ret = 0;

	ret = filt_illegal(cmd, press);
	if(ret)
		return ret;

	ret = filt_cmd1(cmd, press);
	if(ret)
		return ret;

	spec_resp_size = get_cmd_rsp_size(cmd->cmd_index);
	resp_size = get_rsp_size(cmd, spec_resp_size);

    rsp_mask = cmd_rsp_mask(cmd, press);
    cmp_type = cmd_compare_type(cmd);

    len += sprintf(buf + len, "CMD%d,Argu=32'h%08x,RespSize=%d",
			cmd->cmd_index, cmd->arg, spec_resp_size);

    if(resp_size == 32){
		if(cmp_type > 0 && cmd->resp_err == 1)
			cmp_type = 0;

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
static void cypress_set_host(cypress *press)
{
	char buf[255];
	int len = 0;

	len += sprintf(buf + len,"SetHost,CLK=%d,DateRate=%s,IO=%d\n", press->curr_clk, data_rate_str(press->data_rate),press->host_io);
	update_shell_file(&press->file, buf, len);
}

static void handle_cmd6(mmc_cmd *cmd, cypress *press)
{
    cypress_cfg *cfg = press->cfg;
	unsigned int arg = cmd->arg;
    unsigned char index, value;

    index = (arg>>16)&0xff;
    value = (arg>>8)&0xff;

    switch(index){
        case 183://b7 bus width,
            switch(value){
                case 0://1bit data bus
                    press->host_io = HOST_IO_1BIT;
                    press->data_rate = DATA_RATE_SDR;
                    break;
                case 1://4bit data bus
                    press->host_io = HOST_IO_4BIT;
                    press->data_rate = DATA_RATE_SDR;
                    break;
                case 2://8bit data bus
                    press->host_io = HOST_IO_8BIT;
                    press->data_rate = DATA_RATE_SDR;
                    break;
                case 5://4bit ddr
                    press->host_io = HOST_IO_4BIT;
                    press->data_rate = DATA_RATE_DDR;
					break;
                case 6://8bit ddr
                    press->host_io = HOST_IO_8BIT;
                    press->data_rate = DATA_RATE_DDR;
					break;
            }
			press->curr_clk = cfg->usr_clk;
			cypress_set_host(press);
            break;
   }
}


static int wait_ready(cypress *press)
{
	char buf[255];
	int len = 0;

	len = sprintf(buf,"DataWaitReady\n");
	update_shell_file(&press->file, buf, len);
	return 0;
}

static int sbc(void *arg, mmc_cmd *sbc, mmc_val *val)
{
	cypress *press = (cypress *)arg;
	return format_cmd(sbc, press);
}

static int stop(void *arg, mmc_cmd *stop)
{
	cypress *press = (cypress *)arg;

	format_cmd(stop, press);
	wait_ready(press);

	return 0;
}


static int read_data(cypress *press,  mmc_request *req)
{
	cypress_cfg *cfg = press->cfg;
	char path[255];
	char buf[255];
	int len = 0;

	len = sprintf(buf,"DataR,IO=%d,DataRate=%s,BlockSize=%d,BlockNum=%d,SourceF=%s\n",
				 cfg->host_io, data_rate_str(cfg->data_rate),
				 cfg->block_size, req->sectors, get_read_file_path(req, press, path));
	update_shell_file(&press->file, buf, len);
	return len;
}

static int write_data(cypress *press,  mmc_request *req)
{
	cypress_cfg *cfg = press->cfg;
	char path[255];
	char buf[255];
	int len = 0;

    len += sprintf(buf + len,"DataW,IO=%d,DataRate=%s,BlockSize=%d,BlockNum=%d,SourceF=%s\n",
		 cfg->host_io, data_rate_str(cfg->data_rate),
		 cfg->block_size, req->sectors, get_write_file_path(req, press, path));
    len += sprintf(buf + len,"DataWaitReady\n");
	update_shell_file(&press->file, buf, len);
	return len;
}

static void do_assert(mmc_cmd *cmd)
{
	error("%s event_id:%d, cmd->index:%d, not know how to parse now\n", __func__, cmd->event_id, cmd->cmd_index);
	assert(0);
}

static int do_sleep(cypress *press, int ms)
{
	char buf[255];
	int len = 0;

	len += sprintf(buf + len,"sleep,time=%d\n",ms);
	update_shell_file(&press->file, buf, len);
	return 0;
}

static int class1(void*arg, mmc_request *req)
{
	cypress *press = (cypress *)arg;
	cypress_cfg *cfg = press->cfg;
	mmc_cmd *cmd = req->cmd;
	unsigned short index = cmd->cmd_index;

	switch(index){
		case 0:
            press->host_io = HOST_IO_1BIT;
            press->data_rate = DATA_RATE_SDR;
			press->curr_clk = cfg->init_clk;
			cypress_set_host(press);
			break;
        case 1:
            do_sleep(press, 500);
            break;
		case 2:
		case 3:
			break;
		case 4:
			do_assert(cmd);
			break;
		case 5:
			wait_ready(press);
			break;
        case 6:
            handle_cmd6(cmd, press);
			wait_ready(press);
            break;
		case 7:
			break;
		case 8:
			if(req->sectors != 1){
				error("%s event_id:%d, cmd->index:%d, req->sectors:%d not 1\n", __func__, cmd->event_id, index, req->sectors);
			}
			read_data(press, req);
			break;
		case 9://get csd
		case 10://get cid
			break;
		case 12:
		case 13:
			wait_ready(press);
			break;
		case 14:
			read_data(press, req);
			break;
		case 15:
			break;
		case 19:
			read_data(press, req);
			break;
		default:
			do_assert(cmd);
	}

	return 0;
}

static int class2(void*arg, mmc_request *req)
{
	cypress *press = (cypress *)arg;
	cypress_cfg *cfg = press->cfg;
	mmc_cmd *cmd = req->cmd;
	unsigned short index = cmd->cmd_index;

	switch(index){
		case 16:
            cfg->block_size = cmd->arg;
            break;
		case 17:
        case 18:
		case 21:
			read_data(press, req);
			break;
		default:
			do_assert(cmd);
	}
	return 0;	
}

static int class4(void*arg, mmc_request *req)
{
	cypress *press = (cypress *)arg;
	mmc_cmd *cmd = req->cmd;
	unsigned short index = cmd->cmd_index;

	switch(index){
		case 23:
 			//should do it in sbc;
			do_assert(cmd);
		case 24:
        case 25:
		case 26://prg cid
		case 27://prg csd
		case 49://prg rtc to card
			write_data(press, req);
			break;
		default:
			do_assert(cmd);
	}

	return 0;	
}

static int class5(void*arg, mmc_request *req)
{
	cypress *press = (cypress *)arg;
	mmc_cmd *cmd = req->cmd;
	unsigned short index = cmd->cmd_index;

	switch(index){
		case 35:
		case 36:
			break;
		case 38:
			wait_ready(press);
			break;
		default:
			do_assert(cmd);
	}

	return 0;
}

static int class6(void*arg, mmc_request *req)
{
	cypress *press = (cypress *)arg;
	mmc_cmd *cmd = req->cmd;
	unsigned short index = cmd->cmd_index;

	switch(index){
		case 28:
		case 29:
			wait_ready(press);
			break;
		default:
			do_assert(cmd);
	}

	return 0;
}

static int class11(void*arg, mmc_request *req)
{
	cypress *press = (cypress *)arg;
	mmc_cmd *cmd = req->cmd;
	unsigned short index = cmd->cmd_index;
	
	switch(index){
		case 48:
			wait_ready(press);
			break;
		default:
			do_assert(req->cmd);
	}

	return 0;
}

static int vendor(void*arg, mmc_request *req)
{
	//cypress *press = (cypress *)arg;
	mmc_cmd *cmd = req->cmd;
	unsigned short index = cmd->cmd_index;

	switch(index){
		default:
			do_assert(req->cmd);
	}
	return 0;
}

static int undefine(void*arg, mmc_request *req)
{
	do_assert(req->cmd);
	return 0;
}

static int pre(void*arg, mmc_request *req)
{
	cypress *press = (cypress *)arg;

	return format_cmd(req->cmd, press);
}

static int def(void*arg, mmc_request *req)
{
	//cypress *press = (cypress *)arg;
	mmc_cmd *cmd = req->cmd;
	unsigned short index = cmd->cmd_index;

	switch(index){
		//class3
			case 20:
				do_assert(req->cmd);
		//class7			
			case 42:
		//class8			
			case 55:			
			case 56:
		//class9			
			case 39:			
			case 40:
		//class10			
			case 53:			
			case 54:
			break;
		default:
			do_assert(req->cmd);
	}
	return 0;
}
extern void dump_req(mmc_request *req);
static int handle_large_request(cypress *press, mmc_request *req)
{
	cypress_cfg *cfg = press->cfg;

	mmc_cmd *cmd = req->cmd;
    char buf[255], path[255];
	int is_write, is_openend = 1, len = 0;

	dump_req(req);
	if(cmd == NULL){
		error("Err %s:no command request\n", __func__);
		if(req->sbc)
			error("\tsbc event_id:%d\n", req->sbc->event_id);
		if(req->stop)
			error("\tstop event_id:%d\n", req->stop->event_id);
		return -1;
	}

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


static class_ops ops = {
	.pre = &pre,//this is the first command for exec.
	.class1 = &class1,
	.class2 = &class2,
	.class4 = &class4,
	.class5 = &class5,
	.class6 = &class6,
	.class11 = &class11,
	.vendor = &vendor,
	.undefine = &undefine,

};

int cypress_init(func* func, func_param *param)
{
    int ret = 0;
    cypress *press = malloc(sizeof(cypress));
	press->cfg = (cypress_cfg *)param->cfg;

    ret = cypress_config_init(press->cfg, param->has_data);
    if(ret){
        free(press);
		error("%s init fail\n", __func__);
        return ret;
    }

    press->host_io = press->cfg->host_io;
    press->data_rate = press->cfg->data_rate;
	//press->curr_state = cfg->def_state;
	if(press->cfg->curr_state < STATE_TRAN)
    	press->curr_clk = press->cfg->init_clk;
	else
		press->curr_clk = press->cfg->usr_clk;

	press->spec = mmc_spec_init((void *)press, &ops, &sbc, &stop, &def);
	if(press->spec == NULL){
        free(press);
		error("%s init fail spec is NULL\n", __func__);
		return -1;
	}

    ret = file_init(&press->file, func->desc, pattern_type_str(press->cfg->pattern_type), param->log_path, 1);
    if(ret){
        free(press);
		error("%s init fail\n", __func__);
        return ret;
    }

	press->exec_cmd1 = 0;
	press->exec_cmd0 = 0;
    func->priv = (void*)press;
    return ret;
}

int cypress_request(func *func, mmc_request *req)
{
    cypress *press = (cypress *)func->priv;
	cypress_cfg *cfg = press->cfg;
	void *spec;

	if((press == NULL)||(press->spec == NULL))
		return 0;

	if((cfg->self_div) && (req->sectors > cfg->max_sectors)){
		handle_large_request(press, req);
		return 0;
	}

	spec = press->spec;
    if(req->sbc){
        handle_sbc_ops(spec, req->sbc);
    }

    if(req->cmd){
        handle_class_ops(spec, req);
    }

    if(req->stop){
		handle_stop_ops(spec, req->stop);
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


