#ifndef FACTORY_SELFTEST_H
#define FACTORY_SELFTEST_H

#include "app_cfg.h"

#if APP_FACTORY_UART0_ENABLE
void FactorySelftest_Init(void);
void FactorySelftest_FastPoll(void);
void FactorySelftest_SendBootReport(void);
UINT8 FactorySelftest_ShouldBlockDeepSleep(void);

/* Application code may override this weak hook for non-factory UART lines. */
void FactorySelftest_OnAppLine(const char *line);
#else
#define FactorySelftest_Init()             do { } while(0)
#define FactorySelftest_FastPoll()         do { } while(0)
#define FactorySelftest_SendBootReport()   do { } while(0)
#define FactorySelftest_ShouldBlockDeepSleep() (Is_No)
#endif

#endif
