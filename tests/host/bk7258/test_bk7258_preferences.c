/****************************************************************************
 * tests/host/bk7258/test_bk7258_preferences.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <nuttx/mutex.h>

#include "bk7258_preferences.h"

static const char *g_volume;
static const char *g_persona;
static int g_volume_error;
static int g_persona_error;
static int g_set_error;
static int g_commit_error;
static unsigned int g_set_calls;
static unsigned int g_commit_calls;
static char g_last_key[32];
static char g_last_value[32];
static int g_storage_error;
static int g_cleanup_error;
static int g_storage_active;
static unsigned int g_storage_calls;
int nxmutex_lock(mutex_t *mutex) { return -pthread_mutex_lock(mutex); }
int nxmutex_unlock(mutex_t *mutex) { return -pthread_mutex_unlock(mutex); }

int bk7258_preferences_storage_begin(void)
{
  g_storage_calls++;
  assert(!g_storage_active);
  if (g_storage_error < 0)
    {
      return g_storage_error;
    }
  g_storage_active = 1;
  return 0;
}

int bk7258_preferences_storage_end(int status)
{
  assert(g_storage_active);
  g_storage_active = 0;
  return status < 0 ? status : g_cleanup_error;
}

int property_get_with_err(const char *key, char *value)
{
  assert(g_storage_active);
  const char *source;
  int error;

  if (strcmp(key, "persist.shaniu.volume") == 0)
    {
      source = g_volume;
      error = g_volume_error;
    }
  else if (strcmp(key, "persist.shaniu.persona") == 0)
    {
      source = g_persona;
      error = g_persona_error;
    }
  else
    {
      return -EINVAL;
    }

  if (error < 0)
    {
      return error;
    }

  assert(source != NULL);
  strcpy(value, source);
  return (int)strlen(value);
}

int property_set(const char *key, const char *value)
{
  assert(g_storage_active);
  g_set_calls++;
  strcpy(g_last_key, key);
  strcpy(g_last_value, value);
  return g_set_error;
}

int property_commit(void)
{
  assert(g_storage_active);
  g_commit_calls++;
  return g_commit_error;
}

static void reset_db(void)
{
  g_volume = NULL;
  g_persona = NULL;
  g_volume_error = 0;
  g_persona_error = 0;
  g_set_error = 0;
  g_commit_error = 0;
  g_set_calls = 0;
  g_commit_calls = 0;
  g_last_key[0] = '\0';
  g_last_value[0] = '\0';
}

static int storage_operation(void *context)
{
  assert(g_storage_active);
  int *calls = context;
  (*calls)++;
  return 0;
}

int main(void)
{
  struct bk7258_preferences_s preferences;

  int operation_calls = 0;
  assert(bk7258_preferences_with_storage(NULL, NULL) == -EINVAL);
  g_storage_error = -EBUSY;
  assert(bk7258_preferences_with_storage(storage_operation, &operation_calls) == -EBUSY);
  assert(operation_calls == 0 && !g_storage_active);
  g_storage_error = 0;
  assert(bk7258_preferences_with_storage(storage_operation, &operation_calls) == 0);
  assert(operation_calls == 1 && !g_storage_active);
  g_cleanup_error = -EIO;
  assert(bk7258_preferences_with_storage(storage_operation, &operation_calls) == -EIO);
  assert(operation_calls == 2 && !g_storage_active);
  g_cleanup_error = 0;

  reset_db();
  g_volume_error = -ENOENT;
  g_persona_error = -ENOENT;
  assert(bk7258_preferences_get(&preferences) == 0);
  assert(preferences.volume_percent == 50u);
  assert(preferences.persona == BK7258_PERSONA_GENTLE);
  assert(preferences.volume_is_default);
  assert(preferences.persona_is_default);

  reset_db();
  g_volume = "100";
  g_persona = "tsundere_lite";
  assert(bk7258_preferences_get(&preferences) == 0);
  assert(preferences.volume_percent == 100u);
  assert(preferences.persona == BK7258_PERSONA_TSUNDERE_LITE);
  assert(!preferences.volume_is_default);
  assert(!preferences.persona_is_default);

  reset_db();
  g_volume = "50junk";
  g_persona = "gentle";
  assert(bk7258_preferences_get(&preferences) == -EBADMSG);
  g_volume = "101";
  assert(bk7258_preferences_get(&preferences) == -ERANGE);
  g_volume = "50";
  g_persona = "unknown";
  assert(bk7258_preferences_get(&preferences) == -EINVAL);

  reset_db();
  g_volume_error = -EIO;
  assert(bk7258_preferences_get(&preferences) == -EIO);
  g_volume_error = -ENOENT;
  g_persona_error = -EACCES;
  assert(bk7258_preferences_get(&preferences) == -EACCES);

  reset_db();
  assert(bk7258_preferences_set_volume(101u) == -ERANGE);
  assert(g_set_calls == 0u && g_commit_calls == 0u);
  assert(bk7258_preferences_set_persona("invalid") == -EINVAL);
  assert(g_set_calls == 0u && g_commit_calls == 0u);

  assert(bk7258_preferences_set_volume(42u) == 0);
  assert(strcmp(g_last_key, "persist.shaniu.volume") == 0);
  assert(strcmp(g_last_value, "42") == 0);
  assert(g_set_calls == 1u && g_commit_calls == 1u);
  assert(bk7258_preferences_set_persona("playful") == 0);
  assert(strcmp(g_last_key, "persist.shaniu.persona") == 0);
  assert(strcmp(g_last_value, "playful") == 0);

  reset_db();
  g_set_error = -EIO;
  assert(bk7258_preferences_set_persona("quiet") == -EIO);
  assert(g_commit_calls == 0u);
  reset_db();
  g_commit_error = -ENOSPC;
  assert(bk7258_preferences_set_volume(0u) == -ENOSPC);
  assert(g_set_calls == 1u && g_commit_calls == 1u);

  reset_db();
  g_storage_error = -EBUSY;
  assert(bk7258_preferences_set_volume(50u) == -EBUSY);
  assert(g_set_calls == 0u);
  assert(bk7258_preferences_get(&preferences) == -EBUSY);
  g_storage_error = 0;
  g_cleanup_error = -EIO;
  g_volume = "42";
  g_persona = "quiet";
  preferences.volume_percent = 99;
  assert(bk7258_preferences_get(&preferences) == -EIO);
  assert(preferences.volume_percent == 99);
  assert(bk7258_preferences_set_volume(50u) == -EIO);
  assert(!g_storage_active);
  /* A failed commit/cleanup cannot silently reuse the previous volume. */
  unsigned int volume = 77;
  g_storage_error = -EBUSY;
  assert(bk7258_preferences_playback_volume(&volume) == -EBUSY);
  assert(volume == 77);
  g_storage_error = 0;
  g_cleanup_error = 0;
  g_volume = "42";
  g_persona = "quiet";
  assert(bk7258_preferences_playback_volume(&volume) == 0 && volume == 42);
  unsigned int accesses = g_storage_calls;
  g_storage_error = -EBUSY;
  assert(bk7258_preferences_playback_volume(&volume) == 0 && volume == 42);
  assert(g_storage_calls == accesses);
  assert(bk7258_preferences_set_volume(60) == -EBUSY);
  accesses = g_storage_calls;
  assert(bk7258_preferences_playback_volume(&volume) == 0 && volume == 42);
  assert(g_storage_calls == accesses);
  g_storage_error = 0;
  assert(bk7258_preferences_set_volume(60) == 0);
  accesses = g_storage_calls;
  assert(bk7258_preferences_playback_volume(&volume) == 0 && volume == 60);
  assert(g_storage_calls == accesses);
  /* External edits stay out of the playback path until an explicit refresh. */
  g_volume = "80";
  assert(bk7258_preferences_playback_volume(&volume) == 0 && volume == 60);
  assert(bk7258_preferences_get(&preferences) == 0);
  assert(bk7258_preferences_playback_volume(&volume) == 0 && volume == 80);
  assert(bk7258_preferences_playback_volume(NULL) == -EINVAL);
  puts("BK7258_PREFERENCES_HOST_PASS");
  return 0;
}
