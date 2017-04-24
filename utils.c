#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "common.h"

#define DO_IO(func, fd, buf, nbyte)					\
	({												\
		ssize_t ret = 0, r;							\
		do {										\
			r = func(fd, (unsigned char*)buf + ret, nbyte - ret);	\
			if (r < 0 && errno != EINTR) {			\
				ret = -1;							\
				break;								\
			}										\
			else if (r > 0)							\
				ret += r;							\
		} while (r != 0 && (size_t)ret != nbyte);	\
													\
		ret;										\
	})

int do_read_file(char* file, void *buf, size_t len)
{
	int fd, ret = -1;
	if (file == NULL) {
		printf("read file failed: file path is NULL!\n");
		return ret;
	}
	fd = open(file, O_RDONLY);
	if (fd < 0) {
		perror("can't open file");
		return ret;
	}

	ret = DO_IO(read, fd, buf, len);
	if (ret < 0) {
		printf("%s: read file failed ret:%d file:%s\n", __func__, ret, file);
	} else if (ret != (int)len) {
		printf("%s: must be %d bytes length, but we read only %d, from file:%s\n", __func__, (int)len, ret, file);
	}
	close(fd);
	return ret;
}

int do_write_file(char* file, void *buf, size_t len)
{
	int fd, ret = -1;
	if (file == NULL) {
		printf("%s: read file failed: file path is NULL!\n",__func__);
		return ret;
	}
	fd = open(file, O_CREAT|O_WRONLY|O_TRUNC ,S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH );
	if (fd < 0) {
		perror("can't open file");
		return ret;
	}

	ret = DO_IO(write, fd, buf, len);
	if (ret < 0) {
		printf("%s: write file: %s\n", __func__, file);
	} else if (ret != (int)len) {
		printf("%s: must be %d bytes length, but we write only %d, to file:%s\n", __func__, (int)len, ret, file);
	}
	close(fd);
	return ret;
}

void print_param(int argc, char **argv)
{
    int i;
	for(i=0;i<argc;i++)
        dbg(L_DEBUG, "argv[%d]:%s\n",i,argv[i]);
}

int helper_arg(int min_num_arg, int max_num_arg, int *argc,
			char ***argv, const char *usage)
{
	//*argc -= optind;
	/* too many arguments, but when "max_num_arg < min_num_arg" then no
		 limiting (prefer "max_num_arg=-1" to gen infinity)
	*/
	if ( (*argc > max_num_arg) && (max_num_arg >= min_num_arg ) ) {
		fprintf(stderr, "too many non-option arguments (maximal: %i)\n", max_num_arg);
		printf("%s", usage);
		return -1;
		//exit(1);
	}

	/* print usage */
	if (*argc < min_num_arg) {
		fprintf(stderr, "too few non-option arguments (minimal: %i)\n", min_num_arg);
		printf("%s", usage);
		return -1;
		//exit(0);
	}

	//*argv += optind;

	return 0;
}

char *skip_spaces(const char *str)
{
	while (isspace(*str))
		++str;
	return (char *)str;
}

char *strim(char *s)
{
	size_t size;
	char *end;

	size = strlen(s);
	if (!size)
		return s;

	end = s + size - 1;
	while (end >= s && isspace(*end))
		end--;
	*(end + 1) = '\0';

	return skip_spaces(s);
}

static int MAX_WORDS = 10;
char **argv_split(char *str, const char *delim, int *argcp)
{
    char *token;
    int i = 0;

	char **argv = calloc(1, sizeof(char*)*MAX_WORDS);
	if(!argv){
		perror("calloc failed");
		*argcp = 0;
		return NULL;
	}

    /* get the first token */
    token = strsep(&str, delim);

    /* walk through other tokens */
    while( token != NULL ) 
    {
        dbg(L_DEBUG, " %s\n", token );
        if(i>=MAX_WORDS){
            MAX_WORDS = MAX_WORDS*2;
            argv = realloc(argv,sizeof(char*)*MAX_WORDS);
            dbg(L_DEBUG, "mem realloc==>%d\n", MAX_WORDS );
        }
        
		if(strcmp(token,"") != 0)
        	argv[i++]=token;

        token = strsep(&str, delim);
    }

	*argcp = i;
    return argv;
}

void dump_buffer(unsigned char *buf, int n)
{
	int i;
	int line = 0;
	for(i=0; i<n; i++) {
		if(i%16==0) {
			printf("\n");
			printf("[%02d]: ",line++);
		}
		printf("%02x ", buf[i]);
	}
}
