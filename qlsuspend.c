/*************************************************************************

   Program:    qlsuspend
   File:       qlsuspend.c
   
   Version:    V1.0
   Date:       18.09.00
   Function:   Suspends (or restarts) all the QLite daemons
   
   Copyright:  (c) University of Reading / Dr. Andrew C. R. Martin 2000
   Author:     Dr. Andrew C. R. Martin
   Address:    School of Animal and Microbial Sciences,
               The University of Reading,
               Whiteknights,
               P.O. Box 228,
               Reading RG6 6AJ.
               England.
   Phone:      +44 (0)118 987 5123 Extn. 7022
   Fax:        +44 (0)118 931 0180
   EMail:      andrew@bioinf.org.uk
               andrew@stagleys.demon.co.uk
               
**************************************************************************

   This program is not in the public domain.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

   See the file COPYING.DOC for details of what you may and may not
   do with this program.

   In particular, you may not distribute this code without express
   permission from the author; it must be obtained directly from the 
   author.

**************************************************************************

   Description:
   ============

**************************************************************************

   Usage:
   ======

**************************************************************************

   Revision History:
   =================

*************************************************************************/
/* Includes
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __linux__
#  include <linux/limits.h>
#else
#  include <limits.h>
#endif
#include "qlutil.h"

/************************************************************************/
/* Defines and macros
*/
#define MAXBUFF 160

/************************************************************************/
/* Globals
*/

/************************************************************************/
/* Prototypes
*/
int  main(int argc, char **argv);
BOOL ParseCmdLine(int argc, char **argv, char *spoolDir, BOOL *resume);
BOOL CreateSuspendFile(char *spoolDir);
BOOL DeleteSuspendFile(char *spoolDir);
void Usage(void);


/************************************************************************/
/*>int main(int argc, char **argv)
   -------------------------------
   Input:     
   Output:    
   Returns:   

   Main program for the qlrun daemon

   15.09.00 Original  By: ACRM
*/
int main(int argc, char **argv)
{
   char spoolDir[PATH_MAX],
        *env;
   BOOL resume = FALSE;
   

   /* Get the default spool directory from the environment variable if
      this has been set
   */
   if((env=getenv("QLSPOOLDIR"))!=NULL)
      strcpy(spoolDir, env);
   else
      strcpy(spoolDir, DEF_SPOOLDIR);

   if(ParseCmdLine(argc, argv, spoolDir, &resume))
   {
#ifdef ROOT_ONLY
      if(!RootUser())
      {
         fprintf(stderr,"qlsuspend must be run as root\n");
         return(1);
      }
#endif

      if(!CheckForSpoolDir(spoolDir))
      {
         fprintf(stderr,"Spool directory, %s, does not exist!\n",
                 spoolDir);
         return(1);
      }
         
      if(resume)
      {
         if(!DeleteSuspendFile(spoolDir))
         {
            fprintf(stderr,"Unable to delete suspend file.\n");
            return(1);
         }
      }
      else
      {
         if(!CreateSuspendFile(spoolDir))
         {
            fprintf(stderr,"Unable to create suspend file.\n");
            return(1);
         }
      }
   }
   else
   {
      Usage();
   }
   return(0);
}

/************************************************************************/
/*>BOOL ParseCmdLine(int argc, char **argv, char *spoolDir, BOOL *resume)
   --------------------------------------------------------------------
   Input:     int    argc        Argument count
              char   *argv       Arguments
   Output:    char   *spoolDir   Spool directory
              BOOL   *resume     Resume execution rather than suspen
   Returns:   BOOL               Success

   Parses the command line

   15.09.00 Original  By: ACRM
*/
BOOL ParseCmdLine(int argc, char **argv, char *spoolDir, BOOL *resume)
{
   argc--;
   argv++;

   while(argc)
   {
      if(argv[0][0] == '-')
      {
         switch(argv[0][1])
         {
         case 's':
            argv++;
            argc--;
            if(!argc)
               return(FALSE);
            strncpy(spoolDir,argv[0],MAXBUFF);
            break;
         case 'r':
            *resume = TRUE;
            break;
         default:
            return(FALSE);
            break;
         }
      }
      else
      {
         if(argc>0)
            return(FALSE);
      }
      
      argc--;
      argv++;
   }
   
   return(TRUE);

}

/************************************************************************/
/*>BOOL CreateSuspendFile(char *spoolDir)
   --------------------------------------
   Input:   char   *spoolDir    Spool directory
   Returns: BOOL                Created OK?

   Creates a file which indicates to the qlrun daemons should be suspended

   18.09.00 Original   By: ACRM
*/
BOOL CreateSuspendFile(char *spoolDir)
{
   char buffer[PATH_MAX];
   FILE *fp;
   
   /* Create the name of the file which indicates this daemon to be
      shutdown
   */
   sprintf(buffer,"%s/.qlsuspend", spoolDir);

   /* Create the file                                                   */
   if((fp=fopen(buffer, "w"))==NULL)
      return(FALSE);
   fclose(fp);

   return(TRUE);
}


/************************************************************************/
/*>BOOL DeleteSuspendFile(char *spoolDir)
   --------------------------------------
   Input:   char   *spoolDir    Spool directory
   Returns: BOOL                Created OK?

   Deletes a file which indicates to the qlrun daemons should be suspended

   18.09.00 Original   By: ACRM
*/
BOOL DeleteSuspendFile(char *spoolDir)
{
   char buffer[PATH_MAX];
   
   /* Create the name of the file which indicates this daemon to be
      shutdown
   */
   sprintf(buffer,"%s/.qlsuspend", spoolDir);

   /* Create the file                                                   */
   if(unlink(buffer))
      return(FALSE);

   return(TRUE);
}


/************************************************************************/
/*>void Usage(void)
   ----------------
   Prints a usage message

   03.10.00 Original  By: ACRM
*/
void Usage(void)
{
   fprintf(stderr, "\nqlsuspend V1.0 (c) 2000 University of Reading, \
Dr. Andrew C.R. Martin\n");

   fprintf(stderr, "\nUsage: qlsuspend [-s spooldir] [-r]\n");
   fprintf(stderr, "       -r Resume execution of qlrun daemons\n");

   fprintf(stderr,"\nqlsuspend creates a flag file to tell the daemon \
on all nodes to suspend.\n\n");
}

