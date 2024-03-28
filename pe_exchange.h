#ifndef PE_EXCHANGE_H
#define PE_EXCHANGE_H

#include "pe_common.h"
#include <errno.h>
#include <math.h>
#include <libgen.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>

#define LOG_PREFIX "[PEX]"
#define BUFFER_SIZE 128
#define STRING_SIZE 16
#define MAX_EVENTS 20

#define order_type_placement 0
#define order_id_placement 1
#define product_placement 2
#define quantity_placement 3
#define price_placement 4

#define quantity_placement_amend 2
#define price_placement_amend 3

#define price_limit 100000


enum message_enum {
    MARKET_OPEN ,
    INVALID ,
    ACCEPTED ,
    MARKET_BUY ,
    MARKET_SELL ,
    FILL ,
    CANCELLED ,
    AMENDED ,
    BUY ,
    SELL ,
    AMEND ,
    CANCEL 
};

const char* message_string[] = {
    "MARKET OPEN;" ,
    "INVALID;" ,
    "ACCEPTED %s;" ,
    "MARKET BUY %s %s %s;" ,
    "MARKET SELL %s %s %s;" ,
    "FILL %d %d;" ,
    "CANCELLED %d;" ,
    "AMENDED %d;" ,
    "BUY" ,
    "SELL" ,
    "AMEND" ,
    "CANCEL" 
};


typedef struct item_position {
    char name[STRING_SIZE]; 
    int quantity;
    int price;
} Item_Position;

typedef struct trader {
    int trader_number;
    int number_of_order;
    pid_t pid;
    int trader_fifo_read;
    int trader_fifo_write;
    Item_Position items[BUFFER_SIZE];
} Trader;

typedef struct order {
    Trader trader;
    enum message_enum order;
    char name[STRING_SIZE];
    int trader_order_id; 
    int quantity;
    int price;
    int time;
} Order;

typedef struct item {
    char name[STRING_SIZE]; 
    int sell_level;
    int buy_level;
} Item;

typedef struct item_multiple {
    char name[STRING_SIZE]; 
    enum message_enum order;
    int price;
    int total;
    int count;
} Item_Multiple;


#endif