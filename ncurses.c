#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include "UN.h"
#include "shell.h"

char *shell_execute(const char *input);
char result[8192];
int main() {
    char username[255];
    strcpy(username, getun());

    initscr();
    cbreak();

    printw("                             WELCOME %s\n\n", username);
    printw("                            MXJESTICAL SHELL\n\n");
    printw("                        GNU GENERAL PUBLIC LICENSE 3\n\n");

    char input[256];

    while (1) {
        printw("\n<%s>: ", username);
        refresh();
        getstr(input);

        char *output = shell_execute(input);

        printw("%s", output);
        refresh();
    }
    getnstr(input, sizeof(input)-1);
    return 0;
}

char *shell_execute(const char *input) {
    result[0] = '\0';
    
    if(ends_with_ampersand(input)) {

        BG_process(input);
    }

if (sigchld_flag) {
    int status;
    pid_t w;
    while ((w = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        for (int i = 0; i < job_count; i++) {
            if (jobs[i].pid == w) {
                if (WIFEXITED(status) || WIFSIGNALED(status)) {
                    jobs[i].status = DONE;
                    printw("[%d]+ Done %s\n", jobs[i].job_num, jobs[i].cmd);
                } else if (WIFSTOPPED(status)) {
                    jobs[i].status = STOPPED;
                    printw("[%d]+ Stopped %s\n", jobs[i].job_num, jobs[i].cmd);
                } else if (WIFCONTINUED(status)) {
                    jobs[i].status = RUNNING;
                }
            }
        }
    }
    sigchld_flag = 0;
}


    if (strcmp(input, "pwd") == 0) {
        strcpy(result, pwd());
        return result;
    }

    if (sigchld_flag) {
    int status;
    pid_t w;
    while ((w = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        for (int i = 0; i < job_count; i++) {
            if (jobs[i].pid == w) {
                if (WIFEXITED(status) || WIFSIGNALED(status)) {
                    jobs[i].status = DONE;
                    printw("[%d]+ Done %s\n", jobs[i].job_num, jobs[i].cmd);
                } else if (WIFSTOPPED(status)) {
                    jobs[i].status = STOPPED;
                    printw("[%d]+ Stopped %s\n", jobs[i].job_num, jobs[i].cmd);
                } else if (WIFCONTINUED(status)) {
                    jobs[i].status = RUNNING;
                }
            }
        }
    }
    sigchld_flag = 0;
}


    if (strcmp(input, "ls") == 0) {
        strcpy(result, ls());
        return result;
    }

    if (sigchld_flag) {
    int status;
    pid_t w;
    while ((w = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        for (int i = 0; i < job_count; i++) {
            if (jobs[i].pid == w) {
                if (WIFEXITED(status) || WIFSIGNALED(status)) {
                    jobs[i].status = DONE;
                    printw("[%d]+ Done %s\n", jobs[i].job_num, jobs[i].cmd);
                } else if (WIFSTOPPED(status)) {
                    jobs[i].status = STOPPED;
                    printw("[%d]+ Stopped %s\n", jobs[i].job_num, jobs[i].cmd);
                } else if (WIFCONTINUED(status)) {
                    jobs[i].status = RUNNING;
                }
            }
        }
    }
    sigchld_flag = 0;
}

    if (strcmp(input, "sleep") == 0) {
        
    }

    if (strcmp(input, "exit") == 0) {
        endwin(); 
        exit(0);
    }

    return "command not found\n";
}
