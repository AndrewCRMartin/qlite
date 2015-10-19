#define SLEEP

int main(int argc, char **argv)
{
#ifdef SLEEP
   sleep(60);
#else
   int i, j, k;

   for(;;)
   {
      for(i=0; i<1000000; i++)
      {
         for(j=1; j<1000000; j++)
         {
            k = i/j;
         }
      }
   }
#endif

   return(0);
}
