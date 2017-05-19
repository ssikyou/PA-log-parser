#ifndef __COMMON_H__
#define __COMMON_H__

#include <stdio.h>
#include <getopt.h>

#define VERSION "0.1"

#define for_each_opt(opt, long, short)  \
    optind = 0;                          \
    while ((opt=getopt_long(argc, argv, short ? short:"+h", long, NULL)) != -1)

#define L_ERROR 1
#define L_INFO  2
#define L_DEBUG 3
extern int g_debug_level;
extern FILE *g_log_file;

#define DEBUG
#ifdef DEBUG
	#define dbg(level, ...) \
		do { if (level<=g_debug_level && g_log_file) fprintf(g_log_file, __VA_ARGS__); } while (0)
#else
	#define dbg(level, ...)
#endif

#define error(...) \
	do { fprintf(stderr, __VA_ARGS__); \
		if (g_log_file) fprintf(g_log_file, __VA_ARGS__); } while (0)

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

#define NELEMS(x)  (sizeof(x) / sizeof((x)[0]))

int do_read_file(char* file, void *buf, size_t len);
int do_write_file(char* file, void *buf, size_t len);

/**
 * Note: used to check non-option args, not for options!!!
 * TODO
 *
 * Return 0 for success, otherwise fail.
 */ 
int helper_arg(int min_num_arg, int max_num_arg, int *argc,
			char ***argv, const char *usage);

/**
 * skip_spaces - Removes leading whitespace from @str.
 * @str: The string to be stripped.
 *
 * Returns a pointer to the first non-whitespace character in @str.
 */
char *skip_spaces(const char *str);

/**
 * strim - Removes leading and trailing whitespace from @s.
 * @s: The string to be stripped.
 *
 * Note that the first trailing whitespace is replaced with a %NUL-terminator
 * in the given string @s. Returns a pointer to the first non-whitespace
 * character in @s.
 */
char *strim(char *s);

/**
 * argv_split - Split string into argv.
 * @str: The string to be split.
 * @delim: The delimiter string.
 * @argcp: The pointer to argc.
 *
 * The @str will be split into args array which will be returned by this function.
 * The args count will be stored in @argcp.
 * Users need to free the args array memory when done.
 */
char **argv_split(char *str, const char *delim, int *argcp);

#endif
