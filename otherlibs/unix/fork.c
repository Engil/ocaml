/**************************************************************************/
/*                                                                        */
/*                                 OCaml                                  */
/*                                                                        */
/*             Xavier Leroy, projet Cristal, INRIA Rocquencourt           */
/*                                                                        */
/*   Copyright 1996 Institut National de Recherche en Informatique et     */
/*     en Automatique.                                                    */
/*                                                                        */
/*   All rights reserved.  This file is distributed under the terms of    */
/*   the GNU Lesser General Public License version 2.1, with the          */
/*   special exception on linking described in the file LICENSE.          */
/*                                                                        */
/**************************************************************************/

#define CAML_INTERNALS

#include <caml/mlvalues.h>
#include <caml/debugger.h>
#include <caml/eventlog.h>
#include "unixsupport.h"

CAMLprim value unix_fork(value unit)
{
  int ret;

  if (caml_eventlog_status == EVENTLOG_ENABLED
      || caml_eventlog_status == EVENTLOG_PAUSED)
    pre_fork_eventlog();

  ret = fork();
  if (ret == -1) uerror("fork", Nothing);

  if ((caml_eventlog_status == EVENTLOG_ENABLED) ||
      (caml_eventlog_status == EVENTLOG_PAUSED)) {
    if (ret == 0)
      post_fork_eventlog();
  };

  if (caml_debugger_in_use)
    if ((caml_debugger_fork_mode && ret == 0) ||
        (!caml_debugger_fork_mode && ret != 0))
      caml_debugger_cleanup_fork();

  return Val_int(ret);
}
