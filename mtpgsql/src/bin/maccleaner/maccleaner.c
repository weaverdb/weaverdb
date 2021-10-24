/*

this should clean shared memory on mac systems

*/
#include <unistd.h>
#include <stdio.h>
#include <sys/shm.h>
#include <sys/semaphore.h>

int
main(int argc, char *argv[])
{
    int groupcount = 0;
    sem_t*	item;
            
    for (groupcount=0;groupcount<=256;groupcount++)
    {
        char	name[32];

        snprintf(name,sizeof(name),"gate%i",groupcount);
        item = sem_open(name,0);
        if ( item != SEM_FAILED ) {
            if ( sem_close(item) != 0 ) {
                perror("gates:");
            } else {
                sem_close(item);
                sem_unlink(name);
            }
            printf("cleaned out gate%i\n",groupcount);
        }
    }
    item = sem_open("pipeline",0);
    if ( item != SEM_FAILED ) {
        if ( sem_close(item) != 0 ) {
            perror("pipeline:");
        } else {
            sem_unlink("pipeline");
            printf("cleaned out pipeline\n");
        }
    }
/*  clean shared memory  */
    {
        int port = 5432;
        key_t	id = 0;
        int shmid = 0;
        int segs = 0;
        
        for (segs = 0;segs < 1000;segs++) {
            id = port * 1000 + segs;
            shmid = shmget(id,0,0);
            if ( shmid != -1 ) {
                shmctl(shmid,IPC_RMID,(struct shmid_ds *)NULL);
                printf("cleaned shared memory id=%i\n",id);
            } else {
/*                printf("no shared memory for id=%i\n",id);   */
            }
        }
    }
}
