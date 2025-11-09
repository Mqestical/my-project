#ifndef SHELL_H
#define SHELL_H
#include <sys/types.h>
#include <signal.h>

#define MAX_JOB_COUNT 100

typedef enum { RUNNING, STOPPED, DONE } job_status_t;

typedef struct {
    pid_t pid;
    char cmd[256];
    job_status_t status;
    int job_num;
} job_t;

struct linux_dirent64 {
    unsigned long  d_ino;    
    long           d_off;    
    unsigned short d_reclen; 
    unsigned char  d_type;   
    char           d_name[]; 
};

extern job_t jobs[MAX_JOB_COUNT];
extern int job_count;
extern volatile sig_atomic_t sigchld_flag;

char *shell_execute(const char *input);
char *pwd(void);
char *ls(void);
int ends_with_ampersand(char *input);
void BG_process(char *ampsand_input);
void clock_nsleep(int seconds, long nanoseconds);
void sigchld(int signal);
void sigint(int signal);
void sigtstp(int signal);
void sighandler(int signal);

#endif
