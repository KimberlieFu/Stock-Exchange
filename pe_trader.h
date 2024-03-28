#ifndef PE_TRADER_H
#define PE_TRADER_H

#include "pe_common.h"
#include <stdbool.h>

#define BUFFER_SIZE 256
#define order_type_placement 1
#define product_placement 2
#define quantity_placement 3
#define price_placement 4
#define quantity_limit 1000

char delimiter[] = ";";


enum message_enum {
    MARKET_OPEN ,
    BUY ,
    SELL ,
    AMEND ,
    CANCEL
};

char* message_string[] = {
    "MARKET OPEN;",
    "BUY" ,
    "SELL" ,
    "AMEND" ,
    "CANCEL"
};



#endif