#define _GNU_SOURCE
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <ncurses.h>
#include <signal.h>
#include <stdbool.h>
#include "shell.h"

#define MAX_TOKENS 10
#define MAX_LEN 255

int job_id = 0;
volatile sig_atomic_t sigchld_flag = 0;
Job *head = NULL;
// The permission mode for the new directory.
    // 0755 means:
    // User (owner): read, write, execute (rwx)
    // Group: read, execute (r-x)
    // Others: read, execute (r-x)
    // 0755 RWXRWXRWX Permissions for MKDIR.
    // 0755 RWXRWXRWX Permissions for MKDIR.
mode_t mode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH; 
static char output[8192];

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

// ----- Internal command: pwd -----
char *pwd(void) {
    static char buf[512];
    if (getcwd(buf, sizeof(buf)) == NULL) {
        snprintf(output, sizeof(output), "error: getcwd failed\n");
        return output;
    }
    snprintf(output, sizeof(output), "%s\n", buf);
    return output;
}

// ----- Internal command: ls -----
char *ls(void) {
    char cwd[512];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        snprintf(output, sizeof(output), "error: getcwd failed\n");
        return output;
    }

    int dirfd = open(cwd, O_RDONLY | O_DIRECTORY);
    if (dirfd == -1) {
        snprintf(output, sizeof(output), "error: open failed\n");
        return output;
    }

    char buf[8192];
    int nread = syscall(SYS_getdents64, dirfd, buf, sizeof(buf));
    if (nread == -1) {
        snprintf(output, sizeof(output), "error: getdents64 failed\n");
        close(dirfd);
        return output;
    }

    output[0] = '\0';
    for (int bpos = 0; bpos < nread;) {
        struct linux_dirent64 *d = (struct linux_dirent64 *)(buf + bpos);
        strcat(output, d->d_name);
        strcat(output, "\n");
        bpos += d->d_reclen;
    }

    close(dirfd);
    remove_done_jobs(&head);
    return output;
}

// ------------------------- Internal Command: MaKeDIRectory CMD -----------------------------
char* mkdirCMD(char* dirname, char* output_buf, size_t buf_size) {
    int ret = syscall(SYS_mkdirat, AT_FDCWD, dirname, mode);

    if (ret == 0) {
        snprintf(output_buf, buf_size, 
                 "Directory '%s' created with mode 0755 (rwxr-xr-x).\n", dirname);
    } else {
        if (errno == EEXIST) {
            snprintf(output_buf, buf_size, 
                     "Error: Directory '%s' already exists.\n", dirname);
        } else if (errno == ENOENT) {
            snprintf(output_buf, buf_size, 
                     "Error: Parent directory does not exist for '%s'.\n", dirname);
        } else {
            snprintf(output_buf, buf_size, 
                     "mkdir error: %s\n", strerror(errno));
        }
    }

    return output_buf;

}

// ------------------------- Internal Command: ReMoveDirectorY CMD -----------------------------
char* rmdirCMD(char* dirname, char* output_buf, size_t buf_size) {
    int ret = syscall(SYS_unlinkat, AT_FDCWD, dirname, AT_REMOVEDIR);

    if (ret == 0) {
        snprintf(output_buf, buf_size, 
                 "Directory '%s' removed successfully.\n", dirname);
    } else {
        if (errno == ENOENT) {
            snprintf(output_buf, buf_size, 
                     "Error: Directory '%s' does not exist.\n", dirname);
        } else if (errno == ENOTEMPTY) {
            snprintf(output_buf, buf_size, 
                     "Error: Directory '%s' is not empty.\n", dirname);
        } else if (errno == ENOTDIR) {
            snprintf(output_buf, buf_size, 
                     "Error: '%s' is not a directory.\n", dirname);
        } else {
            snprintf(output_buf, buf_size, 
                     "rmdir error: %s\n", strerror(errno));
        }
    }

    return output_buf;
}

// ----- Check if input ends with & -----
int ends_with_ampersand(const char *input) {
    int len = strlen(input);
    if (len == 0) return 0;

    int i = len - 1;
    while (i >= 0 && isspace((unsigned char)input[i]))
        i--;

    if (i < 0) return 0;
    return input[i] == '&';
}

// ----- Sleep helper -----
void clock_nsleep(int seconds, long nanoseconds) {
    struct timespec ts = {seconds, nanoseconds};
    long ret = syscall(SYS_clock_nanosleep, CLOCK_REALTIME, 0, &ts, NULL);
    if (ret != 0) perror("clock_nanosleep");
}

// ----- Job management -----
void add_job(Job **head, const char *cmd) {
    job_id++;
    Job *new_job = malloc(sizeof(Job));
    new_job->job_id = job_id;
    new_job->status = RUNNING;
    new_job->pid = 0; // will set after fork
    strncpy(new_job->cmd, cmd, 255);
    new_job->cmd[255] = '\0';
    new_job->next = NULL;

    if (*head == NULL) {
        *head = new_job;
    } else {
        Job *temp = *head;
        while (temp->next != NULL)
            temp = temp->next;
        temp->next = new_job;
    }
}

void remove_done_jobs(Job **head) {
    Job *current = *head;
    Job *prev = NULL;

    while (current != NULL) {
        if (current->status == DONE) {
            if (prev == NULL)
                *head = current->next;
            else
                prev->next = current->next;

            Job *to_free = current;
            current = current->next;
            free(to_free);
        } else {
            prev = current;
            current = current->next;
        }
    }
}

void print_jobs(Job *head) {
    Job *temp = head;
    while (temp != NULL) {
        printw("[%d] %s (%s)\n", temp->job_id, temp->cmd,
               temp->status == RUNNING ? "RUNNING" :
               temp->status == STOPPED ? "STOPPED" : "DONE");
        temp = temp->next;
        refresh();
    }
}

// ----- Signal handling -----
void handle_sigchld() {
    int status;
    pid_t w;

    while ((w = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        Job *temp = head;
        while (temp != NULL) {
            if (temp->pid == w) {
                if (WIFEXITED(status) || WIFSIGNALED(status))
                    temp->status = DONE;
                else if (WIFSTOPPED(status))
                    temp->status = STOPPED;
                else if (WIFCONTINUED(status))
                    temp->status = RUNNING;
            }
            temp = temp->next;
        }
    }
    sigchld_flag = 0;
}

void sigchld(int signal) { sigchld_flag = 1; }
void sigint(int signal) { write(STDOUT_FILENO, "Ctrl+C detected\n", 16); }
void sigtstp(int signal) { write(STDOUT_FILENO, "Ctrl+Z detected\n", 16); }

void sighandler(int signal) {
    struct sigaction sa;

    sa.sa_handler = &sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, NULL);

    sa.sa_handler = &sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    sa.sa_handler = &sigtstp;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTSTP, &sa, NULL);
}

// ----- Background process -----
// ----- Background process -----
void BG_process(const char *input) {
    if (input == NULL || strlen(input) == 0) return;

    char cmd[256];
    strncpy(cmd, input, 255);
    cmd[255] = '\0';

    int len = strlen(cmd);
    if (len > 0 && cmd[len - 1] == '&') cmd[len - 1] = '\0';
    trim_whitespace(cmd);
    if (strlen(cmd) == 0) return;

    add_job(&head, cmd);
    Job *new_job = head;
    while (new_job->next != NULL) new_job = new_job->next;

    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        // child process
        close(pipefd[0]); // close read end
        int seconds;
        char *argv[32];
        bool is_sleep;
        char folder[256];
        char mkdir_op[256];
        char cmd_copy[256];
        strncpy(cmd_copy, cmd, sizeof(cmd_copy) - 1);
        cmd_copy[255] = '\0';
        
 char *out = NULL;

if (TAGMKDIR(cmd_copy, folder, sizeof(folder))) {
    out = mkdirCMD(folder, mkdir_op, sizeof(mkdir_op));
}
else if (TAGRMDIR(cmd_copy, folder, sizeof(folder))) {
    out = rmdirCMD(folder, mkdir_op, sizeof(mkdir_op));  // reuse mkdir_op buffer
}
else {
    seconds = TAGS(cmd, argv, &is_sleep);
    
    if (is_sleep) clock_nsleep(seconds, 0);
    else if (strcmp(cmd, "ls") == 0) out = ls();
    else if (strcmp(cmd, "pwd") == 0) out = pwd();
    else if (strcmp(cmd, "joblist") == 0) _exit(0);
    else out = "command not found\n";
}

        if (out) write(pipefd[1], out, strlen(out));
        close(pipefd[1]);
        _exit(0);
    } else {
    // parent process
    new_job->pid = pid;
    printw("[%d] %d\n", new_job->job_id, pid);
    refresh();

    close(pipefd[1]); // close write end
    
    // Set pipe to non-blocking mode
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);
    
    // Try to read any immediate output
    char buf[8192];
    int n = read(pipefd[0], buf, sizeof(buf)-1);
    if (n > 0) {
        buf[n] = '\0';
        printw("%s", buf);
        refresh();
    }
    close(pipefd[0]);
}
}
// ----- Tokenize And Get Sleep (TAGS) -----
int TAGS(char *input, char *argv[], bool *is_sleep) {
    int argc = 0;
    char *token = strtok(input, " ");
    while (token != NULL && argc < 31) {
        argv[argc++] = token;
        token = strtok(NULL, " ");
    }
    argv[argc] = NULL;

    *is_sleep = false;

    if (argc == 2 && strcmp(argv[0], "sleep") == 0) {
        char *numstr = argv[1];
        for (int i = 0; numstr[i]; i++) {
            if (!isdigit((unsigned char)numstr[i])) return 0;
        }
        *is_sleep = true;
        return atoi(argv[1]);
    }

    return 0;
}

static void trim_whitespace(char *str) {
    char *end;

    // Trim leading spaces
    while (isspace((unsigned char)*str)) str++;

    if (*str == 0) return; // all spaces

    // Trim trailing spaces
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;

    *(end + 1) = '\0';
}

bool TAGMKDIR(const char *input, char *folder_name, size_t max_len) { // Tokenize And Get MaKe DIRectory.
    if (input == NULL || folder_name == NULL) return false;

    char copy[256];
    strncpy(copy, input, sizeof(copy) - 1);
    copy[sizeof(copy)-1] = '\0';

    // tokenize
    char *token = strtok(copy, " \t\n");
    if (token == NULL || strcmp(token, "mkdir") != 0) return false;

    token = strtok(NULL, " \t\n"); // next token is folder name
    if (token == NULL) return false;

    // copy folder name to output
    strncpy(folder_name, token, max_len - 1);
    folder_name[MAX_LEN - 1] = '\0';
    return true;
}

bool TAGRMDIR(const char *input, char *folder_name, size_t max_len) {
    if (input == NULL || folder_name == NULL) return false;

    char copy[256];
    strncpy(copy, input, sizeof(copy) - 1);
    copy[sizeof(copy)-1] = '\0';

    char *token = strtok(copy, " \t\n");
    if (token == NULL || strcmp(token, "rmdir") != 0) return false;

    token = strtok(NULL, " \t\n");
    if (token == NULL) return false;

    strncpy(folder_name, token, max_len - 1);
    folder_name[max_len - 1] = '\0';
    return true;
}
