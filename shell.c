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
char owner[256];
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
void fg_job(int job_id);
void bg_job(int job_id);
bool parse_job_command(const char *input, char *cmd, int *job_id);
bool TAGCD(const char *input, char *dir_name, size_t max_len);
char* cdCMD(char* dirname, char* output_buf, size_t buf_size);
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

// ------------------------- Internal Command: Change Directory -----------------------------
char* cdCMD(char* dirname, char* output_buf, size_t buf_size) {
    int ret = syscall(SYS_chdir, dirname);

    if (ret == 0) {
        snprintf(output_buf, buf_size, "Changed directory to '%s'\n", dirname);
    } else {
        if (errno == ENOENT) {
            snprintf(output_buf, buf_size, 
                     "Error: Directory '%s' does not exist.\n", dirname);
        } else if (errno == ENOTDIR) {
            snprintf(output_buf, buf_size, 
                     "Error: '%s' is not a directory.\n", dirname);
        } else if (errno == EACCES) {
            snprintf(output_buf, buf_size, 
                     "Error: Permission denied for '%s'.\n", dirname);
        } else {
            snprintf(output_buf, buf_size, 
                     "cd error: %s\n", strerror(errno));
        }
    }

    return output_buf;
}

char* echoCMD(const char* text, char* output_buf, size_t buf_size) {
    snprintf(output_buf, buf_size, "%s\n", text);
    return output_buf;
}

// ------------------------- Internal Command: Cat -----------------------------
char* catCMD(const char* filename, char* output_buf, size_t buf_size) {
    int fd = syscall(SYS_openat, AT_FDCWD, filename, O_RDONLY);
    
    if (fd == -1) {
        if (errno == ENOENT) {
            snprintf(output_buf, buf_size, "cat: %s: No such file or directory\n", filename);
        } else if (errno == EACCES) {
            snprintf(output_buf, buf_size, "cat: %s: Permission denied\n", filename);
        } else {
            snprintf(output_buf, buf_size, "cat: %s: %s\n", filename, strerror(errno));
        }
        return output_buf;
    }
    
    char temp_buf[4096];
    int total_read = 0;
    output_buf[0] = '\0';
    
    while (1) {
        int n = syscall(SYS_read, fd, temp_buf, sizeof(temp_buf) - 1);
        if (n <= 0) break;
        
        temp_buf[n] = '\0';
        
        // Check if we have space left
        if (total_read + n < buf_size - 1) {
            strcat(output_buf, temp_buf);
            total_read += n;
        } else {
            break; // Buffer full
        }
    }
    
    syscall(SYS_close, fd);
    return output_buf;
}

// ------------------------- Internal Command: Touch -----------------------------
char* touchCMD(const char* filename, char* output_buf, size_t buf_size) {
    // Try to open file, create if doesn't exist
    int fd = syscall(SYS_openat, AT_FDCWD, filename, O_WRONLY | O_CREAT | O_NOCTTY | O_NONBLOCK, 0644);
    
    if (fd == -1) {
        snprintf(output_buf, buf_size, "touch: cannot touch '%s': %s\n", filename, strerror(errno));
        return output_buf;
    }
    
    // Update timestamp
    syscall(SYS_close, fd);
    
    struct timespec times[2];
    syscall(SYS_clock_gettime, CLOCK_REALTIME, &times[0]);
    times[1] = times[0];
    
    int ret = syscall(SYS_utimensat, AT_FDCWD, filename, times, 0);
    if (ret == 0) {
        snprintf(output_buf, buf_size, "");  // Success, no output
    } else {
        snprintf(output_buf, buf_size, "touch: cannot touch '%s': %s\n", filename, strerror(errno));
    }
    
    return output_buf;
}

// ------------------------- Internal Command: Copy -----------------------------
char* cpCMD(const char* src, const char* dest, char* output_buf, size_t buf_size) {
    int src_fd = syscall(SYS_openat, AT_FDCWD, src, O_RDONLY);
    if (src_fd == -1) {
        snprintf(output_buf, buf_size, "cp: cannot open '%s': %s\n", src, strerror(errno));
        return output_buf;
    }
    
    int dest_fd = syscall(SYS_openat, AT_FDCWD, dest, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dest_fd == -1) {
        snprintf(output_buf, buf_size, "cp: cannot create '%s': %s\n", dest, strerror(errno));
        syscall(SYS_close, src_fd);
        return output_buf;
    }
    
    char buffer[4096];
    int n;
    while ((n = syscall(SYS_read, src_fd, buffer, sizeof(buffer))) > 0) {
        if (syscall(SYS_write, dest_fd, buffer, n) != n) {
            snprintf(output_buf, buf_size, "cp: error writing to '%s': %s\n", dest, strerror(errno));
            syscall(SYS_close, src_fd);
            syscall(SYS_close, dest_fd);
            return output_buf;
        }
    }
    
    syscall(SYS_close, src_fd);
    syscall(SYS_close, dest_fd);
    
    if (n == -1) {
        snprintf(output_buf, buf_size, "cp: error reading '%s': %s\n", src, strerror(errno));
    } else {
        snprintf(output_buf, buf_size, "");  // Success
    }
    
    return output_buf;
}

// ------------------------- Internal Command: Move -----------------------------
char* mvCMD(const char* src, const char* dest, char* output_buf, size_t buf_size) {
    int ret = syscall(SYS_renameat, AT_FDCWD, src, AT_FDCWD, dest);
    
    if (ret == 0) {
        snprintf(output_buf, buf_size, "");  // Success
    } else {
        if (errno == EXDEV) {
            // Cross-device, need to copy then delete
            char* cp_result = cpCMD(src, dest, output_buf, buf_size);
            if (strlen(cp_result) == 0) {  // Copy succeeded
                syscall(SYS_unlinkat, AT_FDCWD, src, 0);
            }
            return cp_result;
        } else {
            snprintf(output_buf, buf_size, "mv: cannot move '%s' to '%s': %s\n", 
                     src, dest, strerror(errno));
        }
    }
    
    return output_buf;
}

// ------------------------- Internal Command: Grep -----------------------------
char* grepCMD(const char* pattern, const char* filename, char* output_buf, size_t buf_size) {
    int fd = syscall(SYS_openat, AT_FDCWD, filename, O_RDONLY);
    
    if (fd == -1) {
        snprintf(output_buf, buf_size, "grep: %s: %s\n", filename, strerror(errno));
        return output_buf;
    }
    
    char file_buf[8192];
    int n = syscall(SYS_read, fd, file_buf, sizeof(file_buf) - 1);
    syscall(SYS_close, fd);
    
    if (n <= 0) {
        output_buf[0] = '\0';
        return output_buf;
    }
    
    file_buf[n] = '\0';
    output_buf[0] = '\0';
    
    // Make a copy to preserve original for strtok
    char file_copy[8192];
    strncpy(file_copy, file_buf, sizeof(file_copy) - 1);
    file_copy[sizeof(file_copy) - 1] = '\0';
    
    // Simple line-by-line grep
    char *line = strtok(file_copy, "\n");
    while (line != NULL) {
        if (strstr(line, pattern) != NULL) {
            // Check if we have space
            if (strlen(output_buf) + strlen(line) + 2 < buf_size) {
                strcat(output_buf, line);
                strcat(output_buf, "\n");
            }
        }
        line = strtok(NULL, "\n");
    }
    
    if (strlen(output_buf) == 0) {
        snprintf(output_buf, buf_size, "grep: no matches found for '%s'\n", pattern);
    }
    
    return output_buf;
}
// ------------------------- Internal Command: Find -----------------------------
void find_recursive(const char *path, const char *name, char *output_buf, size_t buf_size) {
    int dirfd = syscall(SYS_openat, AT_FDCWD, path, O_RDONLY | O_DIRECTORY);
    if (dirfd == -1) return;
    
    char buf[8192];
    int nread = syscall(SYS_getdents64, dirfd, buf, sizeof(buf));
    
    for (int bpos = 0; bpos < nread;) {
        struct linux_dirent64 *d = (struct linux_dirent64 *)(buf + bpos);
        
        // Skip . and ..
        if (strcmp(d->d_name, ".") != 0 && strcmp(d->d_name, "..") != 0) {
            // Check if name matches
            if (strstr(d->d_name, name) != NULL) {
                char fullpath[512];
                snprintf(fullpath, sizeof(fullpath), "%s/%s\n", path, d->d_name);
                strncat(output_buf, fullpath, buf_size - strlen(output_buf) - 1);
            }
            
            // Recurse into directories
            if (d->d_type == 4) {  // DT_DIR
                char subdir[512];
                snprintf(subdir, sizeof(subdir), "%s/%s", path, d->d_name);
                find_recursive(subdir, name, output_buf, buf_size);
            }
        }
        
        bpos += d->d_reclen;
    }
    
    syscall(SYS_close, dirfd);
}

char* findCMD(const char* name, char* output_buf, size_t buf_size) {
    output_buf[0] = '\0';
    find_recursive(".", name, output_buf, buf_size);
    
    if (strlen(output_buf) == 0) {
        snprintf(output_buf, buf_size, "find: '%s': No matches found\n", name);
    }
    
    return output_buf;
}

// ------------------------- Internal Command: Chmod -----------------------------
char* chmodCMD(const char* mode_str, const char* filename, char* output_buf, size_t buf_size) {
    // Parse octal mode
    mode_t mode = 0;
    for (int i = 0; mode_str[i]; i++) {
        if (!isdigit(mode_str[i])) {
            snprintf(output_buf, buf_size, "chmod: invalid mode: '%s'\n", mode_str);
            return output_buf;
        }
        mode = mode * 8 + (mode_str[i] - '0');
    }
    
    int ret = syscall(SYS_fchmodat, AT_FDCWD, filename, mode, 0);
    
    if (ret == 0) {
        snprintf(output_buf, buf_size, "");  // Success
    } else {
        snprintf(output_buf, buf_size, "chmod: cannot change permissions of '%s': %s\n", 
                 filename, strerror(errno));
    }
    
    return output_buf;
}

// ------------------------- Internal Command: Chown -----------------------------
char* chownCMD(const char* owner, const char* filename, char* output_buf, size_t buf_size) {
    // Parse owner (can be "user" or "user:group")
    char owner_copy[256];
    strncpy(owner_copy, owner, sizeof(owner_copy) - 1);
    owner_copy[sizeof(owner_copy) - 1] = '\0';
    
    uid_t uid = -1;
    gid_t gid = -1;
    
    char *colon = strchr(owner_copy, ':');
    if (colon != NULL) {
        *colon = '\0';
        char *group = colon + 1;
        
        // Try to parse as numeric GID
        gid = atoi(group);
    }
    
    // Try to parse as numeric UID
    uid = atoi(owner_copy);
    
    int ret = syscall(SYS_fchownat, AT_FDCWD, filename, uid, gid, 0);
    
    if (ret == 0) {
        snprintf(output_buf, buf_size, "");  // Success
    } else {
        snprintf(output_buf, buf_size, "chown: cannot change ownership of '%s': %s\n", 
                 filename, strerror(errno));
    }
    
    return output_buf;
}

// ------------------------- Internal Command: PS -----------------------------
char* psCMD(char* output_buf, size_t buf_size) {
    output_buf[0] = '\0';
    
    strcat(output_buf, "PID    COMMAND\n");
    
    // Open /proc directory
    int proc_fd = syscall(SYS_openat, AT_FDCWD, "/proc", O_RDONLY | O_DIRECTORY);
    if (proc_fd == -1) {
        snprintf(output_buf, buf_size, "ps: cannot open /proc\n");
        return output_buf;
    }
    
    char buf[8192];
    int nread = syscall(SYS_getdents64, proc_fd, buf, sizeof(buf));
    
    for (int bpos = 0; bpos < nread;) {
        struct linux_dirent64 *d = (struct linux_dirent64 *)(buf + bpos);
        
        // Check if directory name is a number (PID)
        if (d->d_type == 4 && isdigit(d->d_name[0])) {
            char cmdline_path[512];
            snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%s/cmdline", d->d_name);
            
            int cmd_fd = syscall(SYS_openat, AT_FDCWD, cmdline_path, O_RDONLY);
            if (cmd_fd != -1) {
                char cmdbuf[256];
                int n = syscall(SYS_read, cmd_fd, cmdbuf, sizeof(cmdbuf) - 1);
                syscall(SYS_close, cmd_fd);
                
                if (n > 0) {
                    cmdbuf[n] = '\0';
                    // Replace null bytes with spaces
                    for (int i = 0; i < n; i++) {
                        if (cmdbuf[i] == '\0') cmdbuf[i] = ' ';
                    }
                    
                    char line[512];
                    snprintf(line, sizeof(line), "%-6s %s\n", d->d_name, cmdbuf);
                    
                    if (strlen(output_buf) + strlen(line) < buf_size - 1) {
                        strcat(output_buf, line);
                    }
                }
            }
        }
        
        bpos += d->d_reclen;
    }
    
    syscall(SYS_close, proc_fd);
    return output_buf;
}

// ------------------------- Internal Command: Top -----------------------------
char* topCMD(char* output_buf, size_t buf_size) {
    output_buf[0] = '\0';
    
    // Read /proc/stat for CPU info
    int stat_fd = syscall(SYS_openat, AT_FDCWD, "/proc/stat", O_RDONLY);
    if (stat_fd != -1) {
        char statbuf[1024];
        int n = syscall(SYS_read, stat_fd, statbuf, sizeof(statbuf) - 1);
        syscall(SYS_close, stat_fd);
        
        if (n > 0) {
            statbuf[n] = '\0';
            char *line = strtok(statbuf, "\n");
            if (line != NULL && strncmp(line, "cpu ", 4) == 0) {
                strcat(output_buf, "CPU: ");
                strcat(output_buf, line);
                strcat(output_buf, "\n\n");
            }
        }
    }
    
    // Read /proc/meminfo for memory info
    int mem_fd = syscall(SYS_openat, AT_FDCWD, "/proc/meminfo", O_RDONLY);
    if (mem_fd != -1) {
        char membuf[2048];
        int n = syscall(SYS_read, mem_fd, membuf, sizeof(membuf) - 1);
        syscall(SYS_close, mem_fd);
        
        if (n > 0) {
            membuf[n] = '\0';
            strcat(output_buf, "Memory:\n");
            
            char *line = strtok(membuf, "\n");
            int count = 0;
            while (line != NULL && count < 3) {
                strcat(output_buf, line);
                strcat(output_buf, "\n");
                line = strtok(NULL, "\n");
                count++;
            }
            strcat(output_buf, "\n");
        }
    }
    
    // List top processes
    strcat(output_buf, "PID    CPU%   MEM    COMMAND\n");
    
    int proc_fd = syscall(SYS_openat, AT_FDCWD, "/proc", O_RDONLY | O_DIRECTORY);
    if (proc_fd == -1) {
        return output_buf;
    }
    
    char buf[8192];
    int nread = syscall(SYS_getdents64, proc_fd, buf, sizeof(buf));
    
    int proc_count = 0;
    for (int bpos = 0; bpos < nread && proc_count < 10;) {
        struct linux_dirent64 *d = (struct linux_dirent64 *)(buf + bpos);
        
        if (d->d_type == 4 && isdigit(d->d_name[0])) {
            char stat_path[512];
            snprintf(stat_path, sizeof(stat_path), "/proc/%s/stat", d->d_name);
            
            int stat_fd = syscall(SYS_openat, AT_FDCWD, stat_path, O_RDONLY);
            if (stat_fd != -1) {
                char statbuf[512];
                int n = syscall(SYS_read, stat_fd, statbuf, sizeof(statbuf) - 1);
                syscall(SYS_close, stat_fd);
                
                if (n > 0) {
                    statbuf[n] = '\0';
                    
                    // Parse process name and stats
                    char *paren_start = strchr(statbuf, '(');
                    char *paren_end = strrchr(statbuf, ')');
                    
                    if (paren_start && paren_end) {
                        char procname[256];
                        int name_len = paren_end - paren_start - 1;
                        if (name_len > 255) name_len = 255;
                        strncpy(procname, paren_start + 1, name_len);
                        procname[name_len] = '\0';
                        
                        char line[512];
                        snprintf(line, sizeof(line), "%-6s %-6s %-6s %s\n", 
                                d->d_name, "0.0", "0KB", procname);
                        
                        if (strlen(output_buf) + strlen(line) < buf_size - 1) {
                            strcat(output_buf, line);
                            proc_count++;
                        }
                    }
                }
            }
        }
        
        bpos += d->d_reclen;
    }
    
    syscall(SYS_close, proc_fd);
    
    strcat(output_buf, "\n(Press Ctrl+C to return to shell)\n");
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
    char text[256];
    char filename[256];
    char pattern[256];
    char mode[256];
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
    char cmd_name[256];
    int job_num;
    
    strncpy(cmd_copy, cmd, sizeof(cmd_copy) - 1);
    cmd_copy[255] = '\0';
    
    char *out = NULL;

    // Check for fg/bg commands
    if (parse_job_command(cmd_copy, cmd_name, &job_num)) {
        if (strcmp(cmd_name, "fg") == 0) {
            fg_job(job_num);
            _exit(0);
        }
        if (strcmp(cmd_name, "bg") == 0) {
            bg_job(job_num);
            _exit(0);
        }
    }

    // reset cmd_copy since parse_job_command uses strtok
    strncpy(cmd_copy, cmd, sizeof(cmd_copy) - 1);
    cmd_copy[255] = '\0';

    if (TAGMKDIR(cmd_copy, folder, sizeof(folder))) {
        out = mkdirCMD(folder, mkdir_op, sizeof(mkdir_op));
    }
    else if (TAGRMDIR(cmd_copy, folder, sizeof(folder))) {
        out = rmdirCMD(folder, mkdir_op, sizeof(mkdir_op));
    }

else if (TAGGREP(cmd_copy, pattern, filename, sizeof(pattern))) {
    out = grepCMD(pattern, filename, mkdir_op, sizeof(mkdir_op));
}
else if (TAGFIND(cmd_copy, filename, sizeof(filename))) {
    out = findCMD(filename, mkdir_op, sizeof(mkdir_op));
}
else if (TAGCHMOD(cmd_copy, mode, filename, sizeof(mode))) {
    out = chmodCMD(mode, filename, mkdir_op, sizeof(mkdir_op));
}


else if (TAGCHOWN(cmd_copy, owner, filename, sizeof(owner))) {
    out = chownCMD(owner, filename, mkdir_op, sizeof(mkdir_op));
}
else if (strcmp(cmd, "ps") == 0) {
    out = psCMD(mkdir_op, sizeof(mkdir_op));
}
else if (strcmp(cmd, "top") == 0) {
    out = topCMD(mkdir_op, sizeof(mkdir_op));
}
    strncpy(cmd_copy, cmd, sizeof(cmd_copy) - 1);
cmd_copy[255] = '\0';

if (TAGECHO(cmd_copy, text, sizeof(text))) {
    out = echoCMD(text, mkdir_op, sizeof(mkdir_op));
}
else if (TAGCAT(cmd_copy, filename, sizeof(filename))) {
    out = catCMD(filename, mkdir_op, sizeof(mkdir_op));
}
else if (TAGMKDIR(cmd_copy, folder, sizeof(folder))) {
    out = mkdirCMD(folder, mkdir_op, sizeof(mkdir_op));
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

void fg_job(int job_id) {
    Job *temp = head;
    while (temp != NULL) {
        if (temp->job_id == job_id) {
            printw("DEBUG: Found job %d with PID %d, status %d\n", job_id, temp->pid, temp->status);
            refresh();
            
            if (temp->status == DONE) {
                printw("Job [%d] is already done\n", job_id);
                refresh();
                return;
            }
            
            if (temp->status == STOPPED) {
                kill(temp->pid, SIGCONT);
            }
            
            temp->status = RUNNING;
            printw("[%d] %s continued in foreground\n", temp->job_id, temp->cmd);
            refresh();
            
            int status;
            printw("DEBUG: About to waitpid on %d\n", temp->pid);
            refresh();
            
            pid_t result = waitpid(temp->pid, &status, WUNTRACED);
            
            printw("DEBUG: waitpid returned %d, errno=%d\n", result, errno);
            refresh();
            
            if (result == -1) {
                printw("DEBUG: waitpid error: %s\n", strerror(errno));
                refresh();
            }}
    while (temp != NULL) {
        if (temp->job_id == job_id) {
            if (temp->status == DONE) {
                printw("Job [%d] is already done\n", job_id);
                refresh();
                return;
            }
            
            // Send SIGCONT if stopped
            if (temp->status == STOPPED) {
                kill(temp->pid, SIGCONT);
            }
            
            temp->status = RUNNING;
            printw("[%d] %s continued in foreground\n", temp->job_id, temp->cmd);
            refresh();
            
            // Wait for the process - this should block
            int status;
            while (1) {
                pid_t result = waitpid(temp->pid, &status, WUNTRACED);
                
                if (result == -1) {
                    if (errno == ECHILD) {
                        // Child already exited
                        temp->status = DONE;
                        printw("\n[%d]+ Done %s\n", temp->job_id, temp->cmd);
                    } else {
                        perror("waitpid");
                    }
                    break;
                }
                
                if (WIFEXITED(status) || WIFSIGNALED(status)) {
                    temp->status = DONE;
                    printw("\n[%d]+ Done %s\n", temp->job_id, temp->cmd);
                    break;
                } else if (WIFSTOPPED(status)) {
                    temp->status = STOPPED;
                    printw("\n[%d]+ Stopped %s\n", temp->job_id, temp->cmd);
                    break;
                }
            }
            refresh();
            return;
        }
        temp = temp->next;
    }
    printw("Job [%d] not found\n", job_id);
    refresh();
}
    }
void bg_job(int job_id) {
    Job *temp = head;
    while (temp != NULL) {
        if (temp->job_id == job_id) {
            if (temp->status == STOPPED) {
                // Send SIGCONT to resume in background
                kill(temp->pid, SIGCONT);
                temp->status = RUNNING;
                printw("[%d] %s continued in background\n", temp->job_id, temp->cmd);
                refresh();
                return;
            }
            if (temp->status == RUNNING) {
                printw("Job [%d] is already running\n", job_id);
            } else {
                printw("Job [%d] is done\n", job_id);
            }
            refresh();
            return;
        }
        temp = temp->next;
    }
    printw("Job [%d] not found\n", job_id);
    refresh();
}

bool parse_job_command(const char *input, char *cmd, int *job_id) {
    char copy[256];
    strncpy(copy, input, sizeof(copy) - 1);
    copy[sizeof(copy)-1] = '\0';
    
    char *token = strtok(copy, " \t\n");
    if (token == NULL) return false;
    
    strcpy(cmd, token);
    
    token = strtok(NULL, " \t\n");
    if (token == NULL) return false;
    
    // Check if it's a valid number
    for (int i = 0; token[i]; i++) {
        if (!isdigit((unsigned char)token[i])) return false;
    }
    
    *job_id = atoi(token);
    return true;
}

bool TAGCD(const char *input, char *dir_name, size_t max_len) {
    if (input == NULL || dir_name == NULL) return false;

    char copy[256];
    strncpy(copy, input, sizeof(copy) - 1);
    copy[sizeof(copy)-1] = '\0';

    char *token = strtok(copy, " \t\n");
    if (token == NULL || strcmp(token, "cd") != 0) return false;

    token = strtok(NULL, " \t\n");
    if (token == NULL) {
        // No argument means go to home directory
        const char *home = getenv("HOME");
        if (home) {
            strncpy(dir_name, home, max_len - 1);
            dir_name[max_len - 1] = '\0';
            return true;
        }
        return false;
    }

    strncpy(dir_name, token, max_len - 1);
    dir_name[max_len - 1] = '\0';
    return true;
}

bool TAGECHO(const char *input, char *text, size_t max_len) {
    if (input == NULL || text == NULL) return false;

    char copy[256];
    strncpy(copy, input, sizeof(copy) - 1);
    copy[sizeof(copy)-1] = '\0';

    char *token = strtok(copy, " \t\n");
    if (token == NULL || strcmp(token, "echo") != 0) return false;

    // Get the rest of the line after "echo "
    const char *rest = input + 4; // Skip "echo"
    while (*rest == ' ' || *rest == '\t') rest++; // Skip whitespace
    
    strncpy(text, rest, max_len - 1);
    text[max_len - 1] = '\0';
    return true;
}


bool TAGCAT(const char *input, char *filename, size_t max_len) {
    if (input == NULL || filename == NULL) return false;

    char copy[256];
    strncpy(copy, input, sizeof(copy) - 1);
    copy[sizeof(copy)-1] = '\0';

    char *token = strtok(copy, " \t\n");
    if (token == NULL || strcmp(token, "cat") != 0) return false;

    token = strtok(NULL, " \t\n");
    if (token == NULL) return false;

    strncpy(filename, token, max_len - 1);
    filename[max_len - 1] = '\0';
    return true;
}

bool TAGTOUCH(const char *input, char *filename, size_t max_len) {
    if (input == NULL || filename == NULL) return false;

    char copy[256];
    strncpy(copy, input, sizeof(copy) - 1);
    copy[sizeof(copy)-1] = '\0';

    char *token = strtok(copy, " \t\n");
    if (token == NULL || strcmp(token, "touch") != 0) return false;

    token = strtok(NULL, " \t\n");
    if (token == NULL) return false;

    strncpy(filename, token, max_len - 1);
    filename[max_len - 1] = '\0';
    return true;
}

bool TAGCP(const char *input, char *src, char *dest, size_t max_len) {
    if (input == NULL || src == NULL || dest == NULL) return false;

    char copy[256];
    strncpy(copy, input, sizeof(copy) - 1);
    copy[sizeof(copy)-1] = '\0';

    char *token = strtok(copy, " \t\n");
    if (token == NULL || strcmp(token, "cp") != 0) return false;

    token = strtok(NULL, " \t\n");
    if (token == NULL) return false;
    strncpy(src, token, max_len - 1);
    src[max_len - 1] = '\0';

    token = strtok(NULL, " \t\n");
    if (token == NULL) return false;
    strncpy(dest, token, max_len - 1);
    dest[max_len - 1] = '\0';

    return true;
}

bool TAGMV(const char *input, char *src, char *dest, size_t max_len) {
    if (input == NULL || src == NULL || dest == NULL) return false;

    char copy[256];
    strncpy(copy, input, sizeof(copy) - 1);
    copy[sizeof(copy)-1] = '\0';

    char *token = strtok(copy, " \t\n");
    if (token == NULL || strcmp(token, "mv") != 0) return false;

    token = strtok(NULL, " \t\n");
    if (token == NULL) return false;
    strncpy(src, token, max_len - 1);
    src[max_len - 1] = '\0';

    token = strtok(NULL, " \t\n");
    if (token == NULL) return false;
    strncpy(dest, token, max_len - 1);
    dest[max_len - 1] = '\0';

    return true;
}

bool TAGGREP(const char *input, char *pattern, char *filename, size_t max_len) {
    if (input == NULL || pattern == NULL || filename == NULL) return false;

    char copy[256];
    strncpy(copy, input, sizeof(copy) - 1);
    copy[sizeof(copy)-1] = '\0';

    char *token = strtok(copy, " \t\n");
    if (token == NULL || strcmp(token, "grep") != 0) return false;

    token = strtok(NULL, " \t\n");
    if (token == NULL) return false;
    strncpy(pattern, token, max_len - 1);
    pattern[max_len - 1] = '\0';

    token = strtok(NULL, " \t\n");
    if (token == NULL) return false;
    strncpy(filename, token, max_len - 1);
    filename[max_len - 1] = '\0';

    return true;
}

bool TAGFIND(const char *input, char *name, size_t max_len) {
    if (input == NULL || name == NULL) return false;

    char copy[256];
    strncpy(copy, input, sizeof(copy) - 1);
    copy[sizeof(copy)-1] = '\0';

    char *token = strtok(copy, " \t\n");
    if (token == NULL || strcmp(token, "find") != 0) return false;

    token = strtok(NULL, " \t\n");
    if (token == NULL) return false;

    strncpy(name, token, max_len - 1);
    name[max_len - 1] = '\0';
    return true;
}

bool TAGCHMOD(const char *input, char *mode, char *filename, size_t max_len) {
    if (input == NULL || mode == NULL || filename == NULL) return false;

    char copy[256];
    strncpy(copy, input, sizeof(copy) - 1);
    copy[sizeof(copy)-1] = '\0';

    char *token = strtok(copy, " \t\n");
    if (token == NULL || strcmp(token, "chmod") != 0) return false;

    token = strtok(NULL, " \t\n");
    if (token == NULL) return false;
    strncpy(mode, token, max_len - 1);
    mode[max_len - 1] = '\0';

    token = strtok(NULL, " \t\n");
    if (token == NULL) return false;
    strncpy(filename, token, max_len - 1);
    filename[max_len - 1] = '\0';

    return true;
}

bool TAGCHOWN(const char *input, char *owner, char *filename, size_t max_len) {
    if (input == NULL || owner == NULL || filename == NULL) return false;

    char copy[256];
    strncpy(copy, input, sizeof(copy) - 1);
    copy[sizeof(copy)-1] = '\0';

    char *token = strtok(copy, " \t\n");
    if (token == NULL || strcmp(token, "chown") != 0) return false;

    token = strtok(NULL, " \t\n");
    if (token == NULL) return false;
    strncpy(owner, token, max_len - 1);
    owner[max_len - 1] = '\0';

    token = strtok(NULL, " \t\n");
    if (token == NULL) return false;
    strncpy(filename, token, max_len - 1);
    filename[max_len - 1] = '\0';

    return true;
}