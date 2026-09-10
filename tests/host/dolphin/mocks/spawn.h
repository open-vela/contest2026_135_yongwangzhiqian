/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __MOCK_SPAWN_H
#define __MOCK_SPAWN_H
#include <stddef.h>
#include <sys/types.h>
#include <nuttx/lib/builtin.h>
typedef struct
{
  int priority;
  size_t stacksize;
} posix_spawnattr_t;
typedef void *posix_spawn_file_actions_t;
int posix_spawnattr_init(posix_spawnattr_t *attr);
int posix_spawnattr_destroy(posix_spawnattr_t *attr);
int posix_spawnattr_setpriority(posix_spawnattr_t *attr, int priority);
int posix_spawnattr_setstacksize(posix_spawnattr_t *attr, size_t stacksize);
pid_t task_spawn(const char *name, main_t entry,
                 const posix_spawn_file_actions_t *actions,
                 const posix_spawnattr_t *attr, char *const argv[],
                 char *const envp[]);
#endif
