/****************************************************************************
 * tests/host/dolphin/test_dolphin.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <nuttx/lib/builtin.h>
#include <spawn.h>

static const char *g_available;
static const char *g_started;
static char *const *g_argv;
static struct builtin_s g_builtin;
static int g_spawn_result;
static int g_wait_results[3];
static int g_wait_statuses[3];
static unsigned int g_wait_count;

int builtin_isavail(const char *appname)
{
  return g_available != NULL && strcmp(appname, g_available) == 0 ? 0 : -ENOENT;
}

const struct builtin_s *builtin_for_index(int index)
{
  return index == 0 && g_available != NULL ? &g_builtin : NULL;
}

int posix_spawnattr_init(posix_spawnattr_t *attr)
{
  memset(attr, 0, sizeof(*attr));
  return 0;
}

int posix_spawnattr_destroy(posix_spawnattr_t *attr)
{
  (void)attr;
  return 0;
}

int posix_spawnattr_setpriority(posix_spawnattr_t *attr, int priority)
{
  attr->priority = priority;
  return 0;
}

int posix_spawnattr_setstacksize(posix_spawnattr_t *attr, size_t stacksize)
{
  attr->stacksize = stacksize;
  return 0;
}

pid_t task_spawn(const char *name, main_t entry,
                 const posix_spawn_file_actions_t *actions,
                 const posix_spawnattr_t *attr, char *const argv[],
                 char *const envp[])
{
  assert(entry == g_builtin.main);
  assert(actions == NULL);
  assert(envp == NULL);
  assert(attr->priority == g_builtin.priority);
  assert(attr->stacksize == (size_t)g_builtin.stacksize);
  g_started = name;
  g_argv = argv;

  return g_spawn_result;
}

static pid_t dolphin_test_waitpid(pid_t pid, int *status, int options)
{
  unsigned int index = g_wait_count++;

  assert(pid == g_spawn_result);
  assert(options == 0);
  if (g_wait_results[index] < 0)
    {
      errno = -g_wait_results[index];
      return -1;
    }

  *status = g_wait_statuses[index];
  return g_wait_results[index];
}

#define DOLPHIN_MAIN dolphin_main
#define waitpid dolphin_test_waitpid
#include "../../../app/dolphin/dolphin_main.c"
#undef waitpid
#undef DOLPHIN_MAIN

static void reset(const char *available)
{
  g_available = available;
  g_started = NULL;
  g_argv = NULL;
  g_builtin.name = available;
  g_builtin.priority = 123;
  g_builtin.stacksize = 4567;
  g_builtin.main = dolphin_main;
  g_spawn_result = 17;
  g_wait_results[0] = 17;
  g_wait_statuses[0] = 0;
  g_wait_count = 0;
}

static void test_allowlist_and_exit_status(void)
{
  char *argv[] = { "dolphin", "run", "wifi", NULL };

  reset("bkwifi");
  g_wait_statuses[0] = 7 << 8;
  assert(dolphin_main(3, argv) == 7);
  assert(strcmp(g_started, "bkwifi") == 0);
  assert(strcmp(g_argv[0], "status") == 0);
  assert(g_argv[1] == NULL);

  argv[2] = "unsupported";
  reset("bkwifi");
  assert(dolphin_main(3, argv) == 1);
  assert(g_started == NULL);
}

static void test_unavailable_and_spawn_failure(void)
{
  char *argv[] = { "dolphin", "run", "wifi", NULL };

  reset(NULL);
  assert(dolphin_main(3, argv) == 1);
  assert(g_started == NULL);

  reset("bkwifi");
  g_spawn_result = -1;
  assert(dolphin_main(3, argv) == 1);
  assert(strcmp(g_started, "bkwifi") == 0);
  assert(g_wait_count == 0);
}

static void test_wait_retry_and_failure_status(void)
{
  char *argv[] = { "dolphin", "run", "wifi", NULL };

  reset("bkwifi");
  g_wait_results[0] = -EINTR;
  g_wait_results[1] = 17;
  g_wait_statuses[1] = 0;
  assert(dolphin_main(3, argv) == 0);
  assert(g_wait_count == 2);

  reset("bkwifi");
  g_wait_results[0] = -ECHILD;
  assert(dolphin_main(3, argv) == 1);

  reset("bkwifi");
  g_wait_statuses[0] = SIGTERM;
  assert(dolphin_main(3, argv) == 128 + SIGTERM);
}

int main(void)
{
  char *absent[] = { "dolphin", "run", "nfc", NULL };
  reset("bknfc");
  assert(dolphin_find("nfc") == NULL);
  assert(dolphin_find("motion") == NULL);
  assert(dolphin_find("health") == NULL);
  assert(dolphin_main(3, absent) == 1);
  assert(g_started == NULL);
  test_allowlist_and_exit_status();
  test_unavailable_and_spawn_failure();
  test_wait_retry_and_failure_status();
  puts("DOLPHIN_HOST_TEST_PASS");
  return 0;
}
