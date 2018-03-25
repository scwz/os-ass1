#include <types.h>
#include <lib.h>
#include <synch.h>
#include <test.h>
#include <thread.h>

#include "bar.h"
#include "bar_driver.h"



/*
 * **********************************************************************
 * YOU ARE FREE TO CHANGE THIS FILE BELOW THIS POINT AS YOU SEE FIT
 *
 */

/* Declare any globals you need here (e.g. locks, etc...) */
/* Fixed buffer and its properties */
struct barorder *orders[NCUSTOMERS];
unsigned int orders_head, orders_size;

/* Synchronisation primitives */
struct lock *order_lock, *bottle_lock[NBOTTLES], *cust_lock;
struct cv *orders_full, *orders_empty, *cv_cust[NCUSTOMERS];

/* Helper functions */
void sort(unsigned int bottles[DRINK_COMPLEXITY]);
void insert_order(struct barorder *order);
struct barorder *remove_order(void);

/*
 * **********************************************************************
 * FUNCTIONS EXECUTED BY CUSTOMER THREADS
 * **********************************************************************
 */

/*
 * order_drink()
 *
 * Takes one argument referring to the order to be filled. The
 * function makes the order available to staff threads and then blocks
 * until a bartender has filled the glass with the appropriate drinks.
 */

void order_drink(struct barorder *order)
{
        lock_acquire(order_lock);

        /* wait until orders queue has space */
        while (orders_size == NCUSTOMERS) {
                cv_wait(orders_full, order_lock);
        }

        /* add a new order to the orders queue */
        insert_order(order);

        /* signal that there is an order available to take */
        if (orders_size == 1)  {
                cv_broadcast(orders_empty, order_lock);
        }

        lock_release(order_lock);

        lock_acquire(cust_lock);

        /* block customer until their order has been fulfilled */
        while (!order->order_fulfilled) {
                cv_wait(cv_cust[order->cust_id], cust_lock);
        }

        lock_release(cust_lock);
}



/*
 * **********************************************************************
 * FUNCTIONS EXECUTED BY BARTENDER THREADS
 * **********************************************************************
 */

/*
 * take_order()
 *
 * This function waits for a new order to be submitted by
 * customers. When submitted, it returns a pointer to the order.
 *
 */

struct barorder *take_order(void)
{
        struct barorder *ret;

        lock_acquire(order_lock);

        /* wait until there is an available order to take */
        while (orders_size == 0) {
                cv_wait(orders_empty, order_lock);
        }

        /* take the order */
        ret = remove_order();

        /* signal that there is space in the orders queue */
        if (orders_size == NCUSTOMERS - 1) {
                cv_broadcast(orders_full, order_lock);
        }

        lock_release(order_lock);

        return ret;
}

/*
 * fill_order()
 *
 * This function takes an order provided by take_order and fills the
 * order using the mix() function to mix the drink.
 *
 * NOTE: IT NEEDS TO ENSURE THAT MIX HAS EXCLUSIVE ACCESS TO THE
 * REQUIRED BOTTLES (AND, IDEALLY, ONLY THE BOTTLES) IT NEEDS TO USE TO
 * FILL THE ORDER.
 */

void fill_order(struct barorder *order)
{
        /* add any sync primitives you need to ensure mutual exclusion
           holds as described */

        unsigned int bottle;
        unsigned int *bottles = order->requested_bottles;
        bool locked_bottles[NBOTTLES] = {false};

        sort(bottles);

        /* acquire locks for the drinks included in order */
        for (int i = 0; i < DRINK_COMPLEXITY; i++) {
                bottle = bottles[i] - 1;
                if (bottles[i] && !locked_bottles[bottle]) {
                        lock_acquire(bottle_lock[bottle]);
                        locked_bottles[bottle] = true;
                }
        }

        /* the call to mix must remain */
        mix(order);

        /* release the locks in reverse order */
        for (int i = DRINK_COMPLEXITY - 1; i >= 0; i--) {
                bottle = bottles[i] - 1;
                if (bottles[i] && locked_bottles[bottle]) {
                        locked_bottles[bottle] = false;
                        lock_release(bottle_lock[bottle]);
                }
        }
}


/*
 * serve_order()
 *
 * Takes a filled order and makes it available to (unblocks) the
 * waiting customer.
 */

void serve_order(struct barorder *order)
{
        lock_acquire(cust_lock);

        /* signal that the order has been fulfilled and unblock the customer */
        order->order_fulfilled = true;
        cv_signal(cv_cust[order->cust_id], cust_lock);

        lock_release(cust_lock);
}



/*
 * **********************************************************************
 * INITIALISATION AND CLEANUP FUNCTIONS
 * **********************************************************************
 */


/*
 * bar_open()
 *
 * Perform any initialisation you need prior to opening the bar to
 * bartenders and customers. Typically, allocation and initialisation of
 * synch primitive and variable.
 */

void bar_open(void)
{
        for (int i = 0; i < NCUSTOMERS; i++) {
                orders[i] = NULL;
                cv_cust[i] = cv_create("cv_cust");
                if (cv_cust[i] == NULL) {
                        panic("bar: failed to create cv");
                }
        }

        for (int i = 0; i < NBOTTLES; i++)  {
                bottle_lock[i] = lock_create("bottle_lock");
                if (bottle_lock[i] == NULL) {
                        panic("bar: failed to create lock");
                }
        }

        order_lock = lock_create("order_lock"); 
        if (order_lock == NULL) {
                panic("bar: failed to create lock");
        }

        cust_lock = lock_create("cust_lock");
        if (cust_lock == NULL) {
                panic("bar: failed to create lock");
        }

        orders_full = cv_create("orders_full"); 
        if (orders_full == NULL) {
                panic("bar: failed to create cv");
        }

        orders_empty = cv_create("orders_empty");
        if (orders_empty == NULL) {
                panic("bar: failed to create cv");
        }

        orders_head = 0;
        orders_size = 0;
}

/*
 * bar_close()
 *
 * Perform any cleanup after the bar has closed and everybody
 * has gone home.
 */

void bar_close(void)
{
        for (int i = 0; i < NCUSTOMERS; i++) {
                cv_destroy(cv_cust[i]);
        }

        for (int i = 0; i < NBOTTLES; i++)  {
                lock_destroy(bottle_lock[i]);
        }

        lock_destroy(order_lock); 
        lock_destroy(cust_lock);

        cv_destroy(orders_full); 
        cv_destroy(orders_empty);
}

/*
 * **********************************************************************
 * HELPER FUNCTIONS
 * **********************************************************************
 */

/*
 * sort()
 *
 * Sorts the requested bottles in ascending order.
 * Adapted from the pseudocode at https://en.wikipedia.org/wiki/Counting_sort 
 *
 */

void sort(unsigned int bottles[DRINK_COMPLEXITY])
{
        unsigned int count[NBOTTLES+1] = {0};
        unsigned int output[DRINK_COMPLEXITY] = {0};
        int total = 0, old_count;

        /* calculate frequency of each drink */
        for (int i = 0; i < DRINK_COMPLEXITY; i++) {
                count[bottles[i]]++;
        }

        /* calculate starting index for each drink */
        for (int i = 0; i < NBOTTLES+1; i++) {
                old_count = count[i];
                count[i] = total;
                total += old_count;
        }

        /* copy to output array */
        for (int i = 0; i < DRINK_COMPLEXITY; i++) {
                output[count[bottles[i]]] = bottles[i];
                count[bottles[i]]++;
        }

        /* copy back to input array */
        for (int i = 0; i < DRINK_COMPLEXITY; i++) {
                bottles[i] = output[i];
        }
}

/*
 * insert_order()
 *
 * Give order an ID and insert it at the end of the buffer.
 *
 */

void insert_order(struct barorder *order) 
{
        int tail = (orders_head + orders_size) % NCUSTOMERS;
        order->cust_id = tail;
        order->order_fulfilled = false;

        orders[tail] = order;
        orders_size++;
}

/*
 * remove_order()
 *
 * Remove the first order in the buffer and return it.
 *
 */

struct barorder *remove_order(void)
{
        struct barorder *order = orders[orders_head];

        orders_head = (orders_head + 1) % NCUSTOMERS;
        orders_size--;

        return order;
}
