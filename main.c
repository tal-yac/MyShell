#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <wait.h>
#include <string.h>
#include <fcntl.h>
#include "LineParser.h"

#define INPUT_MAX 2048
#define HISTORY_SIZE 10
#define TERMINATED  -1
#define RUNNING 1
#define SUSPENDED 0

typedef struct process {
    cmdLine* cmd;                         /* the parsed command line*/
    pid_t pid; 		                  /* the process id that is running the command*/
    int status;                           /* status of the process: RUNNING/SUSPENDED/TERMINATED */
    struct process *next;	                  /* next process in chain */
} process;

typedef cmdLine *CmdLine;
typedef process *Process;

typedef struct history {
    CmdLine cmds[10];
    int len;
    int size;
} History;

int execute(CmdLine cl);
int cd(char *, CmdLine);
void addProcess(Process *process_list, CmdLine cl, pid_t pid); // Receive a process list (process_list), a command (cmd), and the process id (pid) of the process running the command.
void printProcessList(Process *process_list); // print the processes.
void freeProcessList(Process process_list); //free all memory allocated for the process list.
void updateProcessList(Process *process_list); //go over the process list, and for each process check if it is done, you can use waitpid with the option WNOHANG.
void updateProcessStatus(Process process_list, int pid, int status);
void tohistory(CmdLine cl);
void printhistory();
void cleanhistory();

int debug;
int pfd[2];
History hist;

int main(int argc, char **argv) {
    hist.size = HISTORY_SIZE;
    int inb = dup(0);
    int outb = dup(1);
    debug = (argc == 2 && strcmp(argv[1], "-d") == 0);
    char cwd[PATH_MAX + 1];
    getcwd(cwd, PATH_MAX);
    char input[INPUT_MAX];
    Process procs = NULL;
    while (1) {
        if (pipe(pfd) < 0) {
            break;
        }
        printf("%s: ", cwd);
        fgets(input, INPUT_MAX, stdin);
        if (strcmp(input, "quit\n") == 0)
            break;
        CmdLine cl = parseCmdLines(input);
        if (cl->arguments[0][0] == '!') {
            int i = cl->arguments[0][1] - '0';
            freeCmdLines(cl);
            if (0 <= i && i < hist.len) {
                cl = hist.cmds[i];
            } else {
                printf("history command out-of-bounds\n");
                continue;
            }
        }
        tohistory(cl);
        if (strcmp(*cl->arguments, "cd") == 0) {
            if (!cd(cwd, cl)) {
                fprintf(stderr, "cd failed\n");
                return 1;
            }
            continue;
        }
        if (strcmp(*cl->arguments, "proc") == 0) {
            printProcessList(&procs);
            continue;
        }
        if (strcmp(*cl->arguments, "history") == 0) {
            printhistory();
            continue;
        }
        pid_t pid, pid2 = 0;
        if (strcmp(*cl->arguments, "suspend") == 0) {
            if ((pid = fork()) < 0) {
                fprintf(stderr, "fork err pid = %d", pid);
                break;
            }
            if (!pid) {
                int spid = atoi(cl->arguments[1]);
                kill(spid, SIGTSTP);
                sleep(atoi(cl->arguments[2]));
                kill(spid, SIGCONT);
                break;
            }
            else {
                freeCmdLines(cl);
                continue;
            }
        }
        if (strcmp(*cl->arguments, "kill") == 0) {
            if ((pid = fork()) < 0) {
                fprintf(stderr, "fork err pid = %d", pid);
                break;
            }
            if (!pid) {
                int spid = atoi(cl->arguments[1]);
                kill(spid, SIGINT);
                break;
            }
            else {
                freeCmdLines(cl);
                continue;
            }
        }
        if ((pid = fork()) < 0) {
            fprintf(stderr, "fork err pid = %d", pid);
            cleanhistory();
            exit(EXIT_FAILURE);
        }
        if (!pid && execute(cl) < 0) {
            printf("unknown command: %s\n", *cl->arguments);
            cleanhistory();
            freeProcessList(procs);
            _exit(EXIT_FAILURE);
        }
        if (cl->next) {
            close(pfd[1]);
            if ((pid2 = fork()) < 0) {
                fprintf(stderr, "fork err pid = %d", pid);
                break;
            }
            if (!pid2) {
                CmdLine cl2 = cl->next;
                if (debug)
                    fprintf(stderr, "(child2>redirecting stdin to the read end of the pipe…)\n");
                fclose(stdin);
                int nfd = dup2(pfd[0], 0);
                close(pfd[0]);
                int exec;
                if ((exec = execute(cl2)) < 0); {
                    printf("exec = %d...\n", exec);
                    cleanhistory();
                    freeProcessList(procs);
                    _exit(EXIT_FAILURE);
                }
            }
            close(pfd[0]);
        }
        if (debug) {
            fprintf(stderr, "pid: %d command: %s\n", pid, *cl->arguments);
        } 
        if (cl->blocking) {
            waitpid(pid, 0, 0);
        }
        if (pid2) {
            waitpid(pid2, 0, 0);
            dup(inb);
            dup(outb);
        }
        addProcess(&procs, cl, pid);
    }
    cleanhistory();
    freeProcessList(procs);
    exit(EXIT_SUCCESS);
}

int execute(CmdLine cl) {
    if (cl->inputRedirect) {
        close(0);
        if(!open(cl->inputRedirect, O_RDONLY, 0777) < 0)
            return -1;
    }
    if (cl->outputRedirect) {
        close(1);
        if(!open(cl->outputRedirect, O_WRONLY | O_CREAT, 0777))
            return -1;
    }
    if (cl->next) {
        close(1);
        int nfd = dup2(pfd[1], 1);
        close(pfd[1]);
    }
        return execvp(*cl->arguments, cl->arguments);
}

int cd(char *cwd, CmdLine cl) {
    int ch = chdir(cl->arguments[1]);
    if (cd < 0)
        return 0;
    getcwd(cwd, PATH_MAX);
    return 1;
}

 // Receive a process list (process_list), a command (cmd), and the process id (pid) of the process running the command.
void addProcess(Process *process_list, CmdLine cmd, pid_t pid) {
    Process p = malloc(sizeof(*p));
    p->cmd = cmd;
    p->pid = pid;
    p->status = (cmd->blocking) ? TERMINATED : RUNNING;
    p->next = NULL;
    if (!*process_list) {
        *process_list = p;
    }
    else {
        Process tail = *process_list;
        while(tail->next) {
            tail = tail->next;
        }
        tail->next = p;
    }
}

void printProcessList(Process *process_list) {
    //<process id> <the command> <process status>
    updateProcessList(process_list);
    char *status[3] = {"TERMINATED", "SUSPENDED", "RUNNING"};
    Process cur = process_list[0];
    printf("PID\tCommand\t\tSTATUS\n");
    while (cur) {
        printf("%d %s %s\n", cur->pid, cur->cmd->arguments[0], status[cur->status + 1]);
        if (cur->status == TERMINATED) {
            if (cur == process_list[0]) {
                process_list[0] = cur->next;
                freeCmdLines(cur->cmd);
                free(cur);
                cur = process_list[0];
            }
            else {
                Process temp = process_list[0];
                while (temp->next != cur) {
                    temp = temp->next;
                }
                temp->next = cur->next;
                freeCmdLines(cur->cmd);
                free(cur);
                cur = temp->next;
            }
        }
        else
            cur = cur->next;
    }
}

void freeProcessList(Process process_list) {
    Process cur = process_list;
    while(cur) {
        Process temp = cur;
        cur = cur->next;
        //freeCmdLines(temp->cmd);
        free(temp);
    }
}

void updateProcessList(Process *process_list) {
    Process cur = process_list[0];
    int status;
    while(cur) {
        if (waitpid(cur->pid, &status, WNOHANG | WUNTRACED | WCONTINUED) > 0) {
            if (WIFEXITED(status) || WIFSIGNALED(status))
                updateProcessStatus(process_list[0], cur->pid, TERMINATED);
            else if (WIFSTOPPED(status))
                updateProcessStatus(process_list[0], cur->pid, SUSPENDED);
            else if (WIFCONTINUED(status))
                updateProcessStatus(process_list[0], cur->pid, RUNNING);
        }
        cur = cur->next;
    }
}

void updateProcessStatus(Process process_list, int pid, int status) {
    if (!pid)
        return;
    while (process_list) {
        if (process_list->pid == pid) {
            process_list->status = status;
            return;
        }
        process_list = process_list->next;
    }
}

void tohistory(CmdLine cl) {
    if (hist.len < hist.size) {
        hist.cmds[hist.len++] = cl;
        return;
    }
    int i;
    CmdLine lru = hist.cmds[0];
    int reused;
    for (i = 0; i < hist.len - 1; i++) {
        hist.cmds[i] = hist.cmds[i + 1];
        if (hist.cmds[i] == lru)
            reused = 1;
    }
    hist.cmds[i] = cl;
}
void printhistory() {
    for (int i= 0; i < hist.len; i++) {
        printf("%d: ", i);
        CmdLine cl = hist.cmds[i];
        while (cl) {
            for (int j = 0; j < cl->argCount; j++)
                printf("%s%c", cl->arguments[j], (j < cl->argCount - 1) ? ' ' : 0);
            if (cl->inputRedirect)
                printf(" < %s", cl->inputRedirect);
            if (cl->outputRedirect)
                printf(" > %s", cl->outputRedirect);
            cl = cl->next;
            if (cl)
                printf(" | ");
        }
        printf("\n");
    }
}
void cleanhistory() {
    for (int i = 0; i < hist.len; i++) {
        for (int j = i + 1; j < hist.len; j++)
            if (hist.cmds[i] == hist.cmds[j])
                hist.cmds[j] = 0;
        freeCmdLines(hist.cmds[i]);
    }
    hist.len = 0;
}