/* This file will contain your solution. Modify it as you wish. */
#include <types.h>
#include <synch.h>
#include <lib.h>
#include "producerconsumer_driver.h"

/* Declare any variables you need here to keep track of and
   synchronise your bounded. A sample declaration of a buffer is shown
   below. You can change this if you choose another implementation. */

static struct pc_data buffer[BUFFER_SIZE];
int buffer_len;
int head;

struct lock *pc_lock;
struct cv *pc_full, *pc_empty;

/* Helper functions */
void insert_item(struct pc_data item);
struct pc_data remove_item(void);

/* consumer_receive() is called by a consumer to request more data. It
   should block on a sync primitive if no data is available in your
   buffer. */

struct pc_data consumer_receive(void)
{
        struct pc_data thedata;

        lock_acquire(pc_lock);
        
        // wait until there is an item in the buffer
        while (buffer_len == 0) {
                cv_wait(pc_empty, pc_lock);
        }

        // remove the item from the buffer
        thedata = remove_item();

        // signal to other threads there is space in the buffer
        if (buffer_len == BUFFER_SIZE-1) {
                cv_broadcast(pc_full, pc_lock);
        }

        lock_release(pc_lock);

        return thedata;
}

/* procucer_send() is called by a producer to store data in your
   bounded buffer. */

void producer_send(struct pc_data item)
{
        lock_acquire(pc_lock);

        // wait until there is space in the buffer
        while (buffer_len == BUFFER_SIZE) {
                cv_wait(pc_full, pc_lock);
        }

        // add item to buffer
        insert_item(item);

        // signal to other threads there are items waiting in the buffer
        if (buffer_len == 1) {
                cv_broadcast(pc_empty, pc_lock);
        }

        lock_release(pc_lock);
}




/* Perform any initialisation (e.g. of global data) you need
   here. Note: You can panic if any allocation fails during setup */

void producerconsumer_startup(void)
{
        pc_lock = lock_create("pc_lock");
        if (pc_lock == NULL) {
                panic("producerconsumer: lock failed to create");
        }

        pc_full = cv_create("pc_full");
        if (pc_full == NULL) {
                panic("producerconsumer: cv failed to create");
        }

        pc_empty = cv_create("pc_empty");
        if (pc_empty == NULL) {
                panic("producerconsumer: cv failed to create");
        }

        buffer_len = 0;
	head = 0;
}

/* Perform any clean-up you need here */
void producerconsumer_shutdown(void)
{
        lock_destroy(pc_lock);
        cv_destroy(pc_full);
        cv_destroy(pc_empty);
}

/* Insert item at end of valid data section in circular array */
void insert_item(struct pc_data item)
{
        // Find end of list, wrapping around if needed
        int tail = (head + buffer_len) % BUFFER_SIZE;

        // Place new item at tail position
        buffer[tail] = item;
        buffer_len++;
}

/* Remove item from circular array, FIFO */
struct pc_data remove_item(void)
{
        struct pc_data item = buffer[head];
        buffer_len--;

        head = (head + 1) % BUFFER_SIZE;

        return item;
}
