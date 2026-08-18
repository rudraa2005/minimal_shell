#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <termios.h>
#include <errno.h>

#define MAX_ARGS 128
#define MAX_JOBS 64
#define MAX_LINE 1024

typedef struct{
    int id;
    pid_t pgid;
    char command [MAX_LINE];
    int running;
} Job;

Job jobs[MAX_JOBS];
int job_count = 0;

pid_t shell_pgid;
struct termios shell_tmodes;

void add_job(pid_t pgid, const char *command, int running){
    if(job_count>= MAX_JOBS){
        return;
    }

    jobs[job_count].id = job_count+1;
    jobs[job_count].pgid = pgid;
    jobs[job_count].running = running;

    strncpy(jobs[job_count].command, command, MAX_LINE - 1);
    jobs[job_count].command[MAX_LINE-1] = '\0';

    job_count++;
}

int find_job(int id){
    for(int i = 0;i<job_count;i++){
        if(jobs[i].id == id){
            return i;
        }
    }
    return -1;
}

void remove_job(int index){
    if(index < 0 || index >= job_count){
        return;
    }
    for(int i = index; i<job_count - 1; i++){
        jobs[i] = jobs[i+1];
    }

    job_count--;
}

void print_jobs(){
    for(int i = 0; i<job_count; i++){
        printf("[%d] %s\t%s\n",jobs[i].id,jobs[i].running ? "Running" : "Stopped",jobs[i].command);
    }
}

void sigchild_handler(int sig){
    (void)sig;

    int status;
    pid_t pid;

    while((pid = waitpid(-1,&status,
        WNOHANG|WUNTRACED|WCONTINUED))>0){

    }
}

void setup_shell(){
    shell_pgid = getpid();

    if(setpgid(shell_pgid, shell_pgid)<0 && errno != EACCES){
        perror("setpgid");
        exit(1);
    }

    tcsetpgrp(STDIN_FILENO, shell_pgid); //give terminal control to shell
    tcgetattr(STDIN_FILENO,&shell_tmodes);  //save terminal settings

    //shell should ignore terminal commands
    signal(SIGINT,SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU,SIG_IGN);

    signal(SIGCHLD, sigchild_handler);
}

int tokenize(char *line, char **tokens){
    int count = 0;

    char *token = strtok(line," \t\n");

    while(token != NULL && count < MAX_ARGS - 1){
        tokens[count++] = token;
        token = strtok(NULL, " \t\n");
    }

    tokens[count] = NULL;
    return count;
}

void setup_redirection(char **args){
    for(int i = 0; args[i] != NULL; i++){
        if(strcmp(args[i],"<")==0){
            if(args[i+1] == NULL){
                fprintf(stderr, "missing input file\n");
                exit(1);
            }
            int fd = open(args[i+1], O_RDONLY);

            if(fd <0){
                perror("open");
                exit(1);
            }

            dup2(fd, STDIN_FILENO);
            close(fd);

            args[i] = NULL;
            i++;
        }
        else if(strcmp(args[i],">") == 0){
            if(args[i+1] == NULL){
                fprintf(stderr,"missing output file\n");
                exit(1);
            }

            int fd = open(args[i+1],O_WRONLY | O_CREAT | O_TRUNC,0644);

            if(fd <0){
                perror("open");
                exit(1);
            }

            dup2(fd,STDOUT_FILENO);
            close(fd);

            args[i] = NULL;
            i++;
        }

        else if(strcmp(args[i],">>") == 0){
            if(args[i+1] == NULL){
                fprintf(stderr,"missing output file");
                exit(1);
            }

            int fd = open(args[i+1], O_WRONLY | O_CREAT | O_APPEND, 0644);

            if(fd <0){
                perror("Open");
                exit(1);
            }

            dup2(fd,STDOUT_FILENO);
            close(fd);

            args[i] = NULL;
            i++;
        }

        else if(strcmp(args[i],"2>") == 0){
            if(args[i+1] == NULL){
                fprintf(stderr, "missing error file\n");
                exit(1);
            }
            int fd = open(args[i+1], O_WRONLY|O_CREAT| O_TRUNC,0644);

            if(fd<0){
                perror("open");
                exit(1);
            }

            dup2(fd,STDERR_FILENO);
            close(fd);

            args[i] = NULL;
            i++;
        }
    }
}


void wait_for_job(pid_t pgid,int job_index){
    int status;
    pid_t pid;

    while(1){
        pid = waitpid(-pgid,&status,WUNTRACED);

        if(pid<0){
            if(errno == EINTR){
                continue;
            }
            break;
        }

        if(WIFSTOPPED(status)){
            if(job_index >= 0){
                jobs[job_index].running = 0;
            }
            printf("\n[%d] STOPPED \t%s\n", 
                job_index>=0 ? jobs[job_index].id : 0, job_index>=0 ? jobs[job_index].command : "");

            break;
        }

        if(WIFEXITED(status) || WIFSIGNALED(status)){
            int alive = 0;
            while(waitpid(-pgid,&status,WNOHANG)>0){
                alive = 1;
            }
            if(!alive){
                break;
            }
        }
    }
}

void execute_pipeline(char *line, int background){
    char *commands[32];
    int command_count = 0;

    char *part = strtok(line,"|");

    while(part != NULL && command_count<32){
        commands[command_count++] = part;
        part = strtok(NULL,"|");
    }

    int pipes[31][2];

    for(int i =0 ; i<command_count-1; i++){
        if(pipe(pipes[i])<0){
            perror("pipe");
            return;
        }
    }

    pid_t pgid = 0;
    for(int i =0; i< command_count;i++){
        char *args[MAX_ARGS];
        tokenize(commands[i],args);

        if(args[0] == NULL){
            continue;
        }

        pid_t pid = fork();
        if(pid<0){
            perror("fork");
            return;
        }

        if(pid == 0){
            if(pgid == 0)
                pgid = getpid();
            setpgid(0,pgid);

            signal(SIGINT, SIG_DFL);
            signal(SIGTTIN,SIG_DFL);
            signal(SIGTTOU,SIG_DFL);
            signal(SIGCHLD,SIG_DFL);

            if(i>0){
                dup2(pipes[i-1][0],STDIN_FILENO);
            }

            if(i < command_count - 1)
                dup2(pipes[i][1], STDOUT_FILENO);

            for(int j=0;j<command_count-1;j++){
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            setup_redirection(args);
            execvp(args[0],args);

            perror("execvp");
            exit(127);
        } else{
            if(pgid == 0){
                pgid=pid;
            }

            setpgid(pid,pgid);
        }
    }

    for(int i = 0; i<command_count-1; i++){
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    if(background){
        add_job(pgid,commands[0],1);

        printf("[%d] %d\n", jobs[job_count-1].id,pgid);

        return;
    }

    tcsetpgrp(STDIN_FILENO,pgid);
    wait_for_job(pgid,-1);

    tcsetpgrp(STDIN_FILENO,shell_pgid);
    tcgetattr(STDIN_FILENO, &shell_tmodes);
}

void foreground_job(int id){
    int index = find_job(id);

    if(index<0){
        printf("No such jobs\n");
        return;
    }

    pid_t pgid = jobs[index].pgid;

    kill(-pgid,SIGCONT);
    jobs[index].running = 1;

    tcsetpgrp(STDIN_FILENO,pgid);
    wait_for_job(pgid,index);

    tcsetpgrp(STDIN_FILENO, shell_pgid);

    int status;
    pid_t result = waitpid(-pgid, &status, WNOHANG);

    if(result == -1 && errno == ECHILD){
        remove_job(index);
    }
}

void background_job(int id){
    int index = find_job(id);

    if(index<0){
        printf("No such Jobs");
        return;
    }

    kill(-jobs[index].pgid,SIGCONT);

    jobs[index].running = 1;

    printf("[%d] %s\n", jobs[index].id,jobs[index].command);
}

int main(){
    setup_shell();

    char line[MAX_LINE];

    while(1){
        printf("myshell>");
        fflush(stdout);

        if(fgets(line, sizeof(line),stdin) == NULL){
            printf("\n");
            break;
        }
        if(line[0]=='\n'){
            continue;
        }

        line[strcspn(line,"\n")] = '\0';

        if(strcmp(line,"exit") == 0){
            break;
        }

        if(strncmp(line,"fg %",3) == 0){
            int id = atoi(line + 3);
            foreground_job(id);
            continue;
        }

        if(strncmp(line,"bg %",3) == 0){
            int id = atoi(line+3);
            background_job(id);
            continue;
        }

        int background = 0;
        int len = strlen(line);
        if(len>0 && line[len-1] == "&"){
            background = 1;
            line[len-1] = '\0';
        }

        execute_pipeline(line,background);
    }
    return 0;
}