#define CAML_INTERNALS
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <inttypes.h>
#include "caml/alloc.h"
#include "caml/eventlog.h"
#include "caml/osdeps.h"

typedef enum { BEGIN, END, BEGIN_FLOW, END_FLOW, GLOBAL_SYNC, COUNTER } evtype;

uintnat caml_eventlog_enabled = 0;

struct event {
  const char* name;
  uint64_t timestamp;
  evtype ty;
  uint64_t value; /* for COUNTER */
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

#define FPRINTF_EV(out, pid, ev, ph, extra_fmt, ...) \
  fprintf(out, \
    "{\"ph\": \"%c\", " \
    "\"ts\": %"PRIu64".%03d, " \
    "\"name\": \"%s\", " \
    "\"pid\": %d" \
    extra_fmt \
    "},\n", \
    (ph), \
    ((ev).timestamp - startup_timestamp) / 1000, \
    (int)(((ev).timestamp - startup_timestamp) % 1000), \
    (ev).name, \
    pid, \
    ## __VA_ARGS__)

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

static void record_flush_time(FILE* out, uint64_t start)
{
  uint64_t ts = caml_time_counter();

  fprintf(out, \
    "{\"ph\": \"X\", " \
    "\"ts\": %"PRIu64".%03d, " \
    "\"dur\": %"PRIu64".%03d, " \
    "\"name\": \"eventlog/flush\", " \
    "\"pid\": %d" \
    "},\n", \
    (start - startup_timestamp) / 1000, \
    (int)((start - startup_timestamp) % 1000), \
    (uint64_t)(ts - start) / 1000,
    (int)((ts - start) % 1000), \
    getpid()
  );

}

static void flush_events(FILE* out, struct event_buffer* eb)
{
  uintnat i;
  uintnat n = eb->ev_generated;
  uintnat start_time = caml_time_counter();
  pid_t pid  = getpid();

  for (i = 0; i < n; i++) {
    struct event ev = eb->events[i];
    switch (ev.ty) {
    case BEGIN:
    case END:
      FPRINTF_EV(out, pid, ev,
                 ev.ty == BEGIN ? 'B' : 'E', "");
      break;
    case BEGIN_FLOW:
    case END_FLOW:
      FPRINTF_EV(out, pid, ev,
                 ev.ty == BEGIN_FLOW ? 's' : 'f',
                 ", \"bp\": \"e\", \"id\": \"0x%08"PRIu64"\"",
                 ev.value);
      break;
    case GLOBAL_SYNC:
      FPRINTF_EV(out, pid, ev, 'i', ", \"cat\": \"gpu\"");
      break;
    case COUNTER:
      FPRINTF_EV(out, pid, ev, 'C',
                 ", \"args\": {\"value\": %"PRIu64"}",
                 ev.value);
      break;
    }
  }
  record_flush_time(out, start_time);
}

static void teardown_eventlog()
{
  struct evbuf_list_node* b;
  int count = 0;
  for (b = evbuf_head.next; b != &evbuf_head; b = b->next) {
    flush_events(output, (struct event_buffer*)b);
    count++;
  } 
  fprintf(output,
          "{\"name\": \"exit\", "
          "\"ph\": \"i\", "
          "\"ts\": %"PRIu64", "
          "\"pid\": %d, "
          "\"s\": \"g\"}\n"
          "]\n}\n",
          (caml_time_counter() - startup_timestamp) / 1000,
          getpid()
  );
  fclose(output);
}

void caml_setup_eventlog()
{
  char filename[64];
  sprintf(filename, "eventlog.%d.json", getpid());
  if (!caml_eventlog_enabled) return;
  output = fopen(filename, "w");
  if (output) {
    char* fullname = realpath(filename, 0);
    
    free(fullname);
    fprintf(output,
            "{\n"
            "\"displayTimeUnit\": \"ns\",\n"
            "\"traceEvents\": [\n");
    startup_timestamp = caml_time_counter();
  } else {
    fprintf(stderr, "Could not begin logging events to %s\n", filename);
    _exit(128);
  }
  atexit(&teardown_eventlog);
}

static void post_event(const char* name, evtype ty, uint64_t value)
{
  uintnat i;
  struct event* ev;
  if (!caml_eventlog_enabled) return;
  if (!evbuf) setup_evbuf();
  i = evbuf->ev_generated;
  CAMLassert(i <= EVENT_BUF_SIZE);
  if (i == EVENT_BUF_SIZE) {
    flush_events(output, evbuf);
    evbuf->ev_generated = 0;
    i = 0;
  }
  ev = &evbuf->events[i];
  ev->name = name;
  ev->ty = ty;
  ev->value = value;
  ev->timestamp = caml_time_counter();
  evbuf->ev_generated = i + 1;
}

void caml_ev_begin(const char* name)
{
  post_event(name, BEGIN, 0);
}

void caml_ev_end(const char* name)
{
  post_event(name, END, 0);
}

void caml_ev_begin_flow(const char* name, uintnat ev)
{
  post_event(name, BEGIN_FLOW, ev);
}

void caml_ev_end_flow(const char* name, uintnat ev)
{
  post_event(name, END_FLOW, ev);
}

void caml_ev_global_sync()
{
  post_event("vblank", GLOBAL_SYNC, 0);
}

void caml_ev_counter(const char* name, uint64_t val)
{
  post_event(name, COUNTER, val);
}

void caml_ev_pause(long reason){}
void caml_ev_resume(){}
void caml_ev_msg(char* msg, ...){}
