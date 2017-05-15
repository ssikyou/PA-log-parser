#ifndef _FILE_H_
#define _FILE_H_
/*
 * log_name.cvs
 * result/
 *      log_name/
 *          pattern/
 *              log_name.txt
 *              read/
 *                  *.bin
 *              write/
 *                  *.bin
 */
typedef struct file_info{
    char *log_name;//parse log file name,except ".cvs".
    char *log_path;//log file path, except last '/' and log_name
    char *pattern;
    char *shell_name;
    char *read_path;
    char *write_path;
    unsigned int rid;//file name id for read
    unsigned int wid;//file name id for write
}file_info;

char * to_windows_path(char *path);
int update_shell_file(file_info *file, char *buf, int len);
int file_init(file_info *file, const char *log_name, const char *pattern);
int file_deinit(file_info *file);
int create_intinc_file(const char *name, unsigned int *val, unsigned short blocks);
int create_intdec_file(const char *name, unsigned int *val, unsigned short blocks);
int create_random_file(const char *name, unsigned short blocks);
int create_pattern_file(const char *name, const unsigned char *pattern, int pattern_size, unsigned short blocks);
int create_logdata_file(const char *name, const unsigned char *data, unsigned int len, unsigned int len_per_trans, unsigned short blocks);

#endif
