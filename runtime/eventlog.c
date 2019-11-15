#define CAML_INTERNALS
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include "caml/alloc.h"
#include "caml/eventlog.h"
#include "caml/misc.h"
#include "caml/osdeps.h"

#ifdef HAS_UNISTD
#include <unistd.h>
#elif _WIN32
#include <process.h>
#endif

#define CTF_MAGIC 0xc1fc1fc1
#define CAML_TRACE_VERSION 0x1

struct ctf_stream_header {
  uint32_t magic;
  uint16_t caml_trace_version;
  uint16_t stream_id;
};

static struct ctf_stream_header header = {
  CTF_MAGIC,
  CAML_TRACE_VERSION,
  0
};

#pragma pack(1)
struct ctf_event_header {
  uint64_t timestamp;
  uint32_t pid;
  uint32_t id;
};

struct event {
  struct ctf_event_header header;
  uint8_t  phase; /* for GC events */
  uint8_t  counter_kind; /* misc counter name */
  uint8_t  alloc_bucket; /* for alloc counters */
  uint32_t count; /* for misc counters */
};

static FILE* output;
static uintnat eventlog_startup_timestamp = 0;
static uint32_t eventlog_startup_pid = 0;

uintnat caml_eventlog_paused = 0;
uintnat caml_eventlog_enabled = 0;

#define EVENT_BUF_SIZE 4096
struct event_buffer {
  uintnat ev_generated;
  struct event events[EVENT_BUF_SIZE];
};

static struct event_buffer* evbuf;

void setup_evbuf()
{
  CAMLassert(!evbuf);
  evbuf = caml_stat_alloc_noexc(sizeof(*evbuf));

  if (evbuf == NULL)
    caml_fatal_error("eventlog: could not allocate event buffer");

  evbuf->ev_generated = 0;
}

void setup_eventlog_file()
{
  char_os *filename = NULL;
  char_os *eventlog_filename;
  value tmp;

  eventlog_filename = caml_secure_getenv(T("OCAML_EVENTLOG_FILE"));
  if (eventlog_filename) {
    char *input =  caml_stat_strdup_of_os(eventlog_filename);

    if (input != NULL) {
      tmp = caml_alloc_sprintf("%s.%d.eventlog",
                               input, eventlog_startup_pid);
      caml_stat_free(input);
      filename = caml_stat_strdup_to_os(String_val(tmp));
    }

  } else {
    tmp = caml_alloc_sprintf("caml-eventlog-%d", eventlog_startup_pid);
    filename = caml_stat_strdup_to_os(String_val(tmp));
  }

  if (filename) {
    output = fopen_os(filename, T("wb"));
    caml_stat_free(filename);
  }

  if (output) {
    fwrite(&header, sizeof(struct ctf_stream_header), 1, output);
    fflush(output);
  } else {
    caml_eventlog_enabled = 0;
  }
}

static void flush_events(FILE* out, struct event_buffer* eb)
{
  uintnat i;
  uintnat n = eb->ev_generated;

  struct ctf_event_header ev_flush;
  ev_flush.id = EV_FLUSH;
  ev_flush.timestamp = caml_time_counter() - eventlog_startup_timestamp;
  ev_flush.pid = eventlog_startup_pid;

  for (i = 0; i < n; i++) {
    struct event ev = eb->events[i];
    ev.header.pid = eventlog_startup_pid;

    fwrite(&ev.header, sizeof(struct ctf_event_header), 1, out);

    switch (ev.header.id)
    {
    case EV_ENTRY:
      fwrite(&ev.phase, sizeof(uint8_t), 1, out);
      break;
    case EV_EXIT:
      fwrite(&ev.phase, sizeof(uint8_t), 1, out);
      break;
    case EV_COUNTER:
      fwrite(&ev.count, sizeof(uint32_t), 1, out);
      fwrite(&ev.counter_kind, sizeof(uint8_t), 1, out);
      break;
    case EV_ALLOC:
      fwrite(&ev.count, sizeof(uint32_t), 1, out);
      fwrite(&ev.alloc_bucket, sizeof(uint8_t), 1, out);
      break;
    default:
      break;
    }
  }

  uint64_t flush_duration =
    (caml_time_counter() - eventlog_startup_timestamp) - ev_flush.timestamp;

  fwrite(&ev_flush, sizeof(struct ctf_event_header), 1, out);
  fwrite(&flush_duration, sizeof(uint64_t), 1, out);
}

static void teardown_eventlog()
{
  if (evbuf) {
    flush_events(output, evbuf);
    caml_stat_free(evbuf);
    evbuf = 0;
  }
  if (output) {
    fclose(output);
    output = 0;
  }
}

void caml_setup_eventlog()
{
  char_os *toggle = caml_secure_getenv(T("OCAML_EVENTLOG_ENABLED"));

  if (toggle != NULL) {
    caml_eventlog_enabled = 1;
    if (*toggle == 'p')
      caml_eventlog_paused = 1;
  };

  if (!caml_eventlog_enabled) return;

  eventlog_startup_timestamp = caml_time_counter();
#ifdef _WIN32
  eventlog_startup_pid = _getpid();
#else
  eventlog_startup_pid = getpid();
#endif
  setup_eventlog_file();

  atexit(&teardown_eventlog);
}

static void post_event(ev_gc_phase phase, ev_gc_counter counter_kind,
                       uint8_t bucket, uint32_t count, ev_type ty)
{
  uintnat i;
  struct event* ev;

  if (!caml_eventlog_enabled) return;
  if (caml_eventlog_paused) return;
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
  ev->header.timestamp = caml_time_counter() - eventlog_startup_timestamp;
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

static uintnat alloc_buckets [20] =
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

/* This function records allocations in nf_allocate in given bucket sizes.
   These buckets are meant to be flushed explicitly by the caller through the
   caml_ev_alloc_flush function. Until then the buckets are just updated until
   flushed.
*/
void caml_ev_alloc(uintnat sz)
{
  if (!caml_eventlog_enabled) return;
  if (caml_eventlog_paused) return;

  if (sz < 10) {
    ++alloc_buckets[sz];
  } else if (sz < 100) {
    ++alloc_buckets[sz/10 + 9];
  } else {
    ++alloc_buckets[19];
  }
}

/*  Note that this function does not trigger an actual disk flush, it just
    pushes events in the event buffer.
*/
void caml_ev_alloc_flush()
{
  int i;

  if (!caml_eventlog_enabled) return;
  if (caml_eventlog_paused) return;

  for (i = 1; i < 20; i++) {
    if (alloc_buckets[i] != 0) {
     post_event(0, 0, i, alloc_buckets[i], EV_ALLOC);
    };
    alloc_buckets[i] = 0;
  }
}

void caml_ev_flush()
{
  fflush(output);
}

void caml_ev_disable()
{
  teardown_eventlog();
}


CAMLprim value caml_ev_resume(value v)
{
  CAMLassert(v == Val_unit);
  if (caml_eventlog_enabled)
    caml_eventlog_paused = 0;
  return Val_unit;
}

CAMLprim value caml_ev_pause(value v)
{
  CAMLassert(v == Val_unit);
  if (caml_eventlog_enabled) {
    caml_eventlog_paused = 1;
    if (evbuf)
     flush_events(output, evbuf);
  };
  return Val_unit;
}
