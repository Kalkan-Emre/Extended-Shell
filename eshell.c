#include "parser.h"
#include <stdio.h>
#include <unistd.h>
#include <signal.h>


void execute_all(parsed_input *input);
void execute_subshell(parsed_input *p_input, int i);

typedef struct {
    int fd[2];
} pipe_fd;


void execute_single_command(command cmd){
    pid_t p;
    int status;
    if ((strcmp(cmd.args[0], "quit") == 0)||
         strcmp(cmd.args[0], "quit\n") == 0) {
        exit(0);
    }
    if ((p=fork())) {
        //Parent
        wait(&status);
	} else {
        //Child
        execvp(cmd.args[0], cmd.args);
        perror("Execvp failed");
        exit(EXIT_FAILURE);
	}
}

void execute_pipeline(parsed_input *p_input, int N, int input_index){
    pid_t p;
    int fd[2], read_end;
    pid_t pid[N];
    int i, child_status;

    pipe(fd);
    for (i = 0; i < N; i++){
        if(i==0){
            if ((pid[i] = fork()) == 0){
                close(fd[0]);
                dup2(fd[1],1);
                close(fd[1]);
                if(p_input->separator==SEPARATOR_PIPE){
                    if(p_input->inputs[i].type==INPUT_TYPE_SUBSHELL){
                        execute_subshell(p_input, i);
                        fflush(stdout);
                        exit(0);
                    }
                    else{
                        execvp(p_input->inputs[i].data.cmd.args[0], p_input->inputs[i].data.cmd.args);
                        perror("Execvp failed");
                        exit(EXIT_FAILURE);
                    }
                }
                else{
                    execvp(p_input->inputs[input_index].data.pline.commands[i].args[0], p_input->inputs[input_index].data.pline.commands[i].args);
                    perror("Execvp failed");
                    exit(EXIT_FAILURE);
                }
            }
            else{
                read_end = fd[0];
                close(fd[1]);
            }
        }
        else{
            pipe(fd);
            if ((pid[i] = fork()) == 0) {
                if(i==N-1){
                    close(fd[1]);
                    close(fd[0]);
                    dup2(read_end,0);
                    close(read_end);
                }
                else{
                    close(fd[0]);
                    dup2(read_end,0);
                    close(read_end);

                    dup2(fd[1],1);
                    close(fd[1]);
                }
                if(p_input->separator==SEPARATOR_PIPE){
                    if(p_input->inputs[i].type==INPUT_TYPE_SUBSHELL){
                        execute_subshell(p_input, i);
                        fflush(stdout);
                        exit(0);
                    }
                    else{
                        execvp(p_input->inputs[i].data.cmd.args[0], p_input->inputs[i].data.cmd.args);
                        perror("Execvp failed");
                        exit(EXIT_FAILURE);
                    }
                }
                else{
                    execvp(p_input->inputs[input_index].data.pline.commands[i].args[0], p_input->inputs[input_index].data.pline.commands[i].args);
                    perror("Execvp failed");
                    exit(EXIT_FAILURE);
                }
            }
            else{
                close(read_end);
                read_end = fd[0];
                close(fd[1]);
            }
        }
    }
    close(read_end);

    for (i = 0; i < N; i++) { /* Parent */
        pid_t wpid = waitpid(pid[i], &child_status, 0);
    }
}

void execute_parallel(parsed_input *p_input, int N){
    pid_t p;
    pid_t pid[N];
    int i, child_status;

    for (i = 0; i < N; i++){
        if ((pid[i] = fork()) == 0){
            if(p_input->inputs[i].type==INPUT_TYPE_COMMAND){
                execvp(p_input->inputs[i].data.cmd.args[0], p_input->inputs[i].data.cmd.args);
                perror("Execvp failed");
                exit(EXIT_FAILURE);
            }
            else if(p_input->inputs[i].type==INPUT_TYPE_PIPELINE){
                execute_pipeline(p_input, p_input->inputs[i].data.pline.num_commands, i);
                exit(0);
            }
        }
    }

    while(1) {
        p = waitpid(-1, &child_status, 0);
        if(p <= 0) {break;}
    }
}

void repeater(pipe_fd pipes[], int N) {
    char buffer[INPUT_BUFFER_SIZE];
    int is_open[N];
    ssize_t bytesRead;

    signal(SIGPIPE, SIG_IGN);

    for (int i = 0; i < N; i++) is_open[i] = 1;

    while ((bytesRead = read(0 , buffer, sizeof(buffer))) > 0) {
        for (int i = 0; i < N; i++) {
            if(!is_open[i]) continue;
            else if(write(pipes[i].fd[1], buffer, bytesRead) == -1){
                is_open[i]=0;
            }
        }
    }
    for (int i = 0; i < N; i++) close(pipes[i].fd[1]);
}

void execute_subshell(parsed_input *p_input, int i){
    pid_t p;
    int status;
    parsed_input subshell_input;
    parse_line(p_input->inputs[i].data.subshell, &subshell_input);

    if ((p=fork())) {
        //Parent
        wait(&status);
    } else {
        //Child
        int child_status;
        int N = subshell_input.num_inputs;
        pipe_fd pipes[N];
        if(subshell_input.separator==SEPARATOR_PARA){
            for(int i = 0 ; i < N ; i++){
                pipe(pipes[i].fd);
                if ((fork()) == 0){
                    dup2(pipes[i].fd[0],0);
                    close(pipes[i].fd[0]);
                    close(pipes[i].fd[1]);
                    if(subshell_input.inputs[i].type==INPUT_TYPE_COMMAND){
                        execvp(subshell_input.inputs[i].data.cmd.args[0], subshell_input.inputs[i].data.cmd.args);
                        perror("Execvp failed");
                        exit(EXIT_FAILURE);
                    }
                    else if(subshell_input.inputs[i].type==INPUT_TYPE_PIPELINE){
                        execute_pipeline(&subshell_input, subshell_input.inputs[i].data.pline.num_commands, i);
                        exit(0);
                    }
                }
                else{
                    close(pipes[i].fd[0]);
                }
            }
            if(fork()==0){
                repeater(pipes, N);
                exit(0);
            }
            else{
                for(int i = 0 ; i < N ; i++) close(pipes[i].fd[1]);
            }
            while(1) {
                p = waitpid(-1, &child_status, 0);
                if(p <= 0) {break;}
            }
            exit(0);
        }
        else{
            execute_all(&subshell_input);
            free_parsed_input(&subshell_input);
            exit(0);
        }
    }
}

void execute_all(parsed_input *input) {
    switch (input->separator) {
        case SEPARATOR_PIPE:
            execute_pipeline(input, input->num_inputs, 0); break;
        case SEPARATOR_SEQ:
            for (int i = 0; i < input->num_inputs; i++) {
                single_input *inp = &input->inputs[i];
                switch (inp->type) {
                    case INPUT_TYPE_COMMAND:
                        execute_single_command(inp->data.cmd);
                        break;
                    case INPUT_TYPE_PIPELINE:
                        execute_pipeline(input, inp->data.pline.num_commands, i);
                        break;
                }
            }
            break;
        case SEPARATOR_PARA:
            execute_parallel(input, input->num_inputs);
            break;
        case SEPARATOR_NONE:
            switch (input->inputs[0].type) {
                case INPUT_TYPE_COMMAND:
                    execute_single_command(input->inputs[0].data.cmd);
                    break;
                case INPUT_TYPE_SUBSHELL:
                    execute_subshell(input, 0);
                    fflush(stdout);
                    break;
            }

        default: break; // Should not happen
    }
}

int main(){
    size_t bufsize = INPUT_BUFFER_SIZE;
    parsed_input  input;
    int is_valid;


    while(1){
        char *line = NULL;
        fflush(stdout);
        printf("/> ");
        getline(&line, &bufsize, stdin);

        is_valid = parse_line(line, &input);

        if(is_valid){
            execute_all(&input);
            free_parsed_input(&input);
        }
        else{
            break;
        }
    }
}