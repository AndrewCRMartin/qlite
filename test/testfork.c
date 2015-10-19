#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>

extern char **environ;



/* Returns 9 if it ran out of time
           otherwise the exit status of the command run
*/

int system_tl (char *command, int timelimit) 
{
   int pid, pidtimer, status;

   if (command == NULL)
      return(1);

   /* Fork off a new process */
   pid = fork();
   if (pid == -1)
      return(-1);
   
   /* If it's the new process, then run the command */
   if (pid == 0) 
   {
      char *argv[4];
      argv[0] = "sh";
      argv[1] = "-c";
      argv[2] = command;
      argv[3] = 0;
      
      execve("/bin/sh", argv, environ);
      
      exit(127);
   }

   /* It's the parent, so pid is the PID of the child */

   if(timelimit != 0)
   {
      /* Fork off another process to wait for the specified timelimit */
      pidtimer = fork();
      if (pidtimer == -1)
         return(-1);
      
      /* If it's the child start the timer                           */
      if(pidtimer == 0)
      {
         sleep(timelimit);
         kill(pid, 9);
         exit(0);
      }
   }
   
   /* It's the parent, so wait for the children to finish         */
   for(;;)
   {
      if(waitpid(pid, &status, 0) == -1) 
      {
         if (errno != EINTR)
            return(-1);
      }
      else
      {
         /* WIFEXITED() tells us whether the child exited normally.
            If not then it was killed by our timer process, so
            we return(9)

            WEXITSTATUS() extracts the exit status as an unsigned
            value, the cast to char and then to int turns it back
            into a signed int < 256
         */
         if(WIFEXITED(status))
         {
            return ((int)(char)WEXITSTATUS(status));
         }
         else
         {
            return(9);
         }
      }
   }

   return(0);
}



/**********************************************************************/

#ifdef DEMO
#include <stdio.h>

int main(int argc, char **argv)
{
   int status;
   
   status = system_tl("/home/andrew/cprogs/qlite/slowprog", 10);

   printf("Status: %d\n", status);
   
   return(0);
}
#endif

