/**
 * comp2017 - assignment 3
 * Kimberlie Fu
 * kifu5223
 */

#include "pe_exchange.h"


volatile sig_atomic_t signal_queue[MAX_EVENTS];
sigset_t mask;
bool exchange_open = true;
int signal_queue_count = 0;
int timer = 0;

void exchange_signal_handler(int sig);
void parent_signal_handler(int sig);
int free_memory(int argc, int num_products, char** product_list, Trader trader_list[BUFFER_SIZE], char fifo_read[BUFFER_SIZE], char fifo_write[BUFFER_SIZE], int fd_epoll);
void orderbook(Item_Multiple item_multiple_list[BUFFER_SIZE], Item item_list[BUFFER_SIZE], int num_orderbook, int num_products);
void position(Trader trader_list[BUFFER_SIZE], int num_traders, int num_products);
void matching(Trader trader_list[BUFFER_SIZE], Item_Multiple item_multiple_list[BUFFER_SIZE], Order buy_order_list[BUFFER_SIZE], Order sell_order_list[BUFFER_SIZE],
                Item item_list[BUFFER_SIZE], char fifo_write_buffer[BUFFER_SIZE], int* num_traders, int* num_traders_final, int* num_products, int* num_orderbook, int* num_buy_orders, int* num_sell_orders, int* exchange_fee);
int compare_item_multiple(const void* a, const void* b);
int compare_order_price(const void* a, const void* b);
int compare_order_time(const void* a, const void* b);



// -----------------------(MAIN)-----------------------

int main(int argc, char **argv) {
	char product_buffer_reader[BUFFER_SIZE];
    char fifo_read_buffer[BUFFER_SIZE];
    char fifo_write_buffer[BUFFER_SIZE];

	char** product_list = NULL;
    Trader trader_list[BUFFER_SIZE];
    Order buy_order_list[BUFFER_SIZE];
    Order sell_order_list[BUFFER_SIZE];
    Item item_list[BUFFER_SIZE] = {0};
    Item_Multiple item_multiple_list[BUFFER_SIZE] = {0};
    Item_Position item_pos_list[BUFFER_SIZE] = {0};
    

    char fifo_read[BUFFER_SIZE]; 
    char fifo_write[BUFFER_SIZE];
    struct epoll_event events[MAX_EVENTS];
    struct epoll_event event;

    int num_traders = argc - 2;
    int num_traders_final = argc - 2;
	int num_products = 0;
    int num_buy_orders = 0;
    int num_sell_orders = 0;
    int num_orderbook = 0;
    int exchange_fee = 0;
    int disconnect = 0;
    FILE* product_file;
    bool first_line = true;

	
	// check number of arguments
    if (argc < 2) {
        printf("Not enough arguments\n");
        return 1;
    }


    // register signal handler
    struct sigaction sa = {0};
    sa.sa_flags = SA_SIGINFO;
    sa.sa_handler  = &exchange_signal_handler;
    sigaction(SIGUSR1, &sa, NULL);



    // set signal fd
    int fd_signal = signalfd(-1, &mask, 0);
    if (fd_signal == -1) {
        perror("Failed to create signalfd");
        return 2;
    }
    
    // register epoll
    int fd_epoll = epoll_create1(0);
    if (fd_epoll == -1) {
        perror("failed to create epoll");
        return 2;
    }

    // signal detect
    event.data.fd = fd_signal;
    event.events = EPOLLIN;
    if (epoll_ctl(fd_epoll, EPOLL_CTL_ADD, fd_signal, &event) == -1) {
        perror("Failed to add fd signal to event list");
        return 2;
    }



	// -----------------------(FILE READING)-----------------------
	product_file = fopen(argv[1], "r");
	if (product_file == NULL) {
        perror("Error opening file");
        return 2;
    }

    printf("%s Starting\n", LOG_PREFIX);

    product_list = malloc(sizeof(char*));
    if (product_list == NULL) {
        printf("Failed to allocate memory.\n");
        return 2; 
    }

    int product_index = 0;
	while (fgets(product_buffer_reader, sizeof(product_buffer_reader), product_file)) {
		product_buffer_reader[strcspn(product_buffer_reader, "\n")] = '\0'; // remove the newline character

        if (first_line) {
            first_line = false;
            num_products = atoi(product_buffer_reader);

            // check if it is int
            if (num_products == 0 && strcmp(product_buffer_reader, "0") != 0) {
                perror("First line in file is not int");
                free_memory(argc, num_products, product_list, trader_list, fifo_read, fifo_write, fd_epoll);;
                return 1;
            }

            // allocate memory for product_list based on num_products
            product_list = realloc(product_list, num_products * sizeof(char*));
            if (product_list == NULL) {
                perror("Failed to allocate memory");
                free_memory(argc, num_products, product_list, trader_list, fifo_read, fifo_write, fd_epoll);;
                return 2; 
            }


        } else {
            product_list[product_index] = malloc(strlen(product_buffer_reader) + 1);
            if (product_list == NULL) {
                perror("Failed to allocate memory");
                free_memory(argc, num_products, product_list, trader_list, fifo_read, fifo_write, fd_epoll);;
                return 2; 
            }

            strcpy(product_list[product_index], product_buffer_reader);
            product_index ++;
        }
    }

	
    printf("%s Trading %d products:", LOG_PREFIX, num_products); // overcounting first thing on list
	for (int i = 0; i < num_products; i ++) {
        Item_Position item_pos;
        Item item;

        strcpy(item_pos.name, product_list[i]);
        item_pos.quantity = 0;
        item_pos.price = 0;

        strcpy(item.name, product_list[i]);
        item.sell_level = 0;
        item.buy_level = 0;


        item_pos_list[i] = item_pos;
        item_list[i] = item;

        printf(" ");
		printf("%s", product_list[i]);
	}
	printf("\n");




	for (int i = 0; i < num_traders; i++) {
        // -----------------------(MAKING FIFO)-----------------------
        // address of fifo in string format

		sprintf(fifo_read, FIFO_TRADER, i);
		sprintf(fifo_write, FIFO_EXCHANGE, i);


        Trader t;
        t.trader_number = i;
        t.number_of_order = 0;
        trader_list[i] = t;


        // create write fifo
        int mkfifo_write = mkfifo(fifo_write, 0777); 
        if (mkfifo_write == -1 && errno != EEXIST) {
            perror("making write fifo failed");
            free_memory(argc, num_products, product_list, trader_list, fifo_read, fifo_write, fd_epoll);;
            return 2;
        }
		printf("%s Created FIFO %s\n", LOG_PREFIX, fifo_write);


        // create read fifo
        int mkfifo_read = mkfifo(fifo_read, 0777);
        if (mkfifo_read == -1 && errno != EEXIST) {
            perror("making read fifo failed");
            free_memory(argc, num_products, product_list, trader_list, fifo_read, fifo_write, fd_epoll);;
            return 2;
        }
		printf("%s Created FIFO %s\n", LOG_PREFIX, fifo_read);


        // -----------------------(INITIATE TRADER)-----------------------
        pid_t pid = fork();

        if (pid == 0) {
            // child process
            char trader_id[BUFFER_SIZE];
            sprintf(trader_id, "%d", i);

            // send signal to parent process 
            kill(getppid(), SIGUSR1);
            if (execl(argv[2 + i], basename(argv[2 + i]), trader_id, NULL) == -1) {
                perror("Failed to execute trader");
                free_memory(argc, num_products, product_list, trader_list, fifo_read, fifo_write, fd_epoll);;
                return 2;
            }

        } else if (pid > 0) {
            // parent process
            pause(); // waiting for child process initialize trader
            printf("%s Starting trader %d (%s)\n", LOG_PREFIX, i, argv[2 + i]);
            trader_list[i].pid = pid;



            // -----------------------(CONNECT FIFO)-----------------------
            int fd_write = open(fifo_write, O_WRONLY); //write only and return file descriptor
            if (fd_write == -1) {
                perror("Failed to connect to name pipe for write");
                free_memory(argc, num_products, product_list, trader_list, fifo_read, fifo_write, fd_epoll);;
                return 2;
            }
            trader_list[i].trader_fifo_read = fd_write;
            printf("%s Connected to %s\n", LOG_PREFIX, fifo_write);


            int fd_read = open(fifo_read, O_RDONLY| O_NONBLOCK); //read only and return file descriptor
            if (fd_read == -1) {
                perror("Failed to connect to name pipe for read");
                free_memory(argc, num_products, product_list, trader_list, fifo_read, fifo_write, fd_epoll);;
                return 2;
            }
            trader_list[i].trader_fifo_write = fd_read;
            event.data.fd = fd_read;
            event.events = EPOLLIN | EPOLLET | EPOLLHUP;
            if (epoll_ctl(fd_epoll, EPOLL_CTL_ADD, fd_read, &event) == -1) {
                perror("Failed to add fd_read to event list");
                free_memory(argc, num_products, product_list, trader_list, fifo_read, fifo_write, fd_epoll);;
                return 2;
            }
            printf("%s Connected to %s\n", LOG_PREFIX, fifo_read);
            memcpy(trader_list[i].items, item_pos_list, sizeof(trader_list[i].items));


        } else if (pid < 0) {
            perror("Forking failed");
            free_memory(argc, num_products, product_list, trader_list, fifo_read, fifo_write, fd_epoll);;
            return 2;
        }
	}


    // -----------------------(MARKET OPEN)-----------------------
	for (int i = 0; i < num_traders; i++) {
        strcpy(fifo_write_buffer, message_string[MARKET_OPEN]);

        int message_length = strlen(fifo_write_buffer);
        if (write(trader_list[i].trader_fifo_read, fifo_write_buffer, message_length) == -1) {
                perror("Failed to write to fifo");
                free_memory(argc, num_products, product_list, trader_list, fifo_read, fifo_write, fd_epoll);;
                return 2;
        }

        kill(trader_list[i].pid, SIGUSR1);
    }




    // -----------------------(EXCHANGE)-----------------------

    // Wait for trader message
    while (exchange_open) {

        /* USYD CODE CITATION ACKNOWLEDGEMENT
            * I declare that the majority of the following code has been taken from the
            * Introductiion to Unix Signal Programming, 
            * epoll() Tutorial – epoll() In 3 Easy Steps!, 
            * Handle signals with epoll_wait and signalfd and it is not my own work. 
            * 
            * Original URL
            * https://www.cs.kent.edu/~ruttan/sysprog/lectures/signals.html
            * https://suchprogramming.com/epoll-in-3-easy-steps/
            * https://stackoverflow.com/questions/43212106/handle-signals-with-epoll-wait-and-signalfd
            * Last access May, 2023
            */ 

        // mask signals
        sigemptyset(&mask);
        sigaddset(&mask, SIGUSR1); 
        sigprocmask(SIG_BLOCK, &mask, NULL);


        int current_events = epoll_wait(fd_epoll, events, MAX_EVENTS, -1);
        if (current_events == -1) {
            perror("Failed to launch epoll wait");
            return 2;
        }

        for (int pipe = 0; pipe < current_events; pipe++) {
            int fd_read = events[pipe].data.fd;
            Trader* current_trader;

            // signal handler
            if (fd_read == fd_signal) {
                struct signalfd_siginfo signal_info;
                ssize_t bytes_read = read(fd_signal, &signal_info, sizeof(signal_info));
                if (bytes_read != sizeof(signal_info)) {
                    perror("Failed to read signal");
                    continue;
                }
                exchange_signal_handler(SIGUSR1);
                continue;
            }

            
            // identify trader
            for (int i = 0; i < num_traders; i ++) {
                if (trader_list[i].trader_fifo_write == fd_read) {
                    current_trader = &trader_list[i];
                }
            }


            // trader disconnected
            if (events[pipe].events & EPOLLHUP) {
                int disconnect_trader_id = current_trader->trader_number;
                if (epoll_ctl(fd_epoll, EPOLL_CTL_DEL, fd_read, NULL) == -1) {
                    perror("Failed to remove fd_read to event list");
                    free_memory(argc, num_products, product_list, trader_list, fifo_read, fifo_write, fd_epoll);;
                    return 2;
                }
                // remove trader
                for (int i = current_trader->trader_number; i < num_traders - 1; i++) {
                    trader_list[i] = trader_list[i+1];
                }

                disconnect ++;
                num_traders --;
                printf("%s Trader %d disconnected\n", LOG_PREFIX, disconnect_trader_id);


            // read trader message
            } else {

                ssize_t bytes_read = read(fd_read, fifo_read_buffer, sizeof(fifo_read_buffer));
                if (bytes_read == -1) {
                    perror("Failed to read from fifo (event loop)");
                    return 2;
                }
                fifo_read_buffer[bytes_read] = '\0'; //string ends with null


                /* USYD CODE CITATION ACKNOWLEDGEMENT
                * I declare that the majority of the following code has been taken from the
                * website titled: "How to write a memory allocator" and it is not my own work. 
                * 
                * Original URL
                * https://allaboutc.org/how-to-write-a-memory-allocator.html
                * Last access March, 2023
                */ 
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


                // BUY
                if (strcmp(instruction_split[order_type_placement], message_string[BUY]) == 0) {
                    bool valid = false;

                    if (token_count == 5) {
                        printf("%s [T%d] Parsing command: <%s %s %s %s %s>\n", LOG_PREFIX, current_trader->trader_number, 
                        instruction_split[order_type_placement], instruction_split[order_id_placement], instruction_split[product_placement],
                        instruction_split[quantity_placement], instruction_split[price_placement]);
                    } else if (token_count == 3) {
                        char* item = instruction_split[product_placement];
                        item[strlen(item) - 1] = '\0';


                        printf("%s [T%d] Parsing command: <%s %s %s>\n", LOG_PREFIX, current_trader->trader_number, 
                        instruction_split[order_type_placement], instruction_split[order_id_placement], instruction_split[product_placement]);
                    } else if (token_count == 1) {
                        printf("%s [T%d] Parsing command: <%s>\n", LOG_PREFIX, current_trader->trader_number, 
                        instruction_split[order_type_placement]);
                    }
                  
                    // Check order
                    for (int i = 0; i < num_products; i++) {
                        if ((strcmp(product_list[i], instruction_split[product_placement]) == 0) && (atoi(instruction_split[quantity_placement]) > 0) && 
                            (atoi(instruction_split[price_placement]) > 0) && (atoi(instruction_split[price_placement]) <= price_limit) && (atoi(instruction_split[order_id_placement]) == current_trader->number_of_order)) {
                            valid = true;
                        }
                    }


                    if (!valid) {
                        // Inform trader order invalid
                        strcpy(fifo_write_buffer, message_string[INVALID]);

                        int message_length = strlen(fifo_write_buffer);
                        if (write(current_trader->trader_fifo_read, fifo_write_buffer, message_length) == -1) {
                                perror("Failed to write to fifo (main)");
                                free_memory(argc, num_products, product_list, trader_list, fifo_read, fifo_write, fd_epoll);;
                                return 2;
                            
                        }
                        continue;
                    }


                    Order order;

                    order.trader = *current_trader;
                    order.order = BUY;
                    strcpy(order.name, instruction_split[product_placement]);
                    order.trader_order_id = atoi(instruction_split[order_id_placement]);
                    order.quantity = atoi(instruction_split[quantity_placement]);
                    order.price = atoi(instruction_split[price_placement]);
                    order.time = timer;
                    current_trader->number_of_order ++;
                
            
                    // update order list
                    buy_order_list[num_buy_orders] = order;

                    bool found = false;
                    for (int i = 0; i < num_orderbook; i++) {
                        if ((strcmp(item_multiple_list[i].name, order.name) == 0) && 
                            (item_multiple_list[i].price == order.price) && (item_multiple_list[i].order == BUY)) {
                            item_multiple_list[i].count ++;
                            item_multiple_list[i].total += order.quantity;
                            found = true;
                        }
                    }

                    if (!found) {
                        Item_Multiple item;

                        strcpy(item.name, instruction_split[product_placement]);  
                        item.order = BUY;
                        item.price = atoi(instruction_split[price_placement]);
                        item.total = atoi(instruction_split[quantity_placement]);
                        item.count = 1;
                        item_multiple_list[num_orderbook] = item;


                        for (int i = 0; i < num_products; i ++) {
                            if (strcmp(item_list[i].name, instruction_split[product_placement]) == 0) {
                                item_list[i].buy_level ++;
                                num_orderbook ++;
                            }
                        }
                    }


                    // Inform trader order accepted
                    sprintf(fifo_write_buffer, message_string[ACCEPTED], instruction_split[order_id_placement]);

                    int message_length = strlen(fifo_write_buffer);
                    if (write(current_trader->trader_fifo_read, fifo_write_buffer, message_length) == -1) {
                            perror("Failed to write to fifo (main)");
                            free_memory(argc, num_products, product_list, trader_list, fifo_read, fifo_write, fd_epoll);;
                            return 2;
                    }
                    
                    sprintf(fifo_write_buffer, message_string[MARKET_BUY], instruction_split[product_placement], instruction_split[quantity_placement], instruction_split[price_placement]);

                    // Inform all traders
                    message_length = strlen(fifo_write_buffer);
                    for (int i = 0; i < num_traders; i++) {
                        if (trader_list[i].trader_number != current_trader->trader_number) {
                            if (write(trader_list[i].trader_fifo_read, fifo_write_buffer, message_length) == -1) {
                                perror("Failed to write to fifo (other)");
                                free_memory(argc, num_products, product_list, trader_list, fifo_read, fifo_write, fd_epoll);;
                                return 2;
                            }
                        }
                    }

                    num_buy_orders ++;
                    
                // SELL
                } else if (strcmp(instruction_split[order_type_placement], message_string[SELL]) == 0) {
                    bool valid = false;

                    if (token_count == 5) {
                        printf("%s [T%d] Parsing command: <%s %s %s %s %s>\n", LOG_PREFIX, current_trader->trader_number, 
                        instruction_split[order_type_placement], instruction_split[order_id_placement], instruction_split[product_placement],
                        instruction_split[quantity_placement], instruction_split[price_placement]);
                    } else if (token_count == 3) {
                        printf("%s [T%d] Parsing command: <%s %s %s>\n", LOG_PREFIX, current_trader->trader_number, 
                        instruction_split[order_type_placement], instruction_split[order_id_placement], instruction_split[product_placement]);
                    } else if (token_count == 1) {
                        printf("%s [T%d] Parsing command: <%s>\n", LOG_PREFIX, current_trader->trader_number, 
                        instruction_split[order_type_placement]);
                    }
                  
                   // Check order
                    for (int i = 0; i < num_products; i++) {
                        if ((strcmp(product_list[i], instruction_split[product_placement]) == 0) && (atoi(instruction_split[quantity_placement]) > 0) && 
                            (atoi(instruction_split[price_placement]) > 0) && (atoi(instruction_split[price_placement]) <= price_limit) && (atoi(instruction_split[order_id_placement]) == current_trader->number_of_order)) {
                            valid = true;
                        }
                    }


                    if (!valid) {
                        // Inform trader order invalid
                        strcpy(fifo_write_buffer, message_string[INVALID]);

                        int message_length = strlen(fifo_write_buffer);
                        if (write(current_trader->trader_fifo_read, fifo_write_buffer, message_length) == -1) {
                                perror("Failed to write to fifo (main)");
                                free_memory(argc, num_products, product_list, trader_list, fifo_read, fifo_write, fd_epoll);;
                                return 2;
                            
                        }
                        continue;
                    }
               
                    Order order;

                    order.trader = *current_trader;
                    order.order = SELL;
                    strcpy(order.name, instruction_split[product_placement]);
                    order.trader_order_id = atoi(instruction_split[order_id_placement]);
                    order.quantity = atoi(instruction_split[quantity_placement]);
                    order.price = atoi(instruction_split[price_placement]);
                    order.time = timer;
                    current_trader->number_of_order ++;


                    // update order list
                    sell_order_list[num_sell_orders] = order;

                    bool found = false;
                    for (int i = 0; i < num_orderbook; i++) {
                        if ((strcmp(item_multiple_list[i].name, order.name) == 0) && 
                            (item_multiple_list[i].price == order.price) && (item_multiple_list[i].order == SELL)) {
                            item_multiple_list[i].count ++;
                            item_multiple_list[i].total += order.quantity;
                            found = true;
                        }
                    }


                    if (!found) {
                        Item_Multiple item;

                        strcpy(item.name, instruction_split[product_placement]);  
                        item.order = SELL;
                        item.price = atoi(instruction_split[price_placement]);
                        item.total = atoi(instruction_split[quantity_placement]);
                        item.count = 1;
                        item_multiple_list[num_orderbook] = item;


                        for (int i = 0; i < num_products; i ++) {
                            if (strcmp(item_list[i].name, instruction_split[product_placement]) == 0) {
                                item_list[i].sell_level ++; 
                                num_orderbook ++;
                            }
                        }
                    }

                    
                    // Inform trader order accepted
                    sprintf(fifo_write_buffer, message_string[ACCEPTED], instruction_split[order_id_placement]);

                    int message_length = strlen(fifo_write_buffer);
                    if (write(current_trader->trader_fifo_read, fifo_write_buffer, message_length) == -1) {
                            perror("Failed to write to fifo");
                            free_memory(argc, num_products, product_list, trader_list, fifo_read, fifo_write, fd_epoll);;
                            return 2;
                    }

                    sprintf(fifo_write_buffer, message_string[MARKET_SELL], instruction_split[product_placement], instruction_split[quantity_placement], instruction_split[price_placement]);

                    // Inform all traders
                    message_length = strlen(fifo_write_buffer);
                    for (int i = 0; i < num_traders; i++) {
                        if (trader_list[i].trader_number != current_trader->trader_number) {
                            if (write(trader_list[i].trader_fifo_read, fifo_write_buffer, message_length) == -1) {
                                perror("Failed to write to fifo");
                                free_memory(argc, num_products, product_list, trader_list, fifo_read, fifo_write, fd_epoll);
                                return 2;
                            }
                        }
                    }

                    num_sell_orders ++;


                // CANCEL  
                } else if (strcmp(instruction_split[order_type_placement], message_string[CANCEL]) == 0) {
                    // fixing id
                    char* id = instruction_split[order_id_placement];
                    id[strlen(id) - 1] = '\0';

                    printf("%s [T%d] Parsing command: <%s %s>\n", LOG_PREFIX, current_trader->trader_number, 
                        instruction_split[order_type_placement], instruction_split[order_id_placement]);
                    
                    Order order_remove;
                    
                    // --update orders--
                    bool found = false;
                    int order_index = 0;
                    for (int i = 0; i < num_buy_orders; i++) {
                        if (buy_order_list[i].trader_order_id == atoi(instruction_split[order_id_placement]) &&
                            buy_order_list[i].trader.trader_number == current_trader->trader_number) {
                            order_index = i;
                            found = true;
                        }
                    }

                    if (!found) {
                        for (int i = 0; i < num_sell_orders; i++) {
                            if (sell_order_list[i].trader_order_id == atoi(instruction_split[order_id_placement]) &&
                                sell_order_list[i].trader.trader_number == current_trader->trader_number) {
                                order_index = i;
                            }
                        }
                    }

                    if (found) {
                        order_remove = buy_order_list[order_index];
                    } else {
                        order_remove = sell_order_list[order_index];
                    }

                    // --update orderbook--
                    bool remove = false;
                    int book_index = 0;
                    for (int i = 0; i < num_orderbook; i++) {
                        if ((strcmp(item_multiple_list[i].name, order_remove.name) == 0) && (item_multiple_list[i].price ==  order_remove.price)) {
                            item_multiple_list[i].total -= order_remove.quantity;
                            item_multiple_list[i].count --;
                            book_index = i;

                            if (item_multiple_list[i].count == 0) {
                                remove = true;
                            }
                        }
                    }

                    if (remove) {
                        for (int i = book_index; i < num_orderbook; i++) {
                            item_multiple_list[i] = item_multiple_list[i+1];
                        }
                        num_orderbook --;
                    }

                    // --update items--
                    for (int i = 0; i < num_products; i++) {
                        if (strcmp(item_list[i].name, order_remove.name) == 0) {
                            if (found) {
                                item_list[i].buy_level --;
                            } else {
                                item_list[i].sell_level --;
                            }
                        }
                    } 

                    // --update orders--
                    if (found) {
                        for (int i = order_index; i < num_buy_orders; i++) {
                            buy_order_list[i] = buy_order_list[i+1];
                        }
                        num_buy_orders --;
                    } else {
                        for (int i = order_index; i < num_sell_orders; i++) {
                            sell_order_list[i] = sell_order_list[i+1];
                        }
                        num_sell_orders --;
                    }

                       
                    // Inform trader order cancelled
                    sprintf(fifo_write_buffer, message_string[CANCELLED], atoi(instruction_split[order_id_placement]));

                    int message_length = strlen(fifo_write_buffer);
                    if (write(current_trader->trader_fifo_read, fifo_write_buffer, message_length) == -1) {
                            perror("Failed to write to fifo");
                            free_memory(argc, num_products, product_list, trader_list, fifo_read, fifo_write, fd_epoll);;
                            return 2;
                    }

                    if (order_remove.order == BUY) {
                        sprintf(fifo_write_buffer, message_string[MARKET_BUY], order_remove.name, "0" ,"0");
                    } else {
                        sprintf(fifo_write_buffer, message_string[MARKET_SELL], order_remove.name, "0" ,"0");
                    }
                   
                    // Inform all traders
                    message_length = strlen(fifo_write_buffer);
                    for (int i = 0; i < num_traders; i++) {
                        if (trader_list[i].trader_number != current_trader->trader_number) {
                            if (write(trader_list[i].trader_fifo_read, fifo_write_buffer, message_length) == -1) {
                                perror("Failed to write to fifo");
                                free_memory(argc, num_products, product_list, trader_list, fifo_read, fifo_write, fd_epoll);;
                                return 2;
                            }
                        }
                    }

                   

                // AMEND
                } else if (strcmp(instruction_split[order_type_placement], message_string[AMEND]) == 0) {
                    // fixing id
                    char* qty = instruction_split[quantity_placement];
                    qty[strlen(qty) - 1] = '\0';

                    printf("%s [T%d] Parsing command: <%s %s %s %s>\n", LOG_PREFIX, current_trader->trader_number, instruction_split[order_type_placement], 
                        instruction_split[order_id_placement], instruction_split[quantity_placement_amend], instruction_split[price_placement_amend]);
                    
                    Order order_old;
                    

                     // --update orders--
                    bool found = false;
                    int order_index = 0;
                    for (int i = 0; i < num_buy_orders; i++) {
                        if (buy_order_list[i].trader_order_id == atoi(instruction_split[order_id_placement]) &&
                            buy_order_list[i].trader.trader_number == current_trader->trader_number) {
                            order_index = i;
                            found = true;
                        }
                    }

                    if (!found) {
                        for (int i = 0; i < num_sell_orders; i++) {
                            if (sell_order_list[i].trader_order_id == atoi(instruction_split[order_id_placement]) &&
                                sell_order_list[i].trader.trader_number == current_trader->trader_number) {
                                order_index = i;
                            }
                        }
                    }

                    if (found) {
                        order_old = buy_order_list[order_index];
                    } else {
                        order_old = sell_order_list[order_index];
                    }


                    // --update orderbook--
                    for (int i = 0; i < num_orderbook; i++) {
                        if ((strcmp(item_multiple_list[i].name, order_old.name) == 0) && (item_multiple_list[i].price ==  order_old.price)) {
                            item_multiple_list[i].total = atoi(instruction_split[quantity_placement_amend]);
                            item_multiple_list[i].price = atoi(instruction_split[price_placement_amend]);
                        }
                    }

                    // --update orders--
                    if (found) {
                        buy_order_list[order_index].quantity = atoi(instruction_split[quantity_placement_amend]);
                        buy_order_list[order_index].price = atoi(instruction_split[price_placement_amend]);
                    } else {
                        sell_order_list[order_index].quantity = atoi(instruction_split[quantity_placement_amend]);
                        sell_order_list[order_index].price = atoi(instruction_split[price_placement_amend]);
                       
                    }


                     // Inform trader order ammended
                    sprintf(fifo_write_buffer, message_string[AMENDED], atoi(instruction_split[order_id_placement]));

                    int message_length = strlen(fifo_write_buffer);
                    if (write(current_trader->trader_fifo_read, fifo_write_buffer, message_length) == -1) {
                            perror("Failed to write to fifo");
                            free_memory(argc, num_products, product_list, trader_list, fifo_read, fifo_write, fd_epoll);;
                            return 2;
                    }

                    if (order_old.order == BUY) {
                        sprintf(fifo_write_buffer, message_string[MARKET_BUY], order_old.name, instruction_split[quantity_placement_amend], instruction_split[price_placement_amend]);
                    } else {
                        sprintf(fifo_write_buffer, message_string[MARKET_SELL], order_old.name, instruction_split[quantity_placement_amend], instruction_split[price_placement_amend]);
                    }
                   
                    // Inform all traders
                    message_length = strlen(fifo_write_buffer);
                    for (int i = 0; i < num_traders; i++) {
                        if (trader_list[i].trader_number != current_trader->trader_number) {
                            if (write(trader_list[i].trader_fifo_read, fifo_write_buffer, message_length) == -1) {
                                perror("Failed to write to fifo");
                                free_memory(argc, num_products, product_list, trader_list, fifo_read, fifo_write, fd_epoll);;
                                return 2;
                            }
                        }
                    }

            


                    

                    

                }

                matching(trader_list, item_multiple_list, buy_order_list, sell_order_list, item_list, fifo_write_buffer, &num_traders, &num_traders_final, &num_products, &num_orderbook, &num_buy_orders, &num_sell_orders, &exchange_fee);
                orderbook(item_multiple_list, item_list, num_orderbook, num_products);
                position(trader_list, num_traders_final, num_products);

            }
        }
        // Unblock signals
        sigprocmask(SIG_UNBLOCK, &mask, NULL);

        if (disconnect == (num_traders_final)) {break;}
    }



    // -----------------------(CLEANING)-----------------------
    printf("%s Trading completed\n", LOG_PREFIX);
    printf("%s Exchange fees collected: $%d\n", LOG_PREFIX, exchange_fee);
    return free_memory(argc, num_products, product_list, trader_list, fifo_read, fifo_write, fd_epoll);;

}














// -----------------------(SIGNAL HANDLER)-----------------------

void exchange_signal_handler(int sig) {
    timer ++;
}



// -----------------------(FUNCTION)-----------------------

int free_memory(int argc, int num_products, char** product_list, Trader trader_list[BUFFER_SIZE], char fifo_read[BUFFER_SIZE], char fifo_write[BUFFER_SIZE], int fd_epoll) {

    for (int i = 0; i < num_products; i++) {
        free(product_list[i]);
    }
    free(product_list);



    for (int i = 0; i < argc - 2; i++) {
        close(trader_list[i].trader_fifo_read);
        close(trader_list[i].trader_fifo_write);

        sprintf(fifo_read, FIFO_TRADER, i);
		sprintf(fifo_write, FIFO_EXCHANGE, i);
    
        if (unlink(fifo_read) != 0) {
            perror("Failed to remove fifo read file");
            return 2;
        }
        if (unlink(fifo_write) != 0) {
            perror("Failed to remove fifo write file");
            return 2;
        }
    }

    close(fd_epoll);
    return 0;
}

void orderbook(Item_Multiple item_multiple_list[BUFFER_SIZE], Item item_list[BUFFER_SIZE], int num_orderbook, int num_products) {
    printf("%s\t--ORDERBOOK--\n", LOG_PREFIX);
    qsort(item_multiple_list, num_orderbook, sizeof(Item_Multiple), compare_item_multiple);

    for (int i = 0; i < num_products; i++) {
        printf("%s\tProduct: %s; Buy levels: %d; Sell levels: %d\n", LOG_PREFIX, item_list[i].name, item_list[i].buy_level, item_list[i].sell_level);

        for (int j = 0; j < num_orderbook; j++) {
            if (strcmp(item_list[i].name, item_multiple_list[j].name) == 0) {
                printf("%s\t\t%s %d @ $%d ", LOG_PREFIX, message_string[item_multiple_list[j].order], item_multiple_list[j].total, item_multiple_list[j].price);

                if (item_multiple_list[j].count > 1) {
                    printf("(%d orders)\n", item_multiple_list[j].count);
                } else {
                    printf("(%d order)\n", item_multiple_list[j].count);
                }
            }
        }
    }
}

void position(Trader trader_list[BUFFER_SIZE], int num_traders, int num_products) {
    printf("%s\t--POSITIONS--\n", LOG_PREFIX);

    for (int i = 0; i < num_traders; i++) {
        printf("%s\tTrader %d: ", LOG_PREFIX, trader_list[i].trader_number);

        for (int j = 0; j < num_products; j++) {
            if (j == num_products -1) {
                printf("%s %d ($%d)\n", trader_list[i].items[j].name, trader_list[i].items[j].quantity, trader_list[i].items[j].price);
            } else {
                printf("%s %d ($%d), ", trader_list[i].items[j].name, trader_list[i].items[j].quantity, trader_list[i].items[j].price);
            }
        }
    }
}

void matching(Trader trader_list[BUFFER_SIZE], Item_Multiple item_multiple_list[BUFFER_SIZE], Order buy_order_list[BUFFER_SIZE], Order sell_order_list[BUFFER_SIZE],
                Item item_list[BUFFER_SIZE], char fifo_write_buffer[BUFFER_SIZE], int* num_traders, int* num_traders_final, int* num_products, int* num_orderbook, int* num_buy_orders, int* num_sell_orders, int* exchange_fee) {

    qsort(buy_order_list, *num_buy_orders, sizeof(Order), compare_order_price);
    qsort(sell_order_list, *num_sell_orders, sizeof(Order), compare_order_price);
    int quantity_sold = 0;
    bool last_sell = false;
    bool end_reached = false;
    bool sell_is_ready = false;
    bool leftover = false;
    float fee_percent = FEE_PERCENTAGE / 100.0;


    if ((*num_sell_orders > 0) && (*num_buy_orders > 0)) {
        sell_is_ready = true;
    }

    while (sell_is_ready) {
        if ((last_sell) && (!end_reached)) {
            break;
        }

        if (*num_sell_orders == 0 || *num_buy_orders == 0) {
            break;
        }

        for (int i = 0; i < *num_sell_orders; i++) {     

            Order* current_seller = &sell_order_list[i];
            Order* final_buyer;
            float matching_fee = 0;
            int value = 0;
            end_reached = false;
      

            if (i == *num_sell_orders - 1) {
                last_sell = true;
            }

            // -----------------------(MATCHING)-----------------------
            Order* higest_initial_buyer = NULL;
            Order potential_buyers[BUFFER_SIZE] = {0};
            int num_potential_buyers = 0;

            // check if there is buyers
            for (int j = 0; j < *num_buy_orders; j++) {
                if (strcmp(buy_order_list[j].name, current_seller->name) == 0) {
                    higest_initial_buyer = &buy_order_list[j];
                    break;
                }
            }

            // no buyers
            if (higest_initial_buyer == NULL) {
                sell_is_ready = false;
                continue;

            } else if (current_seller->price > higest_initial_buyer->price) {
                continue;

            } else {
                potential_buyers[0] = *(higest_initial_buyer);
                num_potential_buyers ++;

                // getting buyer equal to highest initial buyer
                for (int j = 0; j < *num_buy_orders; j++) {
                    if ((higest_initial_buyer->price == buy_order_list[j].price) && (strcmp(higest_initial_buyer->name, buy_order_list[j].name) == 0) &&
                        (buy_order_list[j].trader.trader_number != higest_initial_buyer->trader.trader_number)) {
                        potential_buyers[num_potential_buyers] = buy_order_list[j];
                        num_potential_buyers ++;
                    }
                }

                // No other buyer match
                if (num_potential_buyers == 1) {
                    final_buyer = higest_initial_buyer;

                // compare by time
                } else {
                    qsort(potential_buyers, num_potential_buyers, sizeof(Order), compare_order_time);
                    final_buyer = &buy_order_list[0];

                }
                
                // -----------------------(TRADING)-----------------------
                int buyer_time = final_buyer->time;
                int sell_time = current_seller->time;
                int quantity_exchange = 0;
                Order* first_print;
                Order* second_print;
                if (buyer_time < sell_time) {
                    first_print = final_buyer;
                    second_print = current_seller;

                } else {
                    first_print = current_seller;
                    second_print = final_buyer;
                }

                if (current_seller->quantity - final_buyer->quantity >= 0) {

                    if (buyer_time < sell_time) {
                        value = final_buyer->quantity * final_buyer->price;
                    } else {
                        value = final_buyer->quantity * current_seller->price;
                    }
                    
                    quantity_exchange = final_buyer->quantity;
                } else {

                    if (buyer_time < sell_time) {
                        value = current_seller->quantity * final_buyer->price;
                    } else {
                        value = current_seller->quantity * current_seller->price;
                    }

                    quantity_exchange = current_seller->quantity;
                    leftover = true;
                }
                matching_fee = round(value * fee_percent);
                *(exchange_fee) += (int) matching_fee;
                quantity_sold = current_seller->quantity;

                
                printf("%s Match: Order %d [T%d], New Order %d [T%d], value: $%d, fee: $%.0f.\n", LOG_PREFIX, first_print->trader_order_id, first_print->trader.trader_number,
                        second_print->trader_order_id, second_print->trader.trader_number, value, matching_fee);
                
               
                current_seller->quantity -= final_buyer->quantity;


                // Sending fill
                for (int j = 0; j < *num_traders; j++) {
                    // Inform buyer order accepted
                    if (trader_list[j].trader_number == final_buyer->trader.trader_number) {
                        sprintf(fifo_write_buffer, message_string[FILL], final_buyer->trader_order_id, quantity_exchange);

                        int message_length = strlen(fifo_write_buffer);
                        if (write(final_buyer->trader.trader_fifo_read, fifo_write_buffer, message_length) == -1) {
                                perror("Failed to write to fifo (buyer)");
                        }
                    }

                    // Inform seller order accepted
                    if (trader_list[j].trader_number == current_seller->trader.trader_number) {
                        sprintf(fifo_write_buffer, message_string[FILL], current_seller->trader_order_id, quantity_exchange);

                        int message_length = strlen(fifo_write_buffer);
                        if (write(current_seller->trader.trader_fifo_read, fifo_write_buffer, message_length) == -1) {
                                perror("Failed to write to fifo (seller)");
                        }
                    }
                }

                // -----------------------(UPDATE DETAILS)-----------------------
                int position_buy = 0;
                int position_sell = 0;

                // --update orderbook--
                // position orderbook for BUY
                for (int j = 0; j < *num_orderbook; j++) {
                    if ((strcmp(item_multiple_list[j].name, final_buyer->name) == 0) && (item_multiple_list[j].price == final_buyer->price) 
                            && (item_multiple_list[j].order == BUY)) {
                        position_buy = j;
                        break;
                    }
                }

                // position orderbook for SELL
                for (int j = 0; j < *num_orderbook; j++) {
                    if ((strcmp(item_multiple_list[j].name, current_seller->name) == 0) && (item_multiple_list[j].price == current_seller->price) 
                        && (item_multiple_list[j].order == SELL)) {
                        position_sell = j;
                        break;
                    }
                }
              
              

                // remove buy order (if allowed)
                if (item_multiple_list[position_buy].count >= 1) {

                    if (!leftover) {
                        item_multiple_list[position_buy].count -= 1;
                        
                        if (item_multiple_list[position_buy].count == 0) {
                            for (int j = position_buy; j < *num_orderbook -1; j++) {
                                item_multiple_list[j] = item_multiple_list[j+1];
                            }
                            (*num_orderbook) --;

                            for (int j = 0; j < *num_products; j++) {
                                if (strcmp(item_list[j].name, current_seller->name) == 0) {
                                    item_list[j].buy_level -= 1;
                                }
                            }

                            if (position_sell > 0) {
                                position_sell --;
                            }
                        }
                    } else {
                        item_multiple_list[position_buy].total -= quantity_sold;
                    }
                }
    

                // remove sell order (if allowed)
                if (current_seller->quantity <= 0) {
                    item_multiple_list[position_sell].count -= 1;

                    if (item_multiple_list[position_sell].count == 0) {
                        for (int j = position_sell; j < *num_orderbook -1; j++) {
                            item_multiple_list[j] = item_multiple_list[j+1];
                        }
                        (*num_orderbook) --;
                        
                        for (int j = 0; j < *num_products; j++) {
                            if (strcmp(item_list[j].name, current_seller->name) == 0) {
                                item_list[j].sell_level -= 1;
                            }
                        }
                    }
                } else {
                    item_multiple_list[position_sell].total -= quantity_exchange;
                }
           
               

                // --update orders--
                // postion order for BUY
                for (int j = 0; j < *num_buy_orders; j++) {
                    if (final_buyer->trader_order_id == buy_order_list[j].trader_order_id) {
                        position_buy = j;
                        break;
                    }
                }

                // update buyer list (if allowed)
                if (!leftover) {
                    for (int j = position_buy; j < *num_buy_orders - 1; j++) {
                        buy_order_list[j] = buy_order_list[j + 1];
                    }
                    (*num_buy_orders) --;
                }


                // update sell list (if allowed)
                position_sell = i;
                if (current_seller->quantity < 0) {
                    (*num_sell_orders) --;

                    for (int j = position_sell; j < *num_sell_orders -1; j++) {
                        sell_order_list[j] = sell_order_list[j+1];
                    }
                }
                


                // --update position--
                bool pay = 0;

                // 0 is buyer and 1 is seller
                if (buyer_time < sell_time) {
                    pay = 1;
                }

                for (int j = 0; j < *num_traders_final; j++) {

                    // seller
                    if (trader_list[j].trader_number == current_seller->trader.trader_number) {
                        for (int k = 0; k < *num_products; k++) {
                            if (strcmp(trader_list[j].items[k].name, current_seller->name) == 0) {
                                trader_list[j].items[k].quantity -= quantity_exchange;
                                trader_list[j].items[k].price += value;

                                if (pay == 1) {
                                    trader_list[j].items[k].price -= (int) matching_fee;
                                }
                            }
                        }
                    }

                    // buyer
                    if (trader_list[j].trader_number == final_buyer->trader.trader_number) {
                        for (int k = 0; k < *num_products; k++) {
                            if (strcmp(trader_list[j].items[k].name, current_seller->name) == 0) {
                                trader_list[j].items[k].quantity += quantity_exchange;
                                trader_list[j].items[k].price -= value;
                            
                                if (pay == 0) {
                                    trader_list[j].items[k].price -= (int) matching_fee;
                                }
                            } 
                        }
                    }
                }
                end_reached = true;
            }
        }
    }
}


 /* USYD CODE CITATION ACKNOWLEDGEMENT
    * I declare that the majority of the following code has been taken from the
    * Tutoral answer week 6 and it is not my own work. 
    * 
    * Original URL
    * https://edstem.org/au/courses/10466/resources?download=15510
    * Last access May, 2023
    */ 

int compare_item_multiple(const void* a, const void* b) {
    Item_Multiple* item_a = (Item_Multiple*) a;
    Item_Multiple* item_b = (Item_Multiple*) b;
 
    return item_a->price < item_b->price;
}


int compare_order_price(const void* a, const void* b) {
    Order* item_a = (Order*) a;
    Order* item_b = (Order*) b;

    return item_a->price < item_b->price;
}


int compare_order_time(const void* a, const void* b) {
    Order* item_a = (Order*) a;
    Order* item_b = (Order*) b;

    return item_a->time > item_b->time;
}