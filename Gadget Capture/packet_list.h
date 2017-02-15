#include <linux/slab.h>
#include <linux/types.h>
#include "vendor_com.h"

struct pak_queue_state {
	u16 pak_count;
	u16 pak_length[PAK_QUEUE_LENGTH];
};

struct pak_node {					/*data不会小于15+3*/
	struct pak_node	*next, *prev;
	u16				length;
	void			*data;
};

/*struct pak_node {					//去掉头的长度1533+3,15+3
	struct pak_node	*next, *prev;
	u8					dir;
	u32					pak_num;
	struct timeval		pak_tv;
	u16					pak_length;
	char 				pak_data[1518];
};*/

#define PAK_LIST_HEAD_INIT(name) { &(name), &(name) }

#define PAK_LIST_HEAD(name) \
	struct pak_node name = PAK_LIST_HEAD_INIT(name)

static inline void PAK_INIT_LIST_HEAD(struct pak_node *head)
{
	head->next = head;
	head->prev = head;
	head->length = 0;
}

static inline void pak_list_add_tail(struct pak_node *new, struct pak_node *head)
{
	new->prev = head->prev;
	head->prev->next = new;
	head->prev = new;
	new->next = head;
	head->length++;
}

static inline void pak_list_del(struct pak_node *head)
{
	struct pak_node *caps_d = head->next;
	if(caps_d == head)		/*防止把头自己给释放掉了。。shit*/
		return;
	kfree(head->next->data);
	head->next->next->prev = head;
	head->next = head->next->next;
	kfree(caps_d);
	head->length--;
}

static inline int pak_list_is_empty(struct pak_node *head)
{
	return head->next == head;
}



static inline void pak_queue_state_add(struct pak_queue_state *queue_state, int length)
{
	//u16 tmp;
	queue_state->pak_length[queue_state->pak_count] = length;
	/*tmp = queue_state->pak_length;
	tmp++;*/
	queue_state->pak_count++;
}

static inline void pak_queue_state_dec(struct pak_queue_state *queue_state)
{
	/*u16 tmp;
	tmp = queue_state->pak_length;
	tmp--;*/
	queue_state->pak_count--;//= tmp;
	queue_state->pak_length[queue_state->pak_count] = 0;
}
