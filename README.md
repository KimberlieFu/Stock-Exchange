1. Describe how your exchange works.

    The exchange will start by initiating all the vairables followed starting a signal handler and epoll. During initiation, fd_epoll is 
    created (epoll_create1(0)) to monitor any changes in the traders' pipes and any signal that is received. After all initializations are 
    made the exchange will start by reading the product file for it know what items are being traded and print to stdout. While file 
    reading, the items will be dynamically allocated using malloc with respect to the number of items and another malloc how the length of 
    the string. Next, the exchange will start making named pipes for each trader then start initializing each trader's binar with the use 
    of forking with the child processes. The child processes will send a SIGUSR1 signal to parent to indicate that trader is running. The 
    parent will then connect to the traders named pipes while adding it to files to monitor in epoll. Once all the traders binaries are 
    running, the exchange will send a market open message to all traders. 

    Epoll will be the main way of reading the traders messages to exchange. Epoll events are saved in current_events using epoll_wait where 
    it waits for any file descriptors that has changed then the for loop will process each traders pipes. To prevent any signal to 
    interrupt the processing a sigprocmask is made to handle signals in a orderly manner. Within the loop there are checks for the trader's 
    pipes. The first check is if the event is a signal then it will call signal handler. Next check is if any traders are disconnected then 
    remove from monitoring them. The last is the reading message in the traders' pipes and processing it. For each processing it will enter an 
    if block depending on order type. If its a BUY or SELL block, it checks the order is valid if so it makes a new struct order that contains 
    order detials followed by adding it to the orderbook list (item_multiple_list) then it send the messages to traders. If it is a CANCEL
    order, it simply remove the order from order list and orderbook list. Finally if it is an AMEND order then it updates the requested order.

    After the order is processed, it goes to the function matching to check any order matches so far. The matching will first sort the buy and 
    sell orders in decreasing order the check if there is at least one BUY and SELL order then it will enter the while loop. Within the loop it
    will first interate through all the sell orders. For each sell order, there is set an higest_initial_buyer that is the highest price then 
    it checks if there are any orders that matches the higest_initial_buyer's price. If there is no other order can match it then 
    higest_initial_buyer is set as the final_buyer, otherwise the earliest order will be the final_buyer. Once the final_buyer is selected, the
    exchange will send the message to both traders and stdout then it will update orderbook list, order list, and the position of both traders.
    The next two functions are similar that is orderbook and position. Where it prints the orders currently and traders' positions.

    Finally, the exchange will terminate once all traders are disconnected.



2. Describe your design decisions for the trader and how it's fault-tolerant.

    The trader fault-torlerant in terms of processing a message from exchange because it can not get interupted by signal during processing with 
    the use of sigprocmask which block all signals. 




3. Describe your tests and how to run them.

    The testcases I made are end to end .out files to test the exchange.
