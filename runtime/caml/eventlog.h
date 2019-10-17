#ifndef CAML_EVENTLOG_H
#define CAML_EVENTLOG_H

typedef enum {
    EV_ENTRY,
    EV_EXIT,
    EV_COUNTER,
    EV_ALLOC,
    EV_FLUSH
} ev_type;

typedef enum {
    EV_COMPACT_MAIN,
    EV_COMPACT_RECOMPACT,
    EV_EXPLICIT_GC_SET,
    EV_EXPLICIT_GC_MINOR,
    EV_EXPLICIT_GC_MAJOR,
    EV_EXPLICIT_GC_FULL_MAJOR,
    EV_EXPLICIT_GC_COMPACT,
    EV_MAJOR,
    EV_MAJOR_ROOTS,
    EV_MAJOR_SWEEP,
    EV_MAJOR_MARK_ROOTS, /* FIXME: these NEEDS to be 10, 11 and 12 see major_gc.h caml_gc_subphase defines */
    EV_MAJOR_MARK_MAIN,
    EV_MAJOR_MARK_FINAL, /* FIXME end */
    EV_MAJOR_ROOTS_GLOBAL,
    EV_MAJOR_ROOTS_LOCAL,
    EV_MAJOR_ROOTS_C,
    EV_MAJOR_ROOTS_FINALISED,
    EV_MAJOR_ROOTS_MEMPROF,
    EV_MAJOR_ROOTS_HOOK,
    EV_MAJOR_CHECK_AND_COMPACT,
    EV_MINOR,
    EV_MINOR_LOCAL_ROOTS,
    EV_MINOR_REF_TABLES,
    EV_MINOR_COPY,
    EV_MINOR_UPDATE_WEAK,
    EV_MINOR_FINALIZED,
    EV_EXPLICIT_GC_MAJOR_SLICE
} ev_gc_phase;

typedef enum {
    EV_C_ALLOC_JUMP,
    EV_C_FORCE_MINOR_ALLOC_SMALL,
    EV_C_FORCE_MINOR_MAKE_VECT,
    EV_C_FORCE_MINOR_SET_MINOR_HEAP_SIZE,
    EV_C_FORCE_MINOR_WEAK,
    EV_C_MAJOR_MARK_SLICE_REMAIN,
    EV_C_MAJOR_MARK_SLICE_FIELDS,
    EV_C_MAJOR_MARK_SLICE_POINTERS,
    EV_C_MAJOR_WORK_EXTRA,
    EV_C_MAJOR_WORK_MARK,
    EV_C_MAJOR_WORK_SWEEP,
    EV_C_MINOR_PROMOTED,
    EV_C_REQUEST_MAJOR_ALLOC_SHR,
    EV_C_REQUEST_MAJOR_ADJUST_GC_SPEED,
    EV_C_REQUEST_MINOR_REALLOC_REF_TABLE,
    EV_C_REQUEST_MINOR_REALLOC_EPHE_REF_TABLE,
    EV_C_REQUEST_MINOR_REALLOC_CUSTOM_TABLE

} ev_gc_counter;

uintnat caml_eventlog_enabled;
void caml_setup_eventlog(void);
void caml_ev_begin(ev_gc_phase phase);
void caml_ev_end(ev_gc_phase phase);
void caml_ev_counter(ev_gc_counter counter, uint32_t val);
void caml_ev_alloc(uintnat size);
void caml_ev_alloc_fold(void);

#endif
