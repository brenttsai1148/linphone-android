/*
mediastreamer2 library - modular sound and video processing and streaming
Copyright (C) 2010  Simon MORLAT (simon.morlat@linphone.org)

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/


#include "mediastreamer2/mseventqueue.h"
#include "mediastreamer2/msfilter.h"

#ifndef MS_EVENT_BUF_SIZE
#define MS_EVENT_BUF_SIZE 8192
#endif

typedef enum {
	OnlySynchronous,
	OnlyAsynchronous,
	Both
}InvocationMode;

static void ms_filter_invoke_callbacks(MSFilter **f, unsigned int id, void *arg, InvocationMode synchronous_mode);

struct _MSNotifyContext{
	MSFilterNotifyFunc fn;
	void *ud;
	int synchronous;
};

typedef struct _MSNotifyContext MSNotifyContext;

struct _MSEventQueue{
	ms_mutex_t mutex; /*could be replaced by an atomic counter for freeroom*/
	uint8_t *rptr;
	uint8_t *wptr;
	uint8_t *endptr;
	uint8_t *lim;
	int freeroom;
	int size;
	MSFilter *current_notifier;
	uint8_t buffer[MS_EVENT_BUF_SIZE];
};

static void write_event(MSEventQueue *q, MSFilter *f, unsigned int ev_id, void *arg){
	int argsize=ev_id & 0xff;
	int size=argsize+16;
	uint8_t *nextpos=q->wptr+size;

	if (q->freeroom<size){
		ms_error("Dropped event, no more free space in event buffer !");
		return;
	}
	
	if (nextpos>q->lim){
		/* need to wrap around */
		q->endptr=q->wptr;
		q->wptr=q->buffer;
		nextpos=q->wptr+size;
	}
	*(long*)q->wptr=(long)f;
	*(long*)(q->wptr+8)=(long)ev_id;
	if (argsize>0) memcpy(q->wptr+16,arg,argsize);
	q->wptr=nextpos;
	ms_mutex_lock(&q->mutex);
	q->freeroom-=size;
	ms_mutex_unlock(&q->mutex);
}

static int parse_event(uint8_t *rptr,MSFilter **f, unsigned int *id, void **data, int *argsize){
	int evsize;
	*f=(MSFilter *)*(long*)(rptr);
	*id=(unsigned int)*(long*)(rptr+8);
	*argsize=(*id) & 0xff;
	evsize=(*argsize)+16;
	*data=rptr+16;
	return evsize;
}

static bool_t read_event(MSEventQueue *q){
	int available=q->size-q->freeroom;
	if (available>0){
		MSFilter *f;
		unsigned int id;
		void *data;
		int argsize;
		int evsize;
		
		evsize=parse_event(q->rptr,&f,&id,&data,&argsize);
		if (f) {
			q->current_notifier=f;
			ms_filter_invoke_callbacks(&q->current_notifier,id,argsize>0 ? data : NULL, OnlyAsynchronous);
			q->current_notifier=NULL;
		}
		q->rptr+=evsize;
		if (q->rptr>=q->endptr){
			q->rptr=q->buffer;
		}
		ms_mutex_lock(&q->mutex);
		q->freeroom+=evsize;
		ms_mutex_unlock(&q->mutex);
		return TRUE;
	}
	return FALSE;
}

/*clean all events belonging to a MSFilter that is about to be destroyed*/
void ms_event_queue_clean(MSEventQueue *q, MSFilter *destroyed){
	int freeroom=q->freeroom;
	uint8_t *rptr=q->rptr;
	
	while(q->size>freeroom){
		MSFilter *f;
		unsigned int id;
		void *data;
		int argsize;
		int evsize;
		
		evsize=parse_event(rptr,&f,&id,&data,&argsize);
		if (f==destroyed){
			ms_message("Cleaning pending event of MSFilter [%s:%p]",destroyed->desc->name,destroyed);
			*(long*)rptr=0;
		}
		rptr+=evsize;
		
		if (rptr>=q->endptr){
			rptr=q->buffer;
		}
		freeroom+=evsize;
	}
	if (q->current_notifier==destroyed){
		q->current_notifier=NULL;
	}
}

MSEventQueue *ms_event_queue_new(){
	MSEventQueue *q=ms_new0(MSEventQueue,1);
	int bufsize=MS_EVENT_BUF_SIZE;
	ms_mutex_init(&q->mutex,NULL);
	q->lim=q->buffer+bufsize;
	q->freeroom=bufsize;
	q->wptr=q->rptr=q->buffer;
	q->endptr=q->lim;
	q->size=bufsize;
	return q;
}

void ms_event_queue_destroy(MSEventQueue *q){
	ms_mutex_destroy(&q->mutex);
	ms_free(q);
}

static MSEventQueue *ms_global_event_queue=NULL;

void ms_set_global_event_queue(MSEventQueue *q){
	ms_global_event_queue=q;
}

void ms_event_queue_skip(MSEventQueue *q){
	int bufsize=q->size;
	q->lim=q->buffer+bufsize;
	q->freeroom=bufsize;
	q->wptr=q->rptr=q->buffer;
	q->endptr=q->lim;
}


void ms_event_queue_pump(MSEventQueue *q){
	while(read_event(q)){
	}
}

static MSNotifyContext * ms_notify_context_new(MSFilterNotifyFunc fn, void *ud, bool_t synchronous){
	MSNotifyContext *ctx=ms_new0(MSNotifyContext,1);
	ctx->fn=fn;
	ctx->ud=ud;
	ctx->synchronous=synchronous;
	return ctx;
}

static void ms_notify_context_destroy(MSNotifyContext *obj){
	ms_free(obj);
}

void ms_filter_add_notify_callback(MSFilter *f, MSFilterNotifyFunc fn, void *ud, bool_t synchronous){
	f->notify_callbacks=ms_list_append(f->notify_callbacks,ms_notify_context_new(fn,ud,synchronous));
}

void ms_filter_remove_notify_callback(MSFilter *f, MSFilterNotifyFunc fn, void *ud){
	MSList *elem;
	MSList *found=NULL;
	for(elem=f->notify_callbacks;elem!=NULL;elem=elem->next){
		MSNotifyContext *ctx=(MSNotifyContext*)elem->data;
		if (ctx->fn==fn && ctx->ud==ud){
			found=elem;
			break;
		}
	}
	if (found){
		ms_notify_context_destroy((MSNotifyContext*)found->data);
		f->notify_callbacks=ms_list_remove_link(f->notify_callbacks,found);
	}else ms_warning("ms_filter_remove_notify_callback(filter=%p): no registered callback with fn=%p and ud=%p",f,fn,ud);
}

void ms_filter_clear_notify_callback(MSFilter *f){
	f->notify_callbacks=ms_list_free_with_data(f->notify_callbacks,(void (*)(void*))ms_notify_context_destroy);
}

static void ms_filter_invoke_callbacks(MSFilter **f, unsigned int id, void *arg, InvocationMode synchronous_mode){
	MSList *elem;
	for (elem=(*f)->notify_callbacks;elem!=NULL && *f!=NULL;elem=elem->next){
		MSNotifyContext *ctx=(MSNotifyContext*)elem->data;
		if (synchronous_mode==Both || (synchronous_mode==OnlyAsynchronous && !ctx->synchronous)
			|| (synchronous_mode==OnlySynchronous && ctx->synchronous))
			ctx->fn(ctx->ud,*f,id,arg);
	}
}

void ms_filter_set_notify_callback(MSFilter *f, MSFilterNotifyFunc fn, void *ud){
	ms_filter_add_notify_callback(f,fn,ud,FALSE);
}


void ms_filter_notify(MSFilter *f, unsigned int id, void *arg){
	if (f->notify_callbacks!=NULL){
		if (ms_global_event_queue==NULL){
			/* synchronous notification */
			ms_filter_invoke_callbacks(&f,id,arg,Both);
		}else{
			ms_filter_invoke_callbacks(&f,id,arg,OnlySynchronous);
			write_event(ms_global_event_queue,f,id,arg);
		}
	}
}

void ms_filter_notify_no_arg(MSFilter *f, unsigned int id){
	ms_filter_notify(f,id,NULL);
}

void ms_filter_clean_pending_events(MSFilter *f){
	if (ms_global_event_queue)
		ms_event_queue_clean(ms_global_event_queue,f);
}

