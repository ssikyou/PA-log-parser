#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "emmcparser.h"

#include "common.h"
#include "func.h"
#include "simulator_cfg.h"
#include "file.h"
#include "spec.h"

typedef struct simulator{
	simulator_cfg *cfg;
    file_info file;
	void *spec;
	int force_prg;
	int reliable_wr;
	int cycle;
	unsigned int erase_start;
	unsigned int erase_end;
	int in_userpart;
}simulator;

static int simulator_shell_header(simulator *sim)
{
	char buf[255];
	int len = 0;
	simulator_cfg *cfg = sim->cfg;

	len = sprintf(buf + len,"%s\tR/W\tAddr\tLen\tForcePgm\tReliableWr\n",cfg->show_id?"EventId":"Cycles");
	update_shell_file(&sim->file, buf, len);
	return 0;
}

static int simulator_read(simulator *sim, unsigned int event_id, unsigned int addr, unsigned short sectors)
{
	char buf[255];
	unsigned int len = 0, id;
	simulator_cfg *cfg = sim->cfg;

	if(cfg->show_id)
		id = event_id;
	else
		id =sim->cycle++;
	len = sprintf(buf + len,"%d\tRe\t%x\t%x\t0\t0\n", id, addr, sectors);
	update_shell_file(&sim->file, buf, len);

	return 0;
}

static int simulator_write(simulator *sim, unsigned int event_id, unsigned int addr, unsigned short sectors)
{
	char buf[255];
	unsigned int len = 0, id;
	simulator_cfg *cfg = sim->cfg;

	if(cfg->show_id)
		id = event_id;
	else
		id =sim->cycle++;
	len = sprintf(buf + len,"%d\tWr\t%x\t%x\t%d\t%d\n", id, addr, sectors, sim->force_prg, sim->reliable_wr);
	update_shell_file(&sim->file, buf, len);

	return 0;
}

static int simulator_trim(simulator *sim, unsigned int event_id)
{
	char buf[255];
	unsigned int len = 0, id;
	simulator_cfg *cfg = sim->cfg;
	unsigned int addr;
	unsigned short sectors;

	if(cfg->show_id)
		id = event_id;
	else
		id =sim->cycle++;
	addr = sim->erase_start;
	sectors = sim->erase_end - sim->erase_start + 1;

	len = sprintf(buf + len,"%d\ttr\t%x\t%x\t0\t0\n", id, addr, sectors);
	update_shell_file(&sim->file, buf, len);
	return 0;
}

static int simulator_discard(simulator *sim, unsigned int event_id)
{
	char buf[255];
	unsigned int addr;
	unsigned short sectors;
	unsigned int len = 0, id;
	simulator_cfg *cfg = sim->cfg;

	if(cfg->show_id)
		id = event_id;
	else
		id =sim->cycle++;
	addr = sim->erase_start;
	sectors = sim->erase_end - sim->erase_start + 1;

	len = sprintf(buf + len,"%d\tDis\t%x\t%x\t0\t0\n", id, addr, sectors);
	update_shell_file(&sim->file, buf, len);
	return 0;
}

static int simulator_erase(simulator *sim, unsigned int event_id)
{
	char buf[255];
	unsigned int addr = 0, sectors = 0;
	unsigned int len = 0, id;
	simulator_cfg *cfg = sim->cfg;

	if(cfg->show_id)
		id = event_id;
	else
		id =sim->cycle++;

	addr = sim->erase_start*cfg->erase_sectors;
	sectors = (sim->erase_end - sim->erase_start +1)*cfg->erase_sectors;

	len = sprintf(buf + len,"%d\tEr\t%x\t%x\t0\t0\n", id, addr, sectors);
	update_shell_file(&sim->file, buf, len);
	return 0;
}

static int simulator_reset(simulator *sim, unsigned int event_id)
{
	char buf[255];
	unsigned int len = 0, id;
	simulator_cfg *cfg = sim->cfg;

	if(cfg->show_id)
		id = event_id;
	else
		id =sim->cycle++;

	len = sprintf(buf + len,"%d\tPOR\t0\t0\t0\t0\n", id);
	update_shell_file(&sim->file, buf, len);
	return 0;
}

static int simulator_suddom_reset(simulator *sim, unsigned int event_id)
{
	char buf[255];
	unsigned int len = 0, id;
	simulator_cfg *cfg = sim->cfg;

	if(cfg->show_id)
		id = event_id;
	else
		id =sim->cycle++;

	len = sprintf(buf + len,"%d\tSPOR\t0\t0\t0\t0\n", id);
	update_shell_file(&sim->file, buf, len);
	return 0;
}


static int class1(void*arg, mmc_request *req)
{
	simulator *sim = (simulator *)arg;
	simulator_cfg *cfg = sim->cfg;
	mmc_cmd *cmd = req->cmd;
	unsigned short index = cmd->cmd_index;
    unsigned char id, value;

	switch(index){
		case 0:
			if(cfg->map_reset)
				simulator_reset(sim, cmd->event_id);
			break;
		case 6:
		    id = (cmd->arg>>16)&0xff;
		    value = (cmd->arg>>8)&0xff;
			if(id == 179){//b3
	            if((value & 0x3) == 0){
					dbg(L_INFO, "%s:Enter user part\n",__func__);
					sim->in_userpart = 1;
				}else{
					dbg(L_INFO, "%sLeave user part\n",__func__);
					sim->in_userpart = 0;
				}
	        }
			break;
	}

	return 0;
}

static int class2(void*arg, mmc_request *req)
{
	simulator *sim = (simulator *)arg;
	mmc_cmd *cmd = req->cmd;
	unsigned short index = cmd->cmd_index;

	switch(index){
		case 17:
		case 18:
			simulator_read(sim, cmd->event_id, cmd->arg, req->sectors);
			break;
	}

	return 0;
}

static int class4(void*arg, mmc_request *req)
{
	simulator *sim = (simulator *)arg;
	mmc_cmd *cmd = req->cmd;
	unsigned short index = cmd->cmd_index;

	switch(index){
		case 24:
		case 25:
			simulator_write(sim, cmd->event_id, cmd->arg, req->sectors);
			break;
	}

	return 0;
}

static int class5(void*arg, mmc_request *req)
{
	simulator *sim = (simulator *)arg;
	mmc_cmd *cmd = req->cmd;
	unsigned short index = cmd->cmd_index;

	switch(index){
		case 35:
			sim->erase_start = cmd->arg;
			break;
		case 36:
			sim->erase_end = cmd->arg;
			break;
		case 38:
			if((cmd->arg & 0x3) == 1)//trim
				simulator_trim(sim, cmd->event_id);
			else if((cmd->arg == 0) ||(cmd->arg == 0x80000000))
				simulator_erase(sim, cmd->event_id);
			else if((cmd->arg & 0x3) == 3)
				simulator_discard(sim, cmd->event_id);

			sim->erase_start = 0;
			sim->erase_end = 0;
			break;
	}

	return 0;
}

static class_ops ops = {
	.class1 = &class1,
	.class2 = &class2,
	.class4 = &class4,
	.class5 = &class5,
};

static int sbc(void *arg, mmc_cmd *sbc, mmc_val *val)
{
	simulator *sim = (simulator *)arg;
	sim->force_prg = val->force_prg;
	sim->reliable_wr= val->reliable_wr;
	return 0;
}

int simulator_init(func* func, func_param *param)
{
    int ret = 0;
    simulator *sim = malloc(sizeof(simulator));
	sim->cfg = (simulator_cfg *)param->cfg;
    ret = simulator_config_init(sim->cfg);
    if(ret){
        free(sim);
		error("%s init fail\n", __func__);
        return ret;
    }
	sim->in_userpart = 1;
	sim->spec = mmc_spec_init((void *)sim, &ops, &sbc, NULL, NULL);
    ret = file_init(&sim->file, func->desc, NULL, param->log_path, 0);
    if(ret){
        free(sim);
		error("%s init fail\n", __func__);
        return ret;
    }
	simulator_shell_header(sim);
    func->priv = (void*)sim;
    return ret;
}

int simulator_request(func *func, mmc_request *req)
{
    simulator *sim = (simulator *)func->priv;
	void *spec;
	if((sim == NULL)||(sim->spec == NULL)){
		return 0;
	}
	if(sim->in_userpart == 0){
		return 0;
	}

	spec = sim->spec;
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

int simulator_destory(func *func)
{
    simulator *sim = (simulator *)func->priv;

    file_deinit(&sim->file);
    return 0;
}

func_ops simulator_ops = {
    .desc = "simulator",
    .load_configs = simulator_load_configs,
    .init = simulator_init,
    .request = simulator_request,
    .destory = simulator_destory,
};


