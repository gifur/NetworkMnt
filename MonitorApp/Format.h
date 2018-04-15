#ifndef _FORMAT_H_
#define _FORMAT_H_

#include <stdlib.h>
#include <conio.h>
#include <stdio.h>

const char report_interval[] =   "           %4.1lf-%4.1lf sec  %ss  %ss/sec\n";
const char result_t_upload[] =   "[T-Upload] %4.1lf-%4.1lf sec  %ss  %ss/sec\n";
const char result_t_download[] = "[T-Dwload] %4.1lf-%4.1lf sec  %ss  %ss/sec\n";
const char result_n_upload[] =   "[N-Upload] %4.1lf-%4.1lf sec  %ss  %ss/sec\n";
const char result_n_download[] = "[N-Dwload] %4.1lf-%4.1lf sec  %ss  %ss/sec\n";

const long kKilo_to_Unit = 1024;
const long kMega_to_Unit = 1024 * 1024;
const long kGiga_to_Unit = 1024 * 1024 * 1024;

const long kkilo_to_Unit = 1000;
const long kmega_to_Unit = 1000 * 1000;
const long kgiga_to_Unit = 1000 * 1000 * 1000;

enum {
	kConv_Unit,
	kConv_Kilo,
	kConv_Mega,
	kConv_Giga
};

const double kConversion[] =
{
	1.0,                       /* unit */
	1.0 / 1024,                /* Kilo */
	1.0 / 1024 / 1024,         /* Mega */
	1.0 / 1024 / 1024 / 1024   /* Giga */
};

/* labels for Byte formats Total*/
const char* kLabel_Total[] =
{
	"B",
	"KB",
	"MB",
	"GB"
};

/* labels for bit formats Speed */
const char* kLabel_Speed[]  =
{
	"B/S", 
	"KB/S",
	"MB/S",
	"GB/S"
};

void ByteSprintf(char * outString, int inLen, double inNum, BOOL isSpeed)
{
	int conv;
	const char* suffix;
	const char* format;

	double tmpNum = inNum;
	conv = kConv_Unit;

	while (tmpNum >= 1024.0 && conv <= kConv_Giga){
		tmpNum /= 1024.0;
		conv++;
	}

	inNum *= kConversion[conv];
	if (isSpeed)
	{
		suffix = kLabel_Speed[conv];
	}
	else
	{
		suffix = kLabel_Total[conv];
	}

	if (conv == 0)
	{
		format = "%5.0f%s";
	}
	else if (inNum < 9.995) {
		format = "%4.2f%s";
	}
	else if (inNum < 99.95) {
		format = "%4.1f%s";
	}
	else if (inNum < 999.5) {
		format = "%4.0f%s";
	}
	else {
		format = "%4.0f%s";
	}

	_snprintf_s(outString, inLen, inLen, format, inNum, suffix);
}

#endif //_FORMAT_H_