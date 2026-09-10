/****************************************************************************
 * app/dolphin/dolphin_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <spawn.h>
#include <sys/wait.h>

#include <nuttx/lib/builtin.h>

struct dolphin_command_s
{
  const char *directory_name;
  const char *builtin_name;
  char *const *argv;
};

#ifdef CONFIG_BK7258_APP_NFC
static char *const g_nfc_argv[] = { "bknfc", "scan", NULL };
#endif
#ifdef CONFIG_BK7258_APP_MOTION
static char *const g_motion_argv[] = { "bkmotion", "sample", NULL };
#endif
#ifdef CONFIG_BK7258_APP_HEALTH
static char *const g_health_argv[] = { "bkhealth", "status", NULL };
#endif
static char *const g_wifi_argv[] = { "bkwifi", "status", NULL };

static const struct dolphin_command_s g_commands[] =
{
#ifdef CONFIG_BK7258_APP_NFC
  { "nfc",    "bknfc",    g_nfc_argv },
#endif
#ifdef CONFIG_BK7258_APP_MOTION
  { "motion", "bkmotion", g_motion_argv },
#endif
#ifdef CONFIG_BK7258_APP_HEALTH
  { "health", "bkhealth", g_health_argv },
#endif
  { "wifi",   "bkwifi",   g_wifi_argv },
};

static const struct dolphin_command_s *dolphin_find(const char *name)
{
  size_t index;

  for (index = 0; index < sizeof(g_commands) / sizeof(g_commands[0]); index++)
    {
      if (strcmp(name, g_commands[index].directory_name) == 0)
        {
          return &g_commands[index];
        }
    }

  return NULL;
}

static void dolphin_list(void)
{
  size_t index;

  puts("dolphin directory:");
  for (index = 0; index < sizeof(g_commands) / sizeof(g_commands[0]); index++)
    {
      const struct dolphin_command_s *command = &g_commands[index];
      bool compiled = builtin_isavail(command->builtin_name) >= 0;

      printf("  %-6s %s\n", command->directory_name,
             compiled ? "compiled" : "unavailable");
    }
}

static int dolphin_run(const char *name)
{
  const struct dolphin_command_s *command = dolphin_find(name);
  FAR const struct builtin_s *builtin;
  posix_spawnattr_t attr;
  pid_t pid;
  int index;
  int status;
  int result;

  if (command == NULL)
    {
      fprintf(stderr, "dolphin: unsupported command: %s\n", name);
      return 1;
    }

  index = builtin_isavail(command->builtin_name);
  if (index < 0 || (builtin = builtin_for_index(index)) == NULL)
    {
      fprintf(stderr, "dolphin: %s is unavailable\n", name);
      return 1;
    }

  result = posix_spawnattr_init(&attr);
  if (result == 0)
    {
      result = posix_spawnattr_setpriority(&attr, builtin->priority);
    }

  if (result == 0)
    {
      result = posix_spawnattr_setstacksize(&attr, builtin->stacksize);
    }

  if (result != 0)
    {
      fprintf(stderr, "dolphin: cannot prepare %s: %d\n", name, result);
      return 1;
    }

  /* task_spawn supplies builtin->name as argv[0], as does the existing
   * builtin launcher.  Pass only the fixed command arguments here.
   */

  pid = task_spawn(builtin->name, builtin->main, NULL, &attr,
                   &command->argv[1], NULL);
  (void)posix_spawnattr_destroy(&attr);
  if (pid < 0)
    {
      errno = -pid;
      fprintf(stderr, "dolphin: cannot start %s: %d\n", name, errno);
      return 1;
    }

  do
    {
      result = waitpid(pid, &status, 0);
    }
  while (result < 0 && errno == EINTR);

  if (result < 0)
    {
      fprintf(stderr, "dolphin: wait for %s failed: %d\n", name, errno);
      return 1;
    }

  if (WIFEXITED(status))
    {
      return WEXITSTATUS(status);
    }

  if (WIFSIGNALED(status))
    {
      return 128 + WTERMSIG(status);
    }

  return 1;
}

#ifndef DOLPHIN_MAIN
#  define DOLPHIN_MAIN main
#endif

int DOLPHIN_MAIN(int argc, char *argv[])
{
  if (argc == 1 || (argc == 2 && strcmp(argv[1], "list") == 0))
    {
      dolphin_list();
      return 0;
    }

  if (argc == 3 && strcmp(argv[1], "run") == 0)
    {
      return dolphin_run(argv[2]);
    }

  fprintf(stderr, "usage: dolphin [list|run NAME] (see dolphin list)\n");
  return 1;
}
