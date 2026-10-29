/*
 * YMF7XX Haiku Driver for Yamaha DS-XG (YMF-7xx) audio
 *
 * Copyright 2026, Pirati Del Frico
 * Distributed under the terms of the MIT License.
 */

#ifndef _DEBUG_H_
#define _DEBUG_H_

/*
 * PRINT() executes dprintf if DEBUG = 0 (disabled), or expands to LOG() when DEBUG > 0
 * TRACE() executes dprintf if DEBUG > 0
 * LOG()   executes dprintf and writes to the logfile if DEBUG > 0
 */

#ifndef DEBUG
	#define DEBUG 1
#endif

#undef PRINT
#undef TRACE
#undef ASSERT

#if DEBUG > 0
	#define PRINT(a)		log_printf a
	#define TRACE(a) 		ymf7xx_debug_printf a
	#define LOG(a)			log_printf a
	#define LOG_CREATE()	log_create()
	#define ASSERT(a)		if (a) {} else LOG(("ASSERT failed! file = %s, line = %d\n",__FILE__,__LINE__))
	void log_create();
	void log_printf(const char *text,...);
	void ymf7xx_debug_printf(const char *text,...);
#else
	void ymf7xx_debug_printf(const char *text,...);
	#define PRINT(a)	ymf7xx_debug_printf a
	#define TRACE(a)	((void)(0))
	#define ASSERT(a)	((void)(0))
	#define LOG(a)		((void)(0))
	#define LOG_CREATE()
#endif

#endif
