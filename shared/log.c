/******************************************************************************
* Purpose:
*   This module provides an interface to any C program to write a log message
*   to the log file.  
*------------------------------------------------------------------------------
* Notes: (Also see notes in log.h)
*
*   This file determines if the message should logged by checking the log level.
*   The log level is a bit mask. The type of message (CRITICAL_MSG, INFO_MSG, ...)
*   is defined to the bit position (right to left).
******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <log.h>

static char *subsys_string[] = {
	"none  :",
	"shared:",
	"tpool :",
	"lib   :",
	"libcas:",
	"cmgr  :",
	"meta  :",
	"data  :",
	"client:",
	"",
};

#define LOG_LEVEL_PROPERTY_NAME "log_level"

/* Global variables */ 
/* Contains the current logging state. We default to NO LOGGING, and let the 
   application determine whether to change logging levels */
int g_nLogLevel = 0 ; 


void do_print_message(int nSubSys, FILE *fpLog)
{
	if (nSubSys <= 0 || nSubSys >= MAX_SUBSYS) {
		return;
	}
	fprintf(fpLog," %s ", subsys_string[nSubSys]);
	return;
}

/* See log.h for function prototype and description */
void set_log_level(int nNewLogLevel)
{ 
   int  nPrevLogLevel  = 0;
   char szMessage[256] = "";
   
   /* We want to ALWAYS log any changes to the state of the log level */
   nPrevLogLevel = g_nLogLevel; /* Save the state of the current logging level */
   /* Make sure information messaging is turned on */
   g_nLogLevel = g_nLogLevel | INFO_MSG; 
   
   LOG(stdout, INFO_MSG, SUBSYS_NONE, "----- Log Level Changing -----");
   
   strcpy(szMessage, "Current Logging Level includes :");
   if(nPrevLogLevel & CRITICAL_MSG)
   {
      strcat(szMessage, " CRITICAL ");
   }
   if(nPrevLogLevel & WARNING_MSG)
   {
      strcat(szMessage, " WARNING ");
   }
   if(nPrevLogLevel & INFO_MSG)
   {
      strcat(szMessage, " INFO ");
   }
   if(nPrevLogLevel & DEBUG_MSG)
   {
      strcat(szMessage, " DEBUG");
   }
   LOG(stdout, INFO_MSG, SUBSYS_NONE, "%s", szMessage);

   strcpy(szMessage, "New     Logging Level includes :");
   if(nNewLogLevel & CRITICAL_MSG)
   {
      strcat(szMessage, " CRITICAL ");
   }
   if(nNewLogLevel & WARNING_MSG)
   {
      strcat(szMessage, " WARNING ");
   }
   if(nNewLogLevel & INFO_MSG)
   {
      strcat(szMessage, " INFO ");
   }
   if(nNewLogLevel & DEBUG_MSG)
   {
      strcat(szMessage, " DEBUG ");
   }
   LOG(stdout, INFO_MSG, SUBSYS_NONE, "%s", szMessage);
   
   g_nLogLevel = nNewLogLevel;
}

/* See log.h for function prototype and description */
int contains_newline(char * szMessage, ...)
{
   #define NO_NEW_LINE    0
   #define NEW_LINE_FOUND 1

   int nRet = NO_NEW_LINE,
       i    = 0;
       
   if (szMessage != NULL)
   {
      /* the szMessage MUST be NULL TERMINATED */
      i = strlen(szMessage);
      
      /* We need to back up one (since arrays are zero based) */
      i--;
      
      if(szMessage[i] == '\n')
      {
         nRet = NEW_LINE_FOUND;
      }
   }
  
   return nRet;
}

/* See log.h for function prototype and description */
void load_log_level(char * pszConfigFile)
{
   FILE * fpConfigFile        = NULL;
   char * strtok_ptr          = NULL;
   char   szBuffer[512]       = "";   /* Allocate now, don't have to worry about free */
   int    nRetVal             = 0;
   char   szPropertyName[256] = ""; 
   char   szPropertyVal[256]  = "";
   char   szTokenDelim[]      = "=";

   /* Make sure the pointer is valid */
   if(pszConfigFile != NULL)
   {
      /* We only need read-only access */
      fpConfigFile = fopen(pszConfigFile, "r");
        
      if(fpConfigFile != NULL)
      {
         LOG(stderr, DEBUG_MSG, SUBSYS_NONE, "Successfully opened [%s]\n", pszConfigFile);
         // Iterate through all the lines
         while (fgets(szBuffer, sizeof(szBuffer), fpConfigFile) != NULL)
         {
            // Test to see if we've overflowed buffer
            if ((strlen(szBuffer)+1) >= sizeof(szBuffer))
            {
               LOG(stderr, CRITICAL_MSG, SUBSYS_NONE, "Internal variable \"buffer\" too short.\n");
               fclose(fpConfigFile);
               fpConfigFile = NULL;
               return; /* Just return */
            }

            /* Parse out the "property" and "value" fields from the current entry */
            nRetVal= snprintf(szPropertyName, 
                              sizeof(szPropertyName),
                              strtok_r(szBuffer, (const char *) szTokenDelim, &strtok_ptr));
                              
            if ((nRetVal+1) > sizeof(szPropertyName))  /* retval does not include NULL terminator */
            {
               LOG(stderr, CRITICAL_MSG, SUBSYS_NONE, "Internal variable \"pszPropertyName\" too short.\n"
                   "This array needs to be at least %d bytes in length.\n",
                   nRetVal+1);
               fclose(fpConfigFile);
               fpConfigFile = NULL;
               return; /* Just return */
            }
   
            nRetVal= snprintf(szPropertyVal, 
                              sizeof(szPropertyVal), 
                              strtok_r(NULL, (const char *) szTokenDelim, &strtok_ptr));
                              
            if ((nRetVal+1) > sizeof(szPropertyVal))  /* retval does not include NULL terminator */
            {
               LOG(stderr, CRITICAL_MSG, SUBSYS_NONE, "Internal variable \"pszPropertyVal\" too short.\n"
                   "This array needs to be at least %d bytes in length.\n",
                   nRetVal+1);
               fclose(fpConfigFile);
               fpConfigFile = NULL;
               return; /* Just return */
            }
            
            /* Check to see if this is the property we want */
            if(strcmp(szPropertyName, LOG_LEVEL_PROPERTY_NAME) == 0)
            {
               if(atoi(szPropertyVal) != 0)
               {
                  set_log_level(atoi(szPropertyVal));
               }
               else
               {
                  LOG(stderr, WARNING_MSG, SUBSYS_NONE, "The logging level cannot be set to zero");
               }
            }
         } /* end while(fgets...) */
         
         /* Exhausted all entries in the config file */
         fclose(fpConfigFile);
         fpConfigFile = NULL;
      }
      else
      {
         LOG(stderr, CRITICAL_MSG, SUBSYS_NONE, "Unable to open the config file [%s].\n"
             "\terrno     : [%d]\n\terrno msg : [%s]\n",
             pszConfigFile,
             errno,
             strerror(errno));
      }
   }
   else
   {
      LOG(stderr, CRITICAL_MSG, SUBSYS_NONE, "Invalid pointer! Pointer must not be NULL\n");
   }

}

