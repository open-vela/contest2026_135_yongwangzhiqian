/****************************************************************************
 * app/testing/bk7258/bk7258_drivercheck.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Optional AP command transport for explicitly selected driver tests.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <sched.h>
#include <stdbool.h>
#include <stddef.h>
#include <syslog.h>

#include <nuttx/lib/builtin.h>
#include <nshlib/nshlib.h>

int bk7258_drivercheck_initialize(void)
{
  static bool started;
  FAR const struct builtin_s *app;
  FAR char *argv[] = { "-r", NULL };
  int index;
  pid_t pid;

  if (started)
    {
      return 0;
    }

  index = builtin_isavail("rexecd");
  if (index < 0 || (app = builtin_for_index(index)) == NULL)
    {
      return -ENOENT;
    }

  /* Use the existing AP startup and select RPMsg explicitly.  Do not start
   * another UART shell or a network-facing rexec server.
   */

  nsh_initialize();
  pid = task_create(app->name, app->priority, app->stacksize,
                    app->main, argv);
  if (pid < 0)
    {
      return errno > 0 ? -errno : -EIO;
    }

  started = true;
  syslog(LOG_INFO, "bk7258: drivercheck rexecd requested over RPMsg\n");
  return 0;
}
