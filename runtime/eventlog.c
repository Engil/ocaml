#define CAML_INTERNALS
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <inttypes.h>
#include "caml/alloc.h"
#include "caml/eventlog.h"
#include "caml/osdeps.h"

#define CTF_MAGIC 0xc1fc1fc1

struct ctf_stream_header {
  uint32_t magic;
  uint32_t stream_id;
};

static struct ctf_stream_header header = {
  0xc1fc1fc1,
  0
}; 

#pragma pack(1)
struct ctf_event_header {
  uint32_t id;
  uint64_t timestamp;
};

static uintnat alloc_buckets [20] = 
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

typedef enum {
    EVENTLOG_DISABLED,
    EVENTLOG_ENABLED,
    EVENTLOG_PAUSED
} eventlog_state;

eventlog_state caml_eventlog_status = EVENTLOG_DISABLED;

struct event {
  struct ctf_event_header header;
  uint8_t  phase; /* for GC events */
  uint8_t  counter_kind;
  uint8_t  alloc_bucket; /* for alloc counters */
  uint32_t count; /* for misc counters */
};

struct event_buffer;
struct evbuf_list_node {
  struct evbuf_list_node* next;
  struct evbuf_list_node* prev;
};

static struct evbuf_list_node evbuf_head =
  { &evbuf_head, &evbuf_head };
static FILE* output;
static uint64_t startup_timestamp = 0;

#define EVENT_BUF_SIZE 4096
struct event_buffer {
  struct evbuf_list_node list;

  uintnat ev_generated;

  struct event events[EVENT_BUF_SIZE];
};

static struct event_buffer* evbuf;

void setup_evbuf()
{
  CAMLassert(!evbuf);
  evbuf = malloc(sizeof(*evbuf));
  if (!evbuf) return;

  evbuf->ev_generated = 0;

  evbuf->list.next = evbuf_head.next;
  evbuf_head.next = &evbuf->list;
  evbuf->list.prev = &evbuf_head;
  evbuf->list.next->prev = &evbuf->list;

}

static void flush_events(FILE* out, struct event_buffer* eb)
{
  uintnat i;
  uintnat n = eb->ev_generated;

  struct ctf_event_header ev_flush;
  ev_flush.id = EV_FLUSH;
  ev_flush.timestamp = caml_time_counter() - startup_timestamp;

  for (i = 0; i < n; i++) {
    struct event ev = eb->events[i];
    fwrite(&ev.header, sizeof(struct ctf_event_header), 1, out);
    switch (ev.header.id)
    {
    case 0:
      fwrite(&ev.phase, sizeof(uint8_t), 1, out);
      break;
    case 1:
      fwrite(&ev.phase, sizeof(uint8_t), 1, out);
      break;
    case 2:
      fwrite(&ev.counter_kind, sizeof(uint8_t), 1, out);
      fwrite(&ev.count, sizeof(uint32_t), 1, out);
      break;
    case 3:
      fwrite(&ev.alloc_bucket, sizeof(uint8_t), 1, out);
      fwrite(&ev.count, sizeof(uint32_t), 1, out);
      break;
    default:
      break;
    }
  }

  uint64_t flush_end = caml_time_counter() - startup_timestamp;

  fwrite(&ev_flush, sizeof(struct ctf_event_header), 1, out);
  fwrite(&flush_end, sizeof(uint64_t), 1, out);
}

static void teardown_eventlog()
{
  struct evbuf_list_node* b;
  int count = 0;
  for (b = evbuf_head.next; b != &evbuf_head; b = b->next) {
    flush_events(output, (struct event_buffer*)b);
    count++;
  } 
  fclose(output);
}

void caml_setup_eventlog()
{
  char *filename;
  char *ocaml_eventlog_filename;

  if (caml_secure_getenv("OCAML_EVENTLOG_ENABLED"))
    caml_eventlog_status = EVENTLOG_ENABLED;
  if (caml_eventlog_status == EVENTLOG_DISABLED) return;

  ocaml_eventlog_filename = caml_secure_getenv("OCAML_EVENTLOG_FILE");
  if (ocaml_eventlog_filename) {
    filename = ocaml_eventlog_filename;
  } else {
    filename = malloc(64);
    sprintf(filename, "traces/stream-%d", getpid());
  }

  output = fopen(filename, "w");
  if (output) {
    fwrite(&header, sizeof(struct ctf_stream_header), 1, output);
    startup_timestamp = caml_time_counter();
  } else {
    fprintf(stderr, "Could not begin logging events to %s\n", filename);
    if (!ocaml_eventlog_filename)
      free(filename);
    _exit(128);
  }
  atexit(&teardown_eventlog);
}

static void post_event(ev_gc_phase phase, ev_gc_counter counter_kind, uint8_t bucket, uint32_t count, ev_type ty)
{
  uintnat i;
  struct event* ev;
  if (caml_eventlog_status != EVENTLOG_ENABLED) return;
  if (!evbuf) setup_evbuf();
  i = evbuf->ev_generated;
  CAMLassert(i <= EVENT_BUF_SIZE);
  if (i == EVENT_BUF_SIZE) {
    flush_events(output, evbuf);
    evbuf->ev_generated = 0;
    i = 0;
  }
  ev = &evbuf->events[i];
  ev->header.id = ty;
  ev->count = count;
  ev->counter_kind = counter_kind;
  ev->alloc_bucket = bucket;
  ev->phase = phase;
  ev->header.timestamp = caml_time_counter() - startup_timestamp;
  evbuf->ev_generated = i + 1;
}

void caml_ev_begin(ev_gc_phase phase)
{
  post_event(phase, 0, 0, 0, EV_ENTRY);
}

void caml_ev_end(ev_gc_phase phase)
{
  post_event(phase, 0, 0, 0, EV_EXIT);
}

void caml_ev_counter(ev_gc_counter counter, uint32_t val)
{
  post_event(0, counter, 0, val, EV_COUNTER);
}

void caml_ev_alloc(uintnat sz)
{
  if (sz < 10) {
    ++alloc_buckets[sz];
  } else if (sz < 100) {
    ++alloc_buckets[sz/10 + 9];
  } else {
    ++alloc_buckets[19];
  }
}

void caml_ev_alloc_fold()
{
  int i;
  for (i = 1; i < 20; i++) {
    if (alloc_buckets[i] != 0) {
     post_event(0, 0, i, alloc_buckets[i], EV_ALLOC);
    };
     alloc_buckets[i] = 0;
  }
}

CAMLprim value caml_ev_resume(value v)
{
  CAMLassert(v == Val_unit);
  if (caml_eventlog_status == EVENTLOG_PAUSED)
    caml_eventlog_status = EVENTLOG_ENABLED;
  return Val_unit;
}

CAMLprim value caml_ev_pause(value v)
{
  CAMLassert(v == Val_unit);
  if (caml_eventlog_status == EVENTLOG_ENABLED) {
    caml_eventlog_status = EVENTLOG_PAUSED;
    if (evbuf)
     flush_events(output, evbuf);
  };
  return Val_unit;
}
