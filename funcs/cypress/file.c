#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "file.h"

char * to_windows_path(char *path)
{
    int i = 0;

    for(i = 0; i < strlen(path); i++)
        if(path[i] == '/')
            path[i] = '\\';
    return path;
}

static char *get_file_name(const char *path, int c)
{
    int len;
    const char *h,*t;

    h = strrchr(path,c);
    if(h == NULL){
        h = path;
    }else
        h += 1;

    t = strrchr(h,'.');
    if(t == NULL)
        len = strlen(h);
    else
        len = t - h;

	return strndup(h, len);
}

static char *get_file_path(const char *path, int c)
{
    char *h;

    h = strrchr(path,c);
    if(h == NULL)
        return strdup(".");

    return strndup(path, h - path);
}

char *get_parent_path(const char *path, int c)
{
    char *h, tmp[255];
	int len;

	strcpy(tmp, path);
    h = strrchr(tmp,c);
    if(h == NULL)
        return strdup(".");

	len = h - tmp;
	tmp[len] = 0;


    h = strrchr(tmp,c);
    if(h == NULL)
        return strdup(".");

    return strndup(tmp, h - tmp);
}

static int check_creat_dir(char *path)
{
    int len, i;
    char p[256];

    strcpy(p, path);
    len = strlen(p);
    dbg(L_INFO, "%s: len %d, path %s\n", __func__, len, p);
    if(p[len-1] != '/'){
        strcat(p, "/");
        len++;
    }

    for(i = 0; i < len; i++){
        if(p[i]=='/'){
            p[i] = 0;
            if(access(p, 0) != 0){
                if(mkdir(p, 0777) ==-1){
                    perror("mkdir error");
                    return -1;
                }
            }
            p[i] = '/';
        }
    }
    return 0;
}

int file_init(file_info *file, const char *desc, const char *pattern, const char *log_path, int need_bins)
{
    char name[255];
    int fd, ret;


    dbg(L_INFO, "log path: %s\n", log_path);
    file->log_name = get_file_name(log_path, '/');
    file->log_path = get_parent_path(log_path, '/');
    file->pattern = strdup(pattern);

	if(need_bins){
		file->rid = 0;//file name id for read
		file->wid = 0;//file name id for write
		sprintf(name,"%s/result/%s/%s/%s/write",file->log_path, desc, file->log_name, file->pattern);

		file->write_path = strdup(name);
		ret = check_creat_dir(file->write_path);
		if(ret)
		    return ret;

		sprintf(name,"%s/result/%s/%s/%s/read",file->log_path, desc, file->log_name, file->pattern);
		file->read_path = strdup(name);
		ret = check_creat_dir(file->read_path);
		if(ret)
		    return ret;

	}

    sprintf(name,"%s/result/%s/%s/%s/%s.txt",file->log_path, desc, file->log_name, file->pattern, file->log_name);
    file->shell_name = strdup(name);
    fd = open(file->shell_name, O_CREAT | O_RDWR |O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
    if(fd == -1)
        return fd;
    close(fd);

    return 0;
}

int file_deinit(file_info *file){
    if(file->log_name != NULL){
        free(file->log_name);
        file->log_name = NULL;
    }
    if(file->log_path != NULL){
        free(file->log_path);
        file->log_path = NULL;
    }
    if(file->pattern != NULL){
        free(file->pattern);
        file->pattern = NULL;
    }
    if(file->shell_name != NULL){
        free(file->shell_name);
        file->shell_name = NULL;
    }
    if(file->read_path != NULL){
        free(file->read_path);
        file->read_path = NULL;
    }
    if(file->write_path != NULL){
        free(file->write_path);
        file->write_path = NULL;
    }
    file->rid = 0;
    file->wid = 0;
    return 0;
}

int update_shell_file(file_info *file, char *buf, int len)
{
    int fd;

    fd = open(file->shell_name, O_APPEND | O_RDWR);
    write(fd, buf, len);
    close(fd);
    return 0;
}

int create_intinc_file(const char *name,unsigned int *val,unsigned short blocks)
{
    int fd;
    unsigned long  size = blocks*512;
    fd = open(name, O_CREAT | O_RDWR |O_TRUNC, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
    if (fd < 0) {
        printf("%s:ERR file create fail:%d\n", __func__, fd);
		return -1;
	}

    do{
        write(fd, val, sizeof(val));
        *val = *val + 1;
        size -=sizeof(val);
    }while(size > 0);

    close(fd);

    return 0;
}

int create_intdec_file(const char *name,unsigned int *val,unsigned short blocks)
{
    int fd;
    unsigned long  size = blocks*512;

    fd = open(name, O_CREAT | O_RDWR |O_TRUNC, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
    if (fd < 0) {
        printf("%s:ERR file create fail:%d\n", __func__, fd);
		return -1;
	}

    do{
        write(fd, val, sizeof(val));
        *val = *val - 1;
        size -=sizeof(val);
    }while(size > 0);

    close(fd);

    return 0;
}

int create_random_file(const char *name, unsigned short blocks)
{
    int fd;
    unsigned long  size = blocks*512;
    int rnd = random();

    fd = open(name, O_CREAT | O_RDWR |O_TRUNC, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
    if (fd < 0) {
        printf("%s:ERR file create fail:%d\n", __func__, fd);
		return -1;
	}

    do{
        write(fd, &rnd, sizeof(rnd));
        size -=sizeof(rnd);
    }while(size > 0);

    close(fd);

    return 0;
}

int create_pattern_file(const char *name,const unsigned char *pattern, int pattern_size, unsigned short blocks)
{
    int fd, len;
    unsigned long  size = blocks*512; 

    if(pattern == NULL){
        printf("%s:ERR pattern is NULL", __func__);
        return -1;
    }

    fd = open(name, O_CREAT | O_RDWR |O_TRUNC, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
    if (fd < 0) {
        printf("%s:ERR file create fail:%d\n", __func__, fd);
		return -1;
	}

    do{
        if(pattern_size < size)
            len = pattern_size;
        else
            len = size;
        write(fd, pattern, len);
        size -= len;
    }while(size > 0);

    close(fd);

    return 0;
}

int create_logdata_file(const char *name, const unsigned char *data, unsigned int data_len, unsigned int len_per_trans,unsigned short blocks)
{
    int i, fd, size = 0, len;
    int pattern_size = len_per_trans;
    const unsigned char *pattern;

    if(data == NULL){
        printf("%s:ERR log data part is NULL", __func__);
        return -1;
    }
    if(data_len != pattern_size*blocks){
        printf("%s:ERR data_len:%d  not equal to pattern_size(%d)*blocks(%d) ", __func__, data_len, pattern_size, blocks);
        return -1;
    }

    fd = open(name, O_CREAT | O_RDWR |O_TRUNC, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
    if (fd < 0) {
        printf("%s:ERR file create fail:%d\n", __func__, fd);
		return -1;
	}

    for(i = 0; i < blocks; i++){
        pattern = data + i * pattern_size;
        size = 512;
        do{
            if(pattern_size < size)
                len = pattern_size;
            else
                len = size;
            write(fd,pattern,len);
            size -= len;
        }while(size > 0);
    }
    close(fd);

    return 0;
}
