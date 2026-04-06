#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>

void print_usage() {
    fprintf(stderr, "ERROR: Invalid argument(s)\n");
}

void handle_error(const char *message) {
    perror(message);
    exit(EXIT_FAILURE);
}

void free_board(char **board, int m) {
    int i;
    for (i = 0; i < m; i++) {
        free(*(board + i)); // Free each row
    }
    free(board); // Free the array of row pointers
}

int is_valid_move(char **board, int row, int col, int n) {
    int i, j;
    // Check the current column
    for (i = 0; i < row; i++) {
        if (*(*(board + i) + col) == 'Q') {
            return 0;
        }
    }

    // Check the current row
    for (j = 0; j < col; j++) {
        if (*(*(board + row) + j) == 'Q') {
            return 0;
        }
    }

    // Check the diagonals
    for (i = 0; i < row; i++) {
        j = col - (row - i); // Upper-left diagonal
        if (j >= 0 && j < n && *(*(board + i) + j) == 'Q') {
            return 0;
        }
        j = col + (row - i); // Upper-right diagonal
        if (j >= 0 && j < n && *(*(board + i) + j) == 'Q') {
            return 0;
        }
    }

    return 1;
}

void solve_queens(int row, int m, int n, char **board, int pipe_fd_write, short queen_count, int* pipe_fd) {
    if (row == m) {
        // End-state: All rows are processed, valid solution
        #ifndef QUIET
        printf("P%d: found a solution; notifying top-level parent\n", getpid());
        #endif
        write(pipe_fd_write, &queen_count, sizeof(queen_count));
        close(pipe_fd_write);  // Close the pipe descriptor after use
        free_board(board, m);  // Free allocated memory
        free(pipe_fd);
        exit(EXIT_SUCCESS);
    }

    int possible_moves = 0;
    int col;
    for (col = 0; col < n; col++) {
        if (is_valid_move(board, row, col, n)) {
            possible_moves++;
        }
    }

    if (possible_moves > 0) {
        #ifndef QUIET
        printf("P%d: %d possible move%s at row #%d; creating %d child process%s...\n", 
            getpid(), possible_moves, (possible_moves == 1 ? "" : "s"), row, 
            possible_moves, (possible_moves == 1 ? "" : "es"));
        #else
        if(row==0){
            printf("P%d: %d possible move%s at row #%d; creating %d child processes...\n", getpid(), possible_moves, (possible_moves == 1 ? "" : "s"), row, possible_moves);
        }
        #endif
    }

    for (col = 0; col < n; col++) {
        if (is_valid_move(board, row, col, n)) {
            fflush(stdout); // Ensure all output is flushed before forking
            pid_t pid = fork();

            if (pid < 0) {
                handle_error("fork");
            } else if (pid == 0) {
                // Child process: place a Queen and continue solving
                *(*(board + row) + col) = 'Q';
                queen_count++;
                solve_queens(row + 1, m, n, board, pipe_fd_write, queen_count, pipe_fd);
                free_board(board, m);  // Free allocated memory
                free(pipe_fd);
                exit(EXIT_SUCCESS); // Shouldn't reach here
            } else {
                // Parent process: continue to next column
                #ifdef NO_PARALLEL
                waitpid(pid, NULL, 0);
                #endif
            }
        }
    }

    if (possible_moves == 0) {
        // Dead end: No valid moves for this row
        #ifndef QUIET
        printf("P%d: dead end at row #%d; notifying top-level parent\n", getpid(), row);
        #endif
        write(pipe_fd_write, &queen_count, sizeof(queen_count));
        close(pipe_fd_write);  // Close the pipe descriptor after use
        free_board(board, m);  // Free allocated memory
        free(pipe_fd);
        exit(EXIT_SUCCESS);
    }

    #ifndef NO_PARALLEL
    // Wait for all child processes to finish
    while (wait(NULL) > 0);
    #endif
}

int main(int argc, char **argv) {
    if (argc != 3) {
        print_usage();
        return EXIT_FAILURE;
    }

    int m = atoi(*(argv + 1));
    int n = atoi(*(argv + 2));

    if (m <= 0 || n <= 0) {
        print_usage();
        return EXIT_FAILURE;
    }

    if (n < m) {
        int temp = m;
        m = n;
        n = temp;
    }

    char **board = (char **)calloc(m, sizeof(char *));
    if (board == NULL) handle_error("calloc");

    int i = 0;
    while (i < m) {
        *(board + i) = (char *)calloc(n, sizeof(char));
        if (*(board + i) == NULL) {
            free_board(board, i); // Free already allocated rows
            handle_error("calloc");
        }
        i++;
    }

    // Set up the pipe for IPC
    int *pipe_fd = (int *)calloc(2, sizeof(int));
    if (pipe_fd == NULL) {
        free(pipe_fd);
        free_board(board, m);
        handle_error("malloc");
    }

    if (pipe(pipe_fd) == -1) {
        free(pipe_fd);
        free_board(board, m);
        handle_error("pipe");
    }

    printf("P%d: solving the Abridged (m,n)-Queens problem for %dx%d board\n", getpid(), m, n);
    fflush(stdout); // Ensure all output is flushed before forking

    short queen_count = 0;

    pid_t pid = fork();
    if (pid < 0) {
        close(*(pipe_fd + 0));
        close(*(pipe_fd + 1));
        free(pipe_fd);
        free_board(board, m);
        handle_error("fork");
    } else if (pid == 0) {
        // Child process
        close(*(pipe_fd + 0));
        solve_queens(0, m, n, board, *(pipe_fd + 1), queen_count, pipe_fd);
        close(*(pipe_fd + 1));
        free(pipe_fd);
        free_board(board, m);
        exit(EXIT_SUCCESS);
    } else {
        // Parent process
        close(*(pipe_fd + 1));
        wait(NULL);  // Wait for the child process to finish

        int *counts = (int *)calloc(m + 1, sizeof(int));
        if (counts == NULL) {
            close(*(pipe_fd + 0));
            free(pipe_fd);
            free_board(board, m);
            handle_error("calloc");
        }

        short queens_count;
        while (read(*(pipe_fd + 0), &queens_count, sizeof(queens_count)) > 0) {
            *(counts + queens_count) += 1;
        }

        printf("P%d: search complete\n", getpid());
        int j = 1;
        while (j <= m) {
            printf("P%d: number of %d-Queen end-states: %d\n", getpid(), j, *(counts + j));
            j++;
        }

        close(*(pipe_fd + 0));  // Close the pipe descriptor
        free(pipe_fd);
        free(counts);
    }

    free_board(board, m);

    return EXIT_SUCCESS;
}
