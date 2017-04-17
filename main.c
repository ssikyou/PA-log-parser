#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/mmc/ioctl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/time.h>
#include <getopt.h>


static int helper_arg(int min_num_arg, int max_num_arg, int *argc,
			char ***argv, const char *usage)
{
	if ( (*argc > max_num_arg) && (max_num_arg >= min_num_arg ) ) {
		fprintf(stderr, "too many non-option arguments (maximal: %i)\n", max_num_arg);
		printf("%s", usage);
		return -1;
	}

	/* print usage */
	if (*argc < min_num_arg) {
		fprintf(stderr, "too few non-option arguments (minimal: %i)\n", min_num_arg);
		printf("%s", usage);
		return -1;
	}

	return 0;
}


static struct option cmd_options[] = {
	{ "help",	0, 0, 'h' },
	{ "dev",	1, 0, 'd' },
	{ "ctl",	1, 0, 'c' },
	{ "block",	1, 0, 'b' },
	{ "status",	0, 0, 's' },
	{ 0, 0, 0, 0 }
};

static const char *cmd_help =
	"Usage:\n"
	"\tffu [--dev=/path/to/mmcblkX] [--ctl=/path/to/ctl] [--block=n] </path/to/fwpath>\n"
	"arguments:\n"
	"\tfwpath: firmware file path\n"
	"options:\n"
	"\t--dev: device path\n"
	"\t--ctl: hw control device node path\n"
	"\t--block: send n blocks per request\n"
	"\t--status: show ffu status and firmware version\n"
	"Example:\n"
	"\tffu --dev=/dev/block/mmcblk0 --ctl=/dev/mmc-ctl fw.bin\n"
	"\tffu --dev=/dev/block/mmcblk0 --status\n";

int main(int argc, char **argv)
{
	int opt;
	int ret = 0;

	char *dev = NULL;
	//char *ctl = NULL;
	unsigned int block = 0;
	int show_status = 0;
	int dev_fd, img_fd;

    while ((opt=getopt_long(argc, argv, "hd:c:b:s", cmd_options, NULL)) != -1) {
		switch (opt) {
			case 'd':
				dev = optarg;
				break;
		/*	
			case 'c':
				ctl = optarg;
		*/		break;

			case 'b':
				block = strtoul(optarg, NULL, 0);
				break;

			case 's':
				show_status = 1;
				break;

			case 'h':
			default:
				printf("%s", cmd_help);
				return 0;
		}
	}

	if (show_status) {
		//check the necessary params
		if(!dev) {
			dev = DEFAULT_DEVICE_PATH;
			printf("device is NULL, use default device:%s\n", dev);
		}
	
		dev_fd = open(dev, O_RDWR);
		if (dev_fd < 0) {
			perror("device open failed");
			return -1;
		}

		return show_ffu_status(dev_fd);
	}

	argc -= optind;
	argv += optind;

	if(helper_arg(1, 1, &argc, &argv, cmd_help) != 0)
		return -1;

	//check the necessary params
	if(!dev) {
		dev = DEFAULT_DEVICE_PATH;
		printf("device is NULL, use default device:%s\n", dev);
	}
/*
	if(!ctl) {
		ctl = DEFAULT_CTL_PATH;
		printf("device is NULL, use default ctl device:%s\n", ctl);
	}
*/
	dev_fd = open(dev, O_RDWR);
	if (dev_fd < 0) {
		perror("device open failed");
		return -1;
	}
/*	
	ctl_fd = open(ctl, O_RDWR);
	if (ctl_fd < 0) {
		perror("ctl device open failed");
		return -1;
	}
*/
	img_fd = open(argv[0], O_RDONLY);
	if (img_fd < 0) {
		perror("image open failed");
		close(dev_fd);
		return -1;
	}

	ret = do_ffu(dev_fd, 0, img_fd, block);

	close(img_fd);
	//close(ctl_fd);
	close(dev_fd);
	return ret;
}

