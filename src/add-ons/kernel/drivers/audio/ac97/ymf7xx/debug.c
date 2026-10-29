/*
 * YMF7XX Haiku Driver for Yamaha DS-XG (YMF-7xx) audio
 *
 * Copyright 2026, Pirati Del Frico
 * Distributed under the terms of the MIT License.
 */


#include <KernelExport.h>

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include <directories.h>
#include <OS.h>

#include "debug.h"
#include "ymf7xx.h"


#if DEBUG > 0
static const char *logfile = kSystemLogDirectory "/ymf7xx.log";
static sem_id loglock;
#endif


void ymf7xx_debug_printf(const char *text,...);
void log_printf(const char *text,...);
void log_create(void);


void ymf7xx_debug_printf(const char *text,...)
{
	char buf[1024];
	va_list ap;

	va_start(ap,text);
	vsprintf(buf,text,ap);
	va_end(ap);

	dprintf(DRIVER_NAME ": %s",buf);
}


void log_create()
{
#if DEBUG > 0
	int fd = open(logfile, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	const char *text = DRIVER_NAME ", " VERSION "\n";
	loglock = create_sem(1,"logfile sem");
	write(fd,text,strlen(text));
	close(fd);
#endif
}


void log_printf(const char *text,...)
{
#if DEBUG > 0
	int fd;
	char buf[1024];
	va_list ap;

	va_start(ap,text);
	vsprintf(buf,text,ap);
	va_end(ap);

	dprintf(DRIVER_NAME ": %s",buf);

	acquire_sem(loglock);
	fd = open(logfile, O_WRONLY | O_APPEND);
	write(fd,buf,strlen(buf));
	close(fd);
	release_sem(loglock);
#endif
}
