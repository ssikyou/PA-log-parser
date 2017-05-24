#ifndef _MMCXLS_H_
#define _MMCXLS_H_

#include "emmcparser.h"
#include "xlsxwriter.h"
#include "glib.h"

typedef struct xls_data_entry
{
	unsigned int idx;
	char *idx_desc;
	int val;
	double val_1;
	char *val_desc;
	int val_2;		//for serial2's y axis
} xls_data_entry;

typedef struct xls_area {
	int first_row;
	int first_col;
	int last_row;
	int last_col;
} xls_area;

typedef struct xls_sheet_config {
	char *sheet_name;

	//chart config
	unsigned char chart_type;
	char *chart_title_name;
	char *chart_x_name;
	char *chart_y_name;
	int x_steps;
	int chart_row;	//chart start row
	int chart_col;	//chart start col
	int chart_x_scale;
	int chart_y_scale;

	//serial config
	char *serie_name;
	char *serie2_name;
	xls_area x1_area;	//chart x asix's data cell range
	xls_area y1_area;	//chart y asix's data cell range
	xls_area x2_area;
	xls_area y2_area;
} xls_sheet_config;

typedef struct xls_config {
	char *filename;

	GSList *sheets;
} xls_config;

typedef struct mmc_xls_cb {
	char *desc;
	void *(* prep_data)(mmc_parser *parser, xls_sheet_config *config);
	int (* write_data)(lxw_workbook *workbook, lxw_worksheet *worksheet, void *data, xls_sheet_config *config);
	int (* create_chart)(lxw_workbook *workbook, lxw_worksheet *worksheet, xls_sheet_config *config);
	void (* release_data)(void *data);

	xls_config *config;
} mmc_xls_cb;

//common functions
mmc_xls_cb *alloc_xls_cb();
int mmc_register_xls_cb(mmc_parser *parser, mmc_xls_cb *cb);

int create_chart(lxw_workbook *workbook, lxw_worksheet *worksheet, xls_sheet_config *config);
void destroy_xls_entry(gpointer data);
void print_xls_entry(gpointer item, gpointer user_data);

#endif