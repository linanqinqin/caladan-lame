/*
 * lame.h - LAME bundle scheduling public interface
 */

 #pragma once

 /* TSC accounting functions */
 #ifdef CONFIG_LAME_TSC
 extern void lame_print_tsc_counters(void);
 #endif
 