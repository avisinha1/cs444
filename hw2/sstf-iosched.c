#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/kernel.h>

//shortest seek time first (SSTF) data object.
struct sstf_data {
	struct list_head queue;
};

static void sstf_merged_requests(struct request_queue *q, struct request *rq,
				 struct request *next)
{
	list_del_init(&next->queuelist);
}

static int sstf_dispatch(struct request_queue *q, int force)
{
	struct sstf_data *sData = q->elevator->elevator_data;

	if (!list_empty(&sData->queue)) { //if list is not empty
		struct request *rq = list_entry(sData->queue.next, struct request, queuelist);
		printk(KERN_DEBUG "Dispatching Sector #: %llu\n",blk_rq_pos(rq));	//display which sector is being dispatched
		list_del_init(&rq->queuelist);
		elv_dispatch_add_tail(q, rq); //pass request to dispatch
		return 1;
	}
	
	return 0;
}

static void sstf_add_request(struct request_queue *q, struct request *rq)
{
	struct sstf_data *sData = q->elevator->elevator_data;
	struct list_head *cur_pos; //current sector position
	struct request *cur_node; //request sector position

	
	if (list_empty(&sData->queue)) { //if empty add anywhere in the queue
		printk(KERN_DEBUG "queue list empty...adding item to any position in the queue.\n");
		list_add(&rq->queuelist, &sData->queue); //add item to the queue 
	} else {

		list_for_each(cur_pos,&sData->queue) { //list request to iterate
			cur_node = list_entry(cur_pos, struct request, queuelist);

		
			if(blk_rq_pos(cur_node) < blk_rq_pos(rq)){ //if the request sector position is higher than current position
				printk(KERN_DEBUG "inserting item in front of current sector\n");
				list_add(&rq->queuelist, &cur_node->queuelist); //Add item to the list in front of current request position
				break;
			}
		}
	}
}

static struct request *
sstf_former_request(struct request_queue *q, struct request *rq)
{
	struct sstf_data *sData = q->elevator->elevator_data;

	if (rq->queuelist.prev == &sData->queue)
		return NULL;
	return list_entry(rq->queuelist.prev, struct request, queuelist);
}

static struct request *
sstf_latter_request(struct request_queue *q, struct request *rq)
{
	struct sstf_data *sData = q->elevator->elevator_data;

	if (rq->queuelist.next == &sData->queue)
		return NULL;
	return list_entry(rq->queuelist.next, struct request, queuelist);
}


static int sstf_init_queue(struct request_queue *q, struct elevator_type *e)
{
	struct sstf_data *sData;
	struct elevator_queue *eq;

	eq = elevator_alloc(q, e);
	if (!eq)
		return -ENOMEM;

	sData = kmalloc_node(sizeof(*sData), GFP_KERNEL, q->node);
	if (!sData) {
		kobject_put(&eq->kobj);
		return -ENOMEM;
	}
	eq->elevator_data = sData;

	INIT_LIST_HEAD(&sData->queue); //initialize linked list for queue

	spin_lock_irq(q->queue_lock);
	q->elevator = eq;
	spin_unlock_irq(q->queue_lock);

	printk(KERN_DEBUG "Initialized queue\n");
	return 0;
}

static void sstf_exit_queue(struct elevator_queue *e)
{
	struct sstf_data *sData = e->elevator_data;
	BUG_ON(!list_empty(&sData->queue));
	kfree(sData);
}

static struct elevator_type elevator_sstf = {
	.ops = {
		.elevator_merge_req_fn		= sstf_merged_requests,
		.elevator_dispatch_fn		= sstf_dispatch,
		.elevator_add_req_fn		= sstf_add_request,
		.elevator_former_req_fn		= sstf_former_request,
		.elevator_latter_req_fn		= sstf_latter_request,
		.elevator_init_fn		= sstf_init_queue,
		.elevator_exit_fn		= sstf_exit_queue,
	},
	.elevator_name = "sstf",
	.elevator_owner = THIS_MODULE,
};

static int __init sstf_init(void)
{
	return elv_register(&elevator_sstf);
}

static void __exit sstf_exit(void)
{
	elv_unregister(&elevator_sstf);
}

module_init(sstf_init);
module_exit(sstf_exit);


MODULE_AUTHOR("Omar Elgebaly, Aviral Sinha");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SSTF IO Scheduler");
