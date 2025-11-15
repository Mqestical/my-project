#ifndef SHELL_H
#define SHELL_H

#include <sys/types.h>
#include <signal.h>

typedef enum { RUNNING, STOPPED, DONE } JobStatus;

typedef struct Job {
    pid_t pid;
    int job_id;
    JobStatus status;
    char cmd[256];
    struct Job *next;
} Job;

struct linux_dirent64 {
    unsigned long  d_ino;    
    long           d_off;    
    unsigned short d_reclen; 
    unsigned char  d_type;   
    char           d_name[]; 
};

extern int job_id;
extern volatile sig_atomic_t sigchld_flag;
extern Job *head;

char *shell_execute(const char *input);
char *pwd(void);
char *ls(void);
int ends_with_ampersand(const char *input);
void BG_process(const char *input);
void clock_nsleep(int seconds, long nanoseconds);
void sigchld(int signal);
void sigint(int signal);
void sigtstp(int signal);
void sighandler(int signal);
void add_job(Job **head, const char *cmd);
void remove_done_jobs(Job **head);
void print_jobs(Job *head);
static void trim_whitespace(char *str);
int TAGS(char *input, char *argv[], bool *is_sleep);
char* mkdirCMD(char* dirname, char* output_buf, size_t buf_size);
bool TAGMKDIR(const char *input, char *folder_name, size_t max_len);
char* rmdirCMD(char* dirname, char* output_buf, size_t buf_size);
bool TAGMKDIR(const char *input, char *folder_name, size_t max_len);
bool TAGRMDIR(const char *input, char *folder_name, size_t max_len);
void fg_job(int job_id);
void bg_job(int job_id);
bool parse_job_command(const char *input, char *cmd, int *job_id);
char* cdCMD(char* dirname, char* output_buf, size_t buf_size);
bool TAGCD(const char *input, char *dir_name, size_t max_len);
char* echoCMD(const char* text, char* output_buf, size_t buf_size);
bool TAGECHO(const char *input, char *text, size_t max_len);
char* catCMD(const char* filename, char* output_buf, size_t buf_size);
bool TAGCAT(const char *input, char *filename, size_t max_len);
char* touchCMD(const char* filename, char* output_buf, size_t buf_size);
char* cpCMD(const char* src, const char* dest, char* output_buf, size_t buf_size);
char* mvCMD(const char* src, const char* dest, char* output_buf, size_t buf_size);
bool TAGTOUCH(const char *input, char *filename, size_t max_len);
bool TAGCP(const char *input, char *src, char *dest, size_t max_len);
bool TAGMV(const char *input, char *src, char *dest, size_t max_len);
char* grepCMD(const char* pattern, const char* filename, char* output_buf, size_t buf_size);
char* findCMD(const char* name, char* output_buf, size_t buf_size);
char* chmodCMD(const char* mode_str, const char* filename, char* output_buf, size_t buf_size);
bool TAGGREP(const char *input, char *pattern, char *filename, size_t max_len);
bool TAGFIND(const char *input, char *name, size_t max_len);
bool TAGCHMOD(const char *input, char *mode, char *filename, size_t max_len);
char* chownCMD(const char* owner, const char* filename, char* output_buf, size_t buf_size);
char* psCMD(char* output_buf, size_t buf_size);
char* topCMD(char* output_buf, size_t buf_size);
bool TAGCHOWN(const char *input, char *owner, char *filename, size_t max_len);
#endif