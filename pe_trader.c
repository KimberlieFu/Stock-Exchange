#include "pe_trader.h"

volatile sig_atomic_t market_open = false;



void trader_signal_handler(int sig) {
    market_open = true;
}


void message_writer(char* message, char* order_type, char* order_id, char* product, char* quantity, char* price) {
    strcpy(message, order_type);
    strcat(message, " ");
    strcat(message, order_id);
    strcat(message, " ");
    strcat(message, product);
    strcat(message, " ");
    strcat(message, quantity);
    strcat(message, " ");
    strcat(message, price);
    strcat(message, delimiter);
}


int main(int argc, char ** argv) {
    // address of fifo in string format
    char fifo_read[BUFFER_SIZE]; 
    char fifo_write[BUFFER_SIZE];

    // fifo
    char fifo_read_buffer[BUFFER_SIZE];
    char fifo_write_buffer[BUFFER_SIZE];

    // variables
    int process_id = atoi(argv[1]);
    int order_id = 0;



    // -----------------------(PRE-PROCESS)-----------------------
    // check number of arguments
    if (argc < 2) {
        //printf("Not enough arguments\n");
        return 1;
    }

    // check valid process id
    if (process_id == 0 && strcmp(argv[1], "0") != 0) {
        //printf("Not a valid process id number\n");
        return 1;
    }
    
    // register signal handler
    struct sigaction sa = {0};
    sa.sa_flags = SA_SIGINFO;
    sa.sa_handler = &trader_signal_handler;
    sigaction(SIGUSR1, &sa, NULL);
    
    sprintf(fifo_write, FIFO_TRADER, process_id);
    sprintf(fifo_read, FIFO_EXCHANGE, process_id);


    // connect to named pipes
    // read first for exchange program to recognize named pipe establishment
    int fd_read = open(fifo_read, O_RDONLY); //read only and return file descriptor
    if (fd_read == -1) {
        perror("Failed to connect to name pipe for read");
        return 2;
    }

    int fd_write = open(fifo_write, O_WRONLY); //write only and return file descriptor
    if (fd_write == -1) {
        perror("Failed to connect to name pipe for write");
        return 2;
    }




    // -----------------------(MAIN)-----------------------
    // initailization 
    ssize_t bytes_read = read(fd_read, fifo_read_buffer, sizeof(fifo_read_buffer));
    if (bytes_read == -1) {
        perror("Failed to read from fifo (initialize)");
        return 2;
    }
    fifo_read_buffer[bytes_read] = '\0'; //string ends with null

    while (strcmp(fifo_read_buffer, message_string[MARKET_OPEN]) != 0) {
        // wait for market to open (fifo)
    }
    while (!market_open) {
        // wait for market to open (signal)
    }


    // event loop:
    while (market_open) {

        /* USYD CODE CITATION ACKNOWLEDGEMENT
            * I declare that the majority of the following code has been taken from the
            * Introductiion to Unix Signal Programming and it is not my own work. 
            * 
            * Original URL
            * https://www.cs.kent.edu/~ruttan/sysprog/lectures/signals.html
            * Last access May, 2023
            */ 

        
        // Block all signals
        sigset_t mask; 
        sigfillset(&mask); // all signal set
        sigprocmask(SIG_BLOCK, &mask, NULL);


        // read exchange pipe
        // wait for exchange update (MARKET message)
        ssize_t bytes_read = read(fd_read, fifo_read_buffer, sizeof(fifo_read_buffer));
        if (bytes_read == -1) {
            perror("Failed to read from fifo (event loop)");
            return 2;
        }
        fifo_read_buffer[bytes_read] = '\0'; //string ends with null


        // Unblock signals
        sigprocmask(SIG_UNBLOCK, &mask, NULL);


        // read and split instruction
        int token_count = 0;
        char* instruction_split[BUFFER_SIZE];

        char* word = strtok(fifo_read_buffer, " ");
        while (word != NULL && token_count < BUFFER_SIZE) {
            instruction_split[token_count] = word;
            word = strtok(NULL, " ");
            token_count ++;
        }

        // fixing price
        char* price = instruction_split[price_placement];
        price[strlen(price) - 1] = '\0';


        // check is sell is on pipe
        if (strcmp(instruction_split[order_type_placement], message_string[SELL]) == 0) {
            
            int quantity = atoi(instruction_split[quantity_placement]);

            // check if quantity section is int
            if (quantity == 0 && strcmp(instruction_split[quantity_placement], "0") != 0) {continue;}
            // check if greater than quantity limit
            if (quantity >= quantity_limit) {
                market_open = false;
                break;
            }

            char order_id_str[BUFFER_SIZE];
            sprintf(order_id_str, "%d", order_id);
            
            message_writer(fifo_write_buffer, message_string[BUY], order_id_str, instruction_split[product_placement], instruction_split[quantity_placement], instruction_split[price_placement]);
            
            order_id ++;
            

            // send order
            // writing BUY to named pipe
            int message_length = strlen(fifo_write_buffer);
            if (write(fd_write, fifo_write_buffer, message_length) == -1) {
                perror("Failed to write to fifo");
                return 2;
            }

            // send signal to exchange
            kill(getppid(), SIGUSR1);
        }

    }

    // wait for exchange confirmation (ACCEPTED message)

    close(fd_write);
    close(fd_read);
    return 0;   
}