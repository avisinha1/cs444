/**********************************************
Name: Omar Elgebaly, Aviral Sinha
Class: CS444 - Operating Systems II
Assignment: Concurrency Exercise I
**********************************************/


/*********************References*************************
https://codereview.stackexchange.com/questions/147656/checking-if-cpu-supports-rdrand
http://www.cs.cmu.edu/afs/cs/academic/class/15492-f07/www/pthreads.html
https://linux.die.net/man/3/pthread_cond_signal
http://www.math.sci.hiroshima-u.ac.jp/~m-mat/MT/MT2002/CODES/mt19937ar.c
https://linux.die.net/man/3/pthread_mutex_trylock
********************************************************/

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "mt19937ar.c"

int x86;
struct data_buffer Container;

typedef struct
{
	short wait_timer;
	short number;
} Data;


struct data_buffer {
	short producer; //index for producer
	short consumer; //index for consumer
	Data items[32];
	pthread_cond_t producer_condition; //signal item is needs another 
	pthread_cond_t consumer_condition; //signal item is ready to be consumed
	pthread_mutex_t lock;
	
};

int check_system(){
	unsigned int eax = 0x1;
	unsigned int ebx, ecx, edx;
	  __asm__ __volatile__("cpuid;"
	                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
	                     : "a"(eax)
	                     );
						 
	if (ecx & 0x40000000) 
		x86 = 1; //32 bit
	else
		x86 = 0; //64 bit
	
	return x86;
}

void print_item(Data *item){
	
	printf("\t Item Number: %d\n", item->number);
	printf("\t Item Wait Time: %d\n\n", item->wait_timer);
	
}

int gen_rand(int min, int max)
{
    int rand_num = 0;

    if(x86 == 0) //if system is 64 bit
    {
        //merlene twister if 64 bit
        rand_num =  (int)genrand_int32();
    }
    else
    {
        //if x86/32 bit fill with random value using rdrand
        __asm__ __volatile__("rdrand %0":"=r"(rand_num));
    }

    rand_num = abs(rand_num % (max - min));
    if(rand_num < min)
    {
        return min;
    }

    return rand_num;
}


void *producer_function(void *foo)
{
    while(1)
    {
        
        pthread_mutex_lock(&Container.lock);
        
        Data new;
        
        new.number = gen_rand(1, 100); //item number randomly generated
        new.wait_timer = gen_rand(2,9); //wait timer randomly generated
        
        //print item details.
        printf("Producer generating item:\n");
        print_item(&new);

        
        if(Container.producer == 32) //if producer position in data buffer is at max 32 bits
        {
            printf("AT MAX size\n");
           
            pthread_cond_signal(&(Container.consumer_condition));  //Signal consumer to begin processing data
            pthread_cond_wait(&(Container.producer_condition), &Container.lock); //
        } 

        
        Container.items[Container.producer] = new; //add item to buffer
        Container.producer++;

        //signal cnnsumer thread
        pthread_cond_signal(&(Container.consumer_condition));
        pthread_cond_wait(&(Container.producer_condition), &Container.lock);
        
        if(Container.producer >= 32) //if greater than or equal to 32 bits, resize
        {
            Container.producer = 0;
        }
        //unlock data.
        pthread_mutex_unlock(&Container.lock);
    }
}


void *consumer_function(void *var)
{
	while(1){
		pthread_mutex_lock(&Container.lock); //lock to consumer thread
		Data consume_item;
		if(Container.consumer >= 32) //if consumer is at max size
        {
            Container.consumer = 0; //resize consumer buffer
        }

        
        pthread_cond_signal(&(Container.producer_condition)); //signal producer thread, that consumer is ready.
        
        pthread_cond_wait(&(Container.consumer_condition), &Container.lock); //wait for producer to create a thread.
        
        
        if(Container.producer == 0) //if data in producer buffer is empty
        {
            pthread_cond_wait(&(Container.consumer_condition), &Container.lock); //wait for producer to create item
        }
        
        consume_item = Container.items[Container.consumer];
        printf("Consumer consuming item: %d\n\n", consume_item.number); //start consuming item
        sleep(consume_item.wait_timer);
        printf("Item %d Consumed Successfully\n\n", consume_item.number); //consumed item after consume wait timer finishes
        
        
        Container.consumer++; //increment consumer position in the buffer.
        
        pthread_mutex_unlock(&Container.lock); // unlock data container
    }
}




int main(int argc, char* argv[])
{
    int thread_count, i;

    if(argc <= 1)
        thread_count = 1;
    else
        thread_count = atoi(argv[1]);
    

    check_system();

    Container.consumer = 0;
    Container.producer = 0;

    //producer and consumer threads
    pthread_t threads[2 * thread_count];
    
	//create threads
    for(i = 0; i < thread_count; i++){
        pthread_create(&threads[i], NULL, consumer_function, NULL);
        pthread_create(&threads[i+1], NULL, producer_function, NULL);
    }

    //join threads
    for(i = 0; i < (2 * thread_count); i++)
    {
        pthread_join(threads[i], NULL);
    }

    return 0;
}
