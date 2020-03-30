(* TEST
  * has_eventlog_runtime
  ** native
    flags = "-runtime-variant=e"
*)

(* Test if the eventlog runtime is in working condition *)

let _ =
  Gc.eventlog_pause ();
  Gc.eventlog_resume();
  print_endline "OK"
