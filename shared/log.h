/******************************************************************************
* Notes: 
*   The types of log messages available are:
*     CRITICAL_MSG - Message is ALWAYS displayed. Msg needs some action taken
*     WARNING_MSG  - A recoverable problem, may indicate problems
*     INFO_MSG     - Tracks connections, requests, etc...
*     DEBUG_MSG    - very low-level, developer, verbose messages
*
******************************************************************************/
#ifndef LOG_H
#define LOG_H

#include <time.h>
#include <errno.h>

#define CRITICAL_MSG 1 // Bit 1 of log level 0000 0001
#define WARNING_MSG  2 // Bit 2 of log level 0000 0010
#define INFO_MSG     4 // Bit 3 of log level 0000 0100
#define DEBUG_MSG    8 // Bit 4 of log level 0000 1000

/* subsystems */
#define SUBSYS_NONE   0
#define SUBSYS_SHARED 1
#define SUBSYS_TPOOL  2
#define SUBSYS_LIB    3
#define SUBSYS_LIBCAS 4
#define SUBSYS_CMGR   5
#define SUBSYS_META   6
#define SUBSYS_DATA   7
#define SUBSYS_CLIENT 8
#define MAX_SUBSYS    9

extern int g_nLogLevel;

/*******************************************************************************
* MACRO      : LOG
* Parameters : fpLog    - File pointer to use to print message
*              nLogType - The type of log messages (see notes above for this file)
*              format   - same as format to printf
* Returns    : void
*-------------------------------------------------------------------------------
* Notes: 
*******************************************************************************/
#define LOG(fpLog, nLogType, nSubSys, format...)                                        \
{                                                                              \
   /* Should log this message. Check against the current logging level */      \
   if(nLogType & g_nLogLevel)                                                  \
   {                                                                           \
      struct tm stCurTime;                                                     \
      time_t    debug_print_time = 0;                                          \
      char      cLogType         = 0;                                          \
                                                                               \
      /* Get the current time */                                               \
      time(&debug_print_time);                                                 \
      localtime_r(&debug_print_time, &stCurTime);                              \
                                                                               \
      /* Add the appropriate Message Type Initial */                           \
      switch(nLogType)                                                         \
      {                                                                        \
         case CRITICAL_MSG: cLogType = 'C'; break;                             \
         case WARNING_MSG : cLogType = 'W'; break;                             \
         case INFO_MSG    : cLogType = 'I'; break;                             \
         case DEBUG_MSG   : cLogType = 'D'; break;                             \
      }                                                                        \
		/* Add the __FILE__, __LINE__ macros if appropriate */                   \
		switch(nLogType)																		 \
		{																							 \
			case INFO_MSG    : /* let this fall through to DEBUG_MSG */			 \
			case CRITICAL_MSG: /* let this fall through to DEBUG_MSG */			 \
			case WARNING_MSG : /* let this fall through to DEBUG_MSG */			 \
			case DEBUG_MSG   : {																 \
										 fprintf(fpLog, "(%s,%d) ", __FILE__, __LINE__);\
										 break;													 \
									 }																 \
		}																							 \
      fprintf(fpLog, "[%c %02d/%02d %02d:%02d] ",                              \
              cLogType,                                                        \
              stCurTime.tm_mon+1,                                              \
              stCurTime.tm_mday,                                               \
              stCurTime.tm_hour,                                               \
              stCurTime.tm_min);                                               \
 		do_print_message(nSubSys, fpLog);			                      \
      fprintf(fpLog, format);                                                  \
                                                                               \
      /* Check to see if the last character is a new line, add one if not */   \
      if(!contains_newline(format))                                            \
      {                                                                        \
         fprintf(fpLog, "\n");                                                 \
      }                                                                        \
                                                                               \
      /* Try and push log to file */                                           \
      fflush(fpLog);                                                           \
   }                                                                           \
}                                                                              

/*******************************************************************************
* Macro      : PERROR
* Parameters : pszErrorMsg - User defined message 
* Returns    : void
*-------------------------------------------------------------------------------
* Notes: Prints the user defined error message, along with the specific value
*        listed in errno and the system message for the errno number
*******************************************************************************/
#ifndef PERROR
#define PERROR(nSubSys, pszErrorMsg)                                       \
{                                                                              \
   /* Save a local copy of the errno, in case other errors override value */   \
   int nLocalErrno = errno;                                                    \
                                                                               \
   LOG(stderr, CRITICAL_MSG, nSubSys, "%s\n\terrno     : [%d]\n\terrno msg : [%s]",     \
       pszErrorMsg,                                                            \
       nLocalErrno,                                                            \
       strerror(nLocalErrno));                                                 \
}
#endif

void do_print_message(int, FILE*);

/*******************************************************************************
* Function   : set_log_level
* Parameters : nLogLevel - The log level to set
* Returns    : void
*-------------------------------------------------------------------------------
* Notes: Sets the current logging level 
*******************************************************************************/
void set_log_level(int nLogLevel);

/*******************************************************************************
* Function   : contains_newline
* Parameters : szMessage - NULL TERMINATED string
*              ...       - Needed because log function is a MACRO
* Returns    : NO_NEW_LINE    (0) - Last string character is NOT a new line
*              NEW_LINE_FOUND (1) - Last string character IS a new line
*-------------------------------------------------------------------------------
* Notes: szMessage MUST BE NULL TERMINATED. This function does NOT check for 
*        a new line character in any spot except the last character of string
*******************************************************************************/
int contains_newline(char * szMessage, ...);

/*******************************************************************************
* Function   : load_log_level
* Parameters : 
* Returns    : void
*-------------------------------------------------------------------------------
* Notes: 
*  This function will read a configuration file for the property "log_level".
*  The config file should have properties listed in the following format:
*     property1=value1
*     property2=value2
*******************************************************************************/
void load_log_level(char * pszConfigFile);

#endif
