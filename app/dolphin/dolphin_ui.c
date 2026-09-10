/****************************************************************************
 * app/dolphin/dolphin_ui.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <syslog.h>
#include <unistd.h>

#include <lvgl/lvgl.h>

#include "dolphin_ui.h"
#ifdef CONFIG_BK7258_LVGL_FB_ACCEL
#  include <arch/chip/bk7258_lvgl_fb.h>
#endif
#if defined(CONFIG_BK7258_BT_IPC) && defined(CONFIG_WIRELESS_BLUETOOTH_HOST)
#  include <arch/chip/bk7258_ble_scan.h>
#  define DOLPHIN_HAS_BLE_SCAN 1
#endif
#ifdef CONFIG_BK7258_WIFI_VNET
#  include <arch/chip/bk7258_wifi.h>
#endif

#ifndef CONFIG_DOLPHIN_STORAGE_ROOT
#  define CONFIG_DOLPHIN_STORAGE_ROOT "/mnt/tf"
#endif

#define DOLPHIN_UI_PRIORITY       45
#ifndef CONFIG_DOLPHIN_UI_STACKSIZE
#  define CONFIG_DOLPHIN_UI_STACKSIZE 16384
#endif
#define DOLPHIN_UI_STACKSIZE      CONFIG_DOLPHIN_UI_STACKSIZE
#define DOLPHIN_MAX_FILES         64
#define DOLPHIN_MAX_ENUMERATED    256
#define DOLPHIN_NAME_SIZE         48
#define DOLPHIN_PATH_SIZE         192
#define DOLPHIN_PREVIEW_BYTES     2048

struct dolphin_file_s
{
  char name[DOLPHIN_NAME_SIZE];
  off_t size;
  bool directory;
};

struct dolphin_scan_s
{
  pthread_mutex_t lock;
  struct dolphin_file_s files[DOLPHIN_MAX_FILES];
  char relative[DOLPHIN_PATH_SIZE];
  unsigned int count;
  int error;
  bool more;
  bool truncated;
  bool running;
  bool complete;
};

struct dolphin_preview_s
{
  pthread_mutex_t lock;
  char relative[DOLPHIN_PATH_SIZE];
  char name[DOLPHIN_NAME_SIZE];
  char text[DOLPHIN_PREVIEW_BYTES + 1];
  off_t size;
  size_t length;
  unsigned int request;
  unsigned int completed_request;
  int error;
  bool truncated;
  bool running;
  bool complete;
};

static struct dolphin_scan_s g_scan =
{
  .lock = PTHREAD_MUTEX_INITIALIZER
};
static struct dolphin_preview_s g_preview =
{
  .lock = PTHREAD_MUTEX_INITIALIZER
};
#ifdef DOLPHIN_HAS_BLE_SCAN
static lv_obj_t *g_ble_page;
static lv_obj_t *g_ble_status;
static lv_obj_t *g_ble_results;
static uint32_t g_ble_started;
static bool g_ble_owned;
static bool g_ble_stop_sent;
static void dolphin_ble(lv_event_t *event);
#endif
static bool g_ui_started;
static lv_obj_t *g_page;
static lv_obj_t *g_files_page;
static lv_obj_t *g_preview_page;
static lv_display_t *g_display;
#ifdef CONFIG_BK7258_WIFI_VNET
static lv_obj_t *g_wifi_page;
static uint32_t g_wifi_ticket;
static uint32_t g_channels_ticket;
static lv_obj_t *g_channels_page;
static void dolphin_channels(lv_event_t *event);
static void dolphin_wifi_scan(lv_event_t *event);
#endif

static void dolphin_home(lv_event_t *event);
static void dolphin_files(lv_event_t *event);
static void dolphin_network(lv_event_t *event);
static void dolphin_device(lv_event_t *event);
static void dolphin_files_up(lv_event_t *event);
static void dolphin_preview_timer(lv_timer_t *timer);

static void dolphin_label(lv_obj_t *parent, const char *text, int32_t size)
{
  lv_obj_t *label = lv_label_create(parent);

  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, lv_color_hex(0xf2ead8), LV_PART_MAIN);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  if (size > 0)
    {
      lv_obj_set_width(label, size);
      lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    }
}

static lv_obj_t *dolphin_page(const char *title)
{
  lv_obj_t *screen = lv_screen_active();
  lv_obj_t *header;
  lv_obj_t *home;
  lv_obj_t *title_label;
  lv_obj_t *home_label;
  int32_t width = lv_display_get_horizontal_resolution(g_display);
  int32_t height = lv_display_get_vertical_resolution(g_display);

#ifdef DOLPHIN_HAS_BLE_SCAN
  if (g_ble_page != NULL && g_ble_owned && !g_ble_stop_sent)
    {
      (void)bk7258_ble_scan_stop();
      g_ble_stop_sent = true;
    }
  g_ble_page = NULL;
#endif
  lv_obj_clean(screen);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  g_files_page = NULL;
  g_preview_page = NULL;
#ifdef CONFIG_BK7258_WIFI_VNET
  if (g_wifi_page != NULL && g_wifi_ticket != 0)
    (void)bk7258_wifi_diagnostic_cancel(g_wifi_ticket);
  if (g_channels_page != NULL && g_channels_ticket != 0)
    (void)bk7258_wifi_diagnostic_cancel(g_channels_ticket);
  g_wifi_page = NULL;
  g_channels_page = NULL;
#endif
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x101516), LV_PART_MAIN);
  lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
  header = lv_obj_create(screen);
  lv_obj_set_size(header, width, 48);
  lv_obj_set_style_pad_all(header, 0, LV_PART_MAIN);
  lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_pos(header, 0, 0);
  lv_obj_set_style_bg_color(header, lv_color_hex(0x1a2224), LV_PART_MAIN);
  lv_obj_set_style_border_width(header, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(header, 0, LV_PART_MAIN);
  title_label = lv_label_create(header);
  lv_label_set_text(title_label, title);
  lv_obj_set_style_text_color(title_label, lv_color_hex(0xff9d22), LV_PART_MAIN);
  lv_obj_set_style_text_font(title_label, LV_FONT_DEFAULT, LV_PART_MAIN);
  lv_obj_align(title_label, LV_ALIGN_LEFT_MID, 12, 0);
  home = lv_button_create(header);
  lv_obj_set_size(home, 76, 36);
  lv_obj_align(home, LV_ALIGN_RIGHT_MID, -6, 0);
  lv_obj_set_style_bg_color(home, lv_color_hex(0xf08a24), LV_PART_MAIN);
  lv_obj_set_style_radius(home, 8, LV_PART_MAIN);
  lv_obj_add_event_cb(home, dolphin_home, LV_EVENT_CLICKED, NULL);
  home_label = lv_label_create(home);
  lv_label_set_text(home_label, "HOME");
  lv_obj_set_style_text_font(home_label, LV_FONT_DEFAULT, LV_PART_MAIN);
  lv_obj_set_style_text_color(home_label, lv_color_hex(0x252525), LV_PART_MAIN);
  lv_obj_center(home_label);
  g_page = lv_obj_create(screen);
  lv_obj_set_size(g_page, width, height - 48);
  lv_obj_set_pos(g_page, 0, 48);
  lv_obj_set_style_bg_color(g_page, lv_color_hex(0x151d1f), LV_PART_MAIN);
  lv_obj_set_style_border_width(g_page, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(g_page, 8, LV_PART_MAIN);
  lv_obj_set_style_pad_all(g_page, 12, LV_PART_MAIN);
  lv_obj_set_flex_flow(g_page, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(g_page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  return g_page;
}

static lv_obj_t *dolphin_button(lv_obj_t *parent, const char *text,
                                lv_event_cb_t callback, void *data)
{
  lv_obj_t *button = lv_button_create(parent);
  lv_obj_t *label = lv_label_create(button);

  lv_obj_set_size(button, LV_PCT(94), 52);
  lv_obj_set_style_bg_color(button, lv_color_hex(0x202b2e), LV_PART_MAIN);
  lv_obj_set_style_bg_color(button, lv_color_hex(0xc66b19), LV_PART_MAIN |
                             LV_STATE_PRESSED);
  lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(button, lv_color_hex(0xbd6d25), LV_PART_MAIN);
  lv_obj_set_style_text_color(button, lv_color_hex(0xf2ead8), LV_PART_MAIN);
  lv_obj_set_style_radius(button, 10, LV_PART_MAIN);
  lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, data);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, lv_color_hex(0xf2ead8), LV_PART_MAIN);
  lv_obj_center(label);
  return button;
}

static void dolphin_feature_card(lv_obj_t *parent, const char *title,
                                 const char *detail, lv_event_cb_t callback)
{
  lv_obj_t *card = dolphin_button(parent, title, callback, NULL);
  lv_obj_t *title_label = lv_obj_get_child(card, 0);
  lv_obj_t *detail_label = lv_label_create(card);

  lv_obj_set_height(card, 72);
  lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 14, 10);
  lv_obj_set_width(title_label, LV_PCT(88));
  lv_label_set_long_mode(title_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_pad_all(title_label, 0, LV_PART_MAIN);
  lv_obj_remove_flag(title_label, LV_OBJ_FLAG_SCROLLABLE);
  lv_label_set_text(detail_label, detail);
  lv_obj_set_style_text_font(detail_label, LV_FONT_DEFAULT, LV_PART_MAIN);
  lv_obj_set_style_text_color(detail_label, lv_color_hex(0xb8c2bd), LV_PART_MAIN);
  lv_obj_set_width(detail_label, LV_PCT(88));
  lv_label_set_long_mode(detail_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_pad_all(detail_label, 0, LV_PART_MAIN);
  lv_obj_remove_flag(detail_label, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(detail_label, LV_ALIGN_BOTTOM_LEFT, 14, -9);
}

static void dolphin_home_art(lv_obj_t *parent)
{
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_t *body;
  lv_obj_t *fin;
  lv_obj_t *dot;
  lv_obj_t *title;
  lv_obj_t *caption;

  lv_obj_set_size(card, LV_PCT(94), 82);
  lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x1d292b), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(0x355052), LV_PART_MAIN);
  lv_obj_set_style_radius(card, 14, LV_PART_MAIN);
  body = lv_obj_create(card);
  lv_obj_set_size(body, 54, 26);
  lv_obj_set_pos(body, 18, 29);
  lv_obj_set_style_radius(body, 24, LV_PART_MAIN);
  lv_obj_set_style_bg_color(body, lv_color_hex(0x4b7776), LV_PART_MAIN);
  lv_obj_set_style_border_width(body, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(body, 0, LV_PART_MAIN);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  fin = lv_obj_create(card);
  lv_obj_set_size(fin, 18, 12);
  lv_obj_set_pos(fin, 59, 23);
  lv_obj_set_style_radius(fin, 8, LV_PART_MAIN);
  lv_obj_set_style_bg_color(fin, lv_color_hex(0x4b7776), LV_PART_MAIN);
  lv_obj_set_style_border_width(fin, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(fin, 0, LV_PART_MAIN);
  lv_obj_remove_flag(fin, LV_OBJ_FLAG_SCROLLABLE);
  dot = lv_obj_create(card);
  lv_obj_set_size(dot, 9, 9);
  lv_obj_set_pos(dot, 78, 37);
  lv_obj_set_style_radius(dot, 9, LV_PART_MAIN);
  lv_obj_set_style_bg_color(dot, lv_color_hex(0xf08a24), LV_PART_MAIN);
  lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(dot, 0, LV_PART_MAIN);
  lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
  title = lv_label_create(card);
  lv_label_set_text(title, "DOLPHIN DESK");
  lv_obj_set_style_text_font(title, LV_FONT_DEFAULT, LV_PART_MAIN);
  lv_obj_set_style_text_color(title, lv_color_hex(0xf2ead8), LV_PART_MAIN);
  lv_obj_set_pos(title, 112, 20);
  lv_obj_set_width(title, LV_PCT(52));
  lv_obj_set_style_pad_all(title, 0, LV_PART_MAIN);
  lv_obj_remove_flag(title, LV_OBJ_FLAG_SCROLLABLE);
  caption = lv_label_create(card);
  lv_label_set_text(caption, "Local status");
  lv_obj_set_style_text_font(caption, LV_FONT_DEFAULT, LV_PART_MAIN);
  lv_obj_set_style_text_color(caption, lv_color_hex(0xb8c2bd), LV_PART_MAIN);
  lv_obj_set_pos(caption, 112, 43);
  lv_obj_set_width(caption, LV_PCT(52));
  lv_label_set_long_mode(caption, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_pad_all(caption, 0, LV_PART_MAIN);
  lv_obj_remove_flag(caption, LV_OBJ_FLAG_SCROLLABLE);
}

static int dolphin_storage_path(char *path, size_t length, const char *relative)
{
  int result;

  if (relative[0] == '\0')
    {
      result = snprintf(path, length, "%s", CONFIG_DOLPHIN_STORAGE_ROOT);
    }
  else
    {
      result = snprintf(path, length, "%s/%s", CONFIG_DOLPHIN_STORAGE_ROOT,
                        relative);
    }

  return result < 0 || (size_t)result >= length ? -ENAMETOOLONG : OK;
}

/* Each component is checked before opendir().  This rejects a configured
 * root or an entered intermediate component that is a symlink; it is not a
 * substitute for an fd-based TOCTOU isolation boundary.
 */

static int dolphin_verify_storage_path(const char *relative)
{
  struct stat st;
  char current[DOLPHIN_PATH_SIZE];
  char next[DOLPHIN_PATH_SIZE];
  char components[DOLPHIN_PATH_SIZE];
  char *state;
  char *component;
  int result;

  result = snprintf(current, sizeof(current), "%s", CONFIG_DOLPHIN_STORAGE_ROOT);
  if (result < 0 || (size_t)result >= sizeof(current))
    {
      return -ENAMETOOLONG;
    }

  if (lstat(current, &st) < 0)
    {
      return -errno;
    }
  if (S_ISLNK(st.st_mode))
    {
      return -ELOOP;
    }
  if (!S_ISDIR(st.st_mode))
    {
      return -ENOTDIR;
    }

  snprintf(components, sizeof(components), "%s", relative);
  component = strtok_r(components, "/", &state);
  while (component != NULL)
    {
      result = snprintf(next, sizeof(next), "%s/%s", current, component);
      if (result < 0 || (size_t)result >= sizeof(next))
        {
          return -ENAMETOOLONG;
        }
      snprintf(current, sizeof(current), "%s", next);
      if (lstat(current, &st) < 0)
        {
          return -errno;
        }
      if (S_ISLNK(st.st_mode))
        {
          return -ELOOP;
        }
      if (!S_ISDIR(st.st_mode))
        {
          return -ENOTDIR;
        }
      component = strtok_r(NULL, "/", &state);
    }

  return OK;
}

static void *dolphin_scan_worker(void *arg)
{
  struct dolphin_scan_s *scan = &g_scan;
  struct dirent *entry;
  DIR *directory = NULL;
  char path[DOLPHIN_PATH_SIZE];
  char child[DOLPHIN_PATH_SIZE];
  char relative[DOLPHIN_PATH_SIZE];
  unsigned int count = 0;
  unsigned int enumerated = 0;
  int error = 0;
  bool more = false;
  bool truncated = false;

  (void)arg;
  pthread_mutex_lock(&scan->lock);
  memcpy(relative, scan->relative, sizeof(relative));
  error = dolphin_storage_path(path, sizeof(path), relative);
  pthread_mutex_unlock(&scan->lock);
  if (error == 0)
    {
      error = dolphin_verify_storage_path(relative);
    }

  if (error == 0 && (directory = opendir(path)) == NULL)
    {
      error = -errno;
    }

  while (error == 0)
    {
      struct stat st;
      size_t name_length;

      errno = 0;
      entry = readdir(directory);
      if (entry == NULL)
        {
          if (errno != 0)
            {
              error = -errno;
            }
          break;
        }

      name_length = strlen(entry->d_name);

      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
          name_length == 0)
        {
          continue;
        }

      if (++enumerated > DOLPHIN_MAX_ENUMERATED)
        {
          truncated = true;
          break;
        }

      if (name_length >= DOLPHIN_NAME_SIZE ||
          strchr(entry->d_name, '/') != NULL)
        {
          continue;
        }

      if (snprintf(child, sizeof(child), "%s/%s", path, entry->d_name) >=
          (int)sizeof(child) || lstat(child, &st) < 0 || S_ISLNK(st.st_mode))
        {
          continue;
        }

      if (count == DOLPHIN_MAX_FILES)
        {
          more = true;
        }
      else
        {
          pthread_mutex_lock(&scan->lock);
          snprintf(scan->files[count].name, sizeof(scan->files[count].name), "%s",
                   entry->d_name);
          scan->files[count].size = st.st_size;
          scan->files[count].directory = S_ISDIR(st.st_mode);
          pthread_mutex_unlock(&scan->lock);
          count++;
        }
    }

  if (directory != NULL)
    {
      closedir(directory);
    }

  pthread_mutex_lock(&scan->lock);
  scan->count = count;
  scan->error = error;
  scan->more = more;
  scan->truncated = truncated;
  scan->running = false;
  scan->complete = true;
  pthread_mutex_unlock(&scan->lock);
  return NULL;
}

static bool dolphin_preview_text_name(const char *name)
{
  const char *suffix = strrchr(name, '.');

  return suffix != NULL &&
         (strcmp(suffix, ".txt") == 0 || strcmp(suffix, ".log") == 0 ||
          strcmp(suffix, ".md") == 0 || strcmp(suffix, ".csv") == 0 ||
          strcmp(suffix, ".json") == 0 || strcmp(suffix, ".ini") == 0 ||
          strcmp(suffix, ".cfg") == 0);
}

static int dolphin_preview_ascii(const char *text, size_t length)
{
  size_t index;

  for (index = 0; index < length; index++)
    {
      unsigned char value = (unsigned char)text[index];

      if (value < 0x20 && value != '\n' && value != '\r' && value != '\t')
        {
          return -EILSEQ;
        }
      if (value > 0x7e)
        {
          return -ENOTSUP;
        }
    }

  return OK;
}

static void *dolphin_preview_worker(void *arg)
{
  char relative[DOLPHIN_PATH_SIZE];
  char name[DOLPHIN_NAME_SIZE];
  char path[DOLPHIN_PATH_SIZE];
  char text[DOLPHIN_PREVIEW_BYTES + 1];
  struct stat st;
  unsigned int request;
  size_t length = 0;
  int fd = -1;
  int error = 0;
  bool truncated = false;

  (void)arg;
  pthread_mutex_lock(&g_preview.lock);
  memcpy(relative, g_preview.relative, sizeof(relative));
  memcpy(name, g_preview.name, sizeof(name));
  request = g_preview.request;
  pthread_mutex_unlock(&g_preview.lock);
  error = dolphin_verify_storage_path(relative);
  if (error == 0)
    {
      error = dolphin_storage_path(path, sizeof(path), relative);
    }
  if (error == 0)
    {
      size_t path_length = strlen(path);
      int appended = snprintf(path + path_length, sizeof(path) - path_length,
                              "/%s", name);

      if (appended < 0 || (size_t)appended >= sizeof(path) - path_length)
        {
          error = -ENAMETOOLONG;
        }
    }
  if (error == 0 && lstat(path, &st) < 0)
    {
      error = -errno;
    }
  else if (error == 0 && (S_ISLNK(st.st_mode) || !S_ISREG(st.st_mode)))
    {
      error = -EINVAL;
    }
  if (error == 0 && (fd = open(path, O_RDONLY)) < 0)
    {
      error = -errno;
    }
  while (error == 0 && length < DOLPHIN_PREVIEW_BYTES)
    {
      ssize_t read_count = read(fd, text + length, DOLPHIN_PREVIEW_BYTES - length);

      if (read_count < 0)
        {
          error = -errno;
        }
      else if (read_count == 0)
        {
          break;
        }
      else
        {
          length += (size_t)read_count;
        }
    }
  if (error == 0 && length == DOLPHIN_PREVIEW_BYTES)
    {
      char extra;
      ssize_t read_count = read(fd, &extra, 1);

      if (read_count < 0)
        {
          error = -errno;
        }
      else if (read_count > 0)
        {
          truncated = true;
        }
    }
  if (fd >= 0)
    {
      close(fd);
    }
  if (error == 0 && (error = dolphin_preview_ascii(text, length)) < 0)
    {
      length = 0;
    }
  text[length] = '\0';

  pthread_mutex_lock(&g_preview.lock);
  if (request == g_preview.request)
    {
      memcpy(g_preview.text, text, length + 1);
      g_preview.length = length;
      g_preview.error = error;
      g_preview.truncated = truncated;
      g_preview.completed_request = request;
      g_preview.running = false;
      g_preview.complete = true;
    }
  pthread_mutex_unlock(&g_preview.lock);
  return NULL;
}

static int dolphin_preview_start(const struct dolphin_file_s *file)
{
  pthread_t thread;
  pthread_attr_t attr;
  int result;

  if (!dolphin_preview_text_name(file->name))
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&g_preview.lock);
  if (g_preview.running)
    {
      pthread_mutex_unlock(&g_preview.lock);
      return -EBUSY;
    }
  pthread_mutex_lock(&g_scan.lock);
  memcpy(g_preview.relative, g_scan.relative, sizeof(g_preview.relative));
  pthread_mutex_unlock(&g_scan.lock);
  snprintf(g_preview.name, sizeof(g_preview.name), "%s", file->name);
  g_preview.size = file->size;
  g_preview.length = 0;
  g_preview.error = 0;
  g_preview.truncated = false;
  g_preview.complete = false;
  g_preview.request++;
  g_preview.running = true;
  result = pthread_attr_init(&attr);
  if (result == 0)
    {
      result = pthread_attr_setstacksize(&attr, 16384);
      if (result == 0)
        {
          result = pthread_create(&thread, &attr, dolphin_preview_worker, NULL);
        }
      pthread_attr_destroy(&attr);
    }
  if (result == 0)
    {
      pthread_detach(thread);
    }
  else
    {
      g_preview.running = false;
      g_preview.error = -result;
      g_preview.completed_request = g_preview.request;
      g_preview.complete = true;
    }
  pthread_mutex_unlock(&g_preview.lock);
  return result == 0 ? OK : -result;
}

static void dolphin_file_detail(lv_event_t *event)
{
  const struct dolphin_file_s *file = lv_event_get_user_data(event);
  char text[96];
  int result;

  if (file->directory)
    {
      return;
    }

  dolphin_page("FILE DETAIL");
  snprintf(text, sizeof(text), "%s\nSize: %lld bytes", file->name,
           (long long)file->size);
  dolphin_label(g_page, text, LV_PCT(94));
  result = dolphin_preview_start(file);
  if (result == OK)
    {
      g_preview_page = g_page;
      dolphin_label(g_page, "Loading text preview...", LV_PCT(94));
    }
  else if (result == -EINVAL)
    {
      dolphin_label(g_page, "Preview available for text files only", LV_PCT(94));
    }
  else if (result == -EBUSY)
    {
      dolphin_label(g_page, "Preview worker is busy", LV_PCT(94));
    }
  else
    {
      snprintf(text, sizeof(text), "Preview unavailable: %d", -result);
      dolphin_label(g_page, text, LV_PCT(94));
    }
  dolphin_button(g_page, "BACK", dolphin_files, NULL);
}

static void dolphin_open_directory(lv_event_t *event)
{
  const struct dolphin_file_s *file = lv_event_get_user_data(event);
  char next[DOLPHIN_PATH_SIZE];
  int result;

  if (!file->directory)
    {
      dolphin_file_detail(event);
      return;
    }

  pthread_mutex_lock(&g_scan.lock);
  if (g_scan.relative[0] == '\0')
    {
      result = snprintf(next, sizeof(next), "%s", file->name);
    }
  else
    {
      result = snprintf(next, sizeof(next), "%s/%s", g_scan.relative,
                        file->name);
    }
  if (result >= 0 && (size_t)result < sizeof(next))
    {
      snprintf(g_scan.relative, sizeof(g_scan.relative), "%s", next);
    }
  pthread_mutex_unlock(&g_scan.lock);
  if (result < 0 || (size_t)result >= sizeof(next))
    {
      syslog(LOG_ERR, "dolphin-ui: file path too long\n");
    }
  dolphin_files(NULL);
}

static void dolphin_files_up(lv_event_t *event)
{
  char *separator;

  (void)event;
  pthread_mutex_lock(&g_scan.lock);
  if (!g_scan.running)
    {
      separator = strrchr(g_scan.relative, '/');
      if (separator != NULL)
        {
          *separator = '\0';
        }
      else
        {
          g_scan.relative[0] = '\0';
        }
    }
  pthread_mutex_unlock(&g_scan.lock);
  dolphin_files(NULL);
}

static void dolphin_files(lv_event_t *event)
{
  pthread_t thread;
  char relative[DOLPHIN_PATH_SIZE];
  char location[DOLPHIN_PATH_SIZE + 16];
  int result;

  (void)event;
  dolphin_page("FILES");
  g_files_page = g_page;
  pthread_mutex_lock(&g_scan.lock);
  snprintf(relative, sizeof(relative), "%s", g_scan.relative);
  if (!g_scan.running)
    {
      g_scan.count = 0;
      g_scan.error = 0;
      g_scan.more = false;
      g_scan.truncated = false;
      g_scan.complete = false;
      g_scan.running = true;
      result = pthread_create(&thread, NULL, dolphin_scan_worker, NULL);
      if (result == 0)
        {
          pthread_detach(thread);
        }
      else
        {
          g_scan.running = false;
          g_scan.complete = true;
          g_scan.error = -result;
        }
    }
  pthread_mutex_unlock(&g_scan.lock);
  snprintf(location, sizeof(location), "Read-only: /%s", relative);
  dolphin_label(g_page, location, LV_PCT(94));
  if (relative[0] != '\0')
    {
      dolphin_button(g_page, "UP", dolphin_files_up, NULL);
    }
  dolphin_label(g_page, "Loading...", LV_PCT(94));
}

#ifdef CONFIG_BK7258_WIFI_VNET
static void dolphin_wireless_stop(lv_event_t *event)
{
  (void)event;
  if (g_wifi_ticket != 0)
    (void)bk7258_wifi_diagnostic_cancel(g_wifi_ticket);
  if (g_channels_ticket != 0)
    (void)bk7258_wifi_diagnostic_cancel(g_channels_ticket);
}

static void dolphin_channels(lv_event_t *event)
{
  char text[96];
  int ret = 0;
  (void)event;
  dolphin_page("CHANNEL STATS");
  g_channels_page = g_page;
  if (g_channels_ticket == 0)
    {
      ret = bk7258_wifi_channels_async(300, &g_channels_ticket);
      if (ret < 0) g_channels_ticket = 0;
    }
  if (ret < 0)
    {
      snprintf(text, sizeof(text), "Radio unavailable (%d)", ret);
      dolphin_label(g_page, text, LV_PCT(94));
      dolphin_button(g_page, "RETRY", dolphin_channels, NULL);
    }
  else
    {
      dolphin_label(g_page, "Passive sampling: channels 1-13\n300 ms per channel",
                     LV_PCT(94));
      dolphin_button(g_page, "STOP", dolphin_wireless_stop, NULL);
    }
}

static void dolphin_channels_timer(lv_timer_t *timer)
{
  struct bk7258_wifi_channel_stats_s result;
  char text[112];
  int ret;
  (void)timer;
  if (g_channels_ticket == 0) return;
  memset(&result, 0, sizeof(result));
  ret = bk7258_wifi_channels_poll(g_channels_ticket, &result);
  if (ret == -EAGAIN) return;
  g_channels_ticket = 0;
  if (g_channels_page == NULL || g_channels_page != g_page) return;
  dolphin_page("CHANNEL STATS");
  if (ret == 0) ret = result.status;
  if (ret < 0)
    {
      snprintf(text, sizeof(text), "Sampling stopped (%d)", ret);
      dolphin_label(g_page, text, LV_PCT(94));
    }
  dolphin_label(g_page, "Observed frames, not channel utilization", LV_PCT(94));
  for (uint32_t i = 0; i < result.returned &&
                       i < BK7258_WIFI_CHANNEL_STATS_MAX; i++)
    {
      snprintf(text, sizeof(text), "CH %lu: %lu frames / %lu bytes",
               (unsigned long)result.channels[i].channel,
               (unsigned long)result.channels[i].frame_count,
               (unsigned long)result.channels[i].byte_count);
      dolphin_label(g_page, text, LV_PCT(94));
    }
  dolphin_button(g_page, "START AGAIN", dolphin_channels, NULL);
  dolphin_button(g_page, "NETWORK", dolphin_network, NULL);
}

static void dolphin_wifi_scan(lv_event_t *event)
{
  char text[80];
  int ret = 0;
  (void)event;
  dolphin_page("WI-FI SCAN");
  g_wifi_page = g_page;
  if (g_wifi_ticket == 0)
    {
      ret = bk7258_wifi_scan_async(BK7258_WIFI_SCAN_DEFAULT_MS,
                                   &g_wifi_ticket);
      if (ret < 0) g_wifi_ticket = 0;
    }
  if (ret < 0)
    {
      snprintf(text, sizeof(text), "Scan unavailable (%d)", ret);
      dolphin_label(g_page, text, LV_PCT(94));
      dolphin_button(g_page, "RETRY", dolphin_wifi_scan, NULL);
    }
  else
    {
      dolphin_label(g_page, "Scanning... You can return HOME", LV_PCT(94));
      dolphin_button(g_page, "STOP", dolphin_wireless_stop, NULL);
    }
}

static void dolphin_wifi_timer(lv_timer_t *timer)
{
  struct bk7258_wifi_scan_result_s result;
  char text[112];
  int ret;
  (void)timer;
  if (g_wifi_ticket == 0) return;
  memset(&result, 0, sizeof(result));
  ret = bk7258_wifi_scan_poll(g_wifi_ticket, &result);
  if (ret == -EAGAIN) return;
  g_wifi_ticket = 0;
  /* Always consume completion, but never repaint a page the user left. */
  if (g_wifi_page == NULL || g_wifi_page != g_page) return;
  dolphin_page("WI-FI SCAN");
  if (ret == 0) ret = result.status;
  if (ret < 0)
    {
      snprintf(text, sizeof(text), "Scan failed (%d)", ret);
      dolphin_label(g_page, text, LV_PCT(94));
    }
  else
    {
      snprintf(text, sizeof(text), "Found %lu; showing strongest %lu",
               (unsigned long)result.found,
               (unsigned long)result.returned);
      dolphin_label(g_page, text, LV_PCT(94));
      for (uint32_t i = 0; i < result.returned &&
                           i < BK7258_WIFI_SCAN_MAX_RESULTS; i++)
        {
          char name[BK7258_WIFI_SSID_MAX_LEN + 1];
          memcpy(name, result.aps[i].ssid, sizeof(name));
          name[sizeof(name) - 1] = '\0';
          for (size_t j = 0; name[j] != '\0'; j++)
            if ((unsigned char)name[j] < 32 ||
                (unsigned char)name[j] > 126) name[j] = '?';
          snprintf(text, sizeof(text), "%s\nChannel %u | %ld dBm",
                   name[0] ? name : "Hidden network", result.aps[i].channel,
                   (long)result.aps[i].rssi);
          dolphin_label(g_page, text, LV_PCT(94));
        }
      if (result.found == 0)
        dolphin_label(g_page, "No networks found", LV_PCT(94));
    }
  dolphin_button(g_page, "SCAN AGAIN", dolphin_wifi_scan, NULL);
  dolphin_button(g_page, "NETWORK", dolphin_network, NULL);
}
#endif

#ifdef DOLPHIN_HAS_BLE_SCAN
static void dolphin_ble_stop(lv_event_t *event)
{
  (void)event;
  if (g_ble_owned)
    {
      int ret = bk7258_ble_scan_stop();
      g_ble_stop_sent = ret == 0;
    }
}

static void dolphin_ble(lv_event_t *event)
{
  int ret;
  (void)event;
  dolphin_page("BLE BROADCASTS");
  g_ble_page = g_page;
  dolphin_button(g_page, "STOP", dolphin_ble_stop, NULL);
  dolphin_button(g_page, "START AGAIN", dolphin_ble, NULL);
  g_ble_status = lv_label_create(g_page);
  lv_obj_set_width(g_ble_status, LV_PCT(94));
  lv_obj_set_style_text_color(g_ble_status, lv_color_hex(0xf2ead8), 0);
  g_ble_results = lv_label_create(g_page);
  lv_obj_set_width(g_ble_results, LV_PCT(94));
  lv_obj_set_style_text_color(g_ble_results, lv_color_hex(0xf2ead8), 0);
  lv_label_set_long_mode(g_ble_results, LV_LABEL_LONG_WRAP);
  ret = bk7258_ble_scan_start();
  if (ret == 0)
    {
      g_ble_owned = true;
      g_ble_stop_sent = false;
      g_ble_started = lv_tick_get();
      lv_label_set_text(g_ble_status, "Scanning for 10 seconds...");
    }
  else
    {
      char error[64];
      snprintf(error, sizeof(error), "Radio unavailable (%d); stop/retry", ret);
      lv_label_set_text(g_ble_status, error);
    }
}

static void dolphin_ble_timer(lv_timer_t *timer)
{
  struct bk7258_ble_scan_snapshot_s result;
  char text[1536];
  char status[96];
  size_t used = 0;
  (void)timer;
  if (!g_ble_owned) return;
  if (bk7258_ble_scan_poll(&result) < 0) return;
  if (result.active && !g_ble_stop_sent &&
      lv_tick_elaps(g_ble_started) >= 10000)
    dolphin_ble_stop(NULL);
  if (!result.active) g_ble_owned = false;
  if (g_ble_page == NULL || g_ble_page != g_page) return;
  snprintf(status, sizeof(status), "%s | %lu reports | error %ld",
           result.state == BK7258_BLE_SCAN_FAULTED ? "Stop failed: retry STOP" :
           result.active ? (g_ble_stop_sent ? "Stopping" : "Scanning") : "Stopped",
           (unsigned long)result.count, (long)result.last_error);
  lv_label_set_text(g_ble_status, status);
  text[0] = '\0';
  for (uint32_t i = 0; i < result.count && i < BK7258_BLE_SCAN_MAX_RESULTS; i++)
    {
      const struct bk7258_ble_scan_result_s *item = &result.results[i];
      char name[32] = "Unnamed";
      for (size_t j = 0; j < item->payload_length &&
                         j < BK7258_BLE_SCAN_MAX_PAYLOAD;)
        {
          size_t len = item->payload[j];
          if (len == 0 || j + 1 + len > item->payload_length ||
              j + 1 + len > BK7258_BLE_SCAN_MAX_PAYLOAD) break;
          if ((item->payload[j + 1] == 8 || item->payload[j + 1] == 9) && len > 1)
            {
              size_t n = len - 1;
              if (n >= sizeof(name)) n = sizeof(name) - 1;
              for (size_t k = 0; k < n; k++)
                {
                  unsigned char ch = item->payload[j + 2 + k];
                  name[k] = ch >= 32 && ch <= 126 ? ch : '?';
                }
              name[n] = '\0';
            }
          j += len + 1;
        }
      int n = snprintf(text + used, sizeof(text) - used,
                       "%s | %d dBm\n%02X:%02X:%02X:%02X:%02X:%02X\n\n",
                       name, item->rssi, item->address[5], item->address[4],
                       item->address[3], item->address[2], item->address[1],
                       item->address[0]);
      if (n < 0 || (size_t)n >= sizeof(text) - used) break;
      used += n;
    }
  lv_label_set_text(g_ble_results, used ? text : "No advertisements received");
}
#endif

static void dolphin_network(lv_event_t *event)
{
  struct ifaddrs *addresses;
  struct ifaddrs *item;
  char line[112];
  bool found = false;

  (void)event;
  dolphin_page("NETWORK");
  if (getifaddrs(&addresses) != 0)
    {
      snprintf(line, sizeof(line), "Network info unavailable: %d", errno);
      dolphin_label(g_page, line, LV_PCT(94));
    }
  else
    {
      for (item = addresses; item != NULL; item = item->ifa_next)
        {
          char address[INET6_ADDRSTRLEN] = "-";

          if (item->ifa_name == NULL)
            {
              continue;
            }

          found = true;


          if (item->ifa_addr != NULL && item->ifa_addr->sa_family == AF_INET)
            {
              struct sockaddr_in *ipv4 = (struct sockaddr_in *)item->ifa_addr;
              (void)inet_ntop(AF_INET, &ipv4->sin_addr, address, sizeof(address));
            }
          else if (item->ifa_addr != NULL && item->ifa_addr->sa_family == AF_INET6)
            {
              struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)item->ifa_addr;
              (void)inet_ntop(AF_INET6, &ipv6->sin6_addr, address, sizeof(address));
            }

          snprintf(line, sizeof(line), "%s: %s\n%s", item->ifa_name,
                   (item->ifa_flags & IFF_UP) ? "Enabled" : "Disabled", address);
          dolphin_label(g_page, line, LV_PCT(94));
        }
      freeifaddrs(addresses);
      if (!found)
        {
          dolphin_label(g_page, "No network interfaces reported", LV_PCT(94));
        }
    }
  dolphin_button(g_page, "REFRESH", dolphin_network, NULL);
#ifdef DOLPHIN_HAS_BLE_SCAN
  dolphin_button(g_page, "BLE BROADCASTS", dolphin_ble, NULL);
#endif
#ifdef CONFIG_BK7258_WIFI_VNET
  dolphin_button(g_page, "SCAN WI-FI", dolphin_wifi_scan, NULL);
  dolphin_button(g_page, "CHANNEL STATS", dolphin_channels, NULL);
#endif
}

static void dolphin_device(lv_event_t *event)
{
  struct utsname system;
  char text[144];
  int32_t width = lv_display_get_horizontal_resolution(g_display);
  int32_t height = lv_display_get_vertical_resolution(g_display);

  (void)event;
  dolphin_page("DEVICE");
  if (uname(&system) == 0)
    {
      snprintf(text, sizeof(text), "%.40s %.40s\nDisplay: %ld x %ld", system.sysname,
               system.release, (long)width, (long)height);
    }
  else
    {
      snprintf(text, sizeof(text), "System info unavailable: %d\nDisplay: %ld x %ld",
               errno, (long)width, (long)height);
    }
  dolphin_label(g_page, text, LV_PCT(94));
}

static void dolphin_home(lv_event_t *event)
{
  (void)event;
  pthread_mutex_lock(&g_scan.lock);
  if (!g_scan.running)
    {
      g_scan.relative[0] = '\0';
    }
  pthread_mutex_unlock(&g_scan.lock);
  dolphin_page("DOLPHIN");
  dolphin_home_art(g_page);
  dolphin_feature_card(g_page, "FILES", "Browse TF storage read-only",
                       dolphin_files);
  dolphin_feature_card(g_page, "NETWORK", "Show live interface addresses",
                       dolphin_network);
  dolphin_feature_card(g_page, "DEVICE", "System and display information",
                       dolphin_device);
}

static void dolphin_scan_timer(lv_timer_t *timer)
{
  unsigned int index;
  int error;
  bool more;
  bool truncated;
  char line[80];
  char relative[DOLPHIN_PATH_SIZE];
  char location[DOLPHIN_PATH_SIZE + 16];

  (void)timer;
  pthread_mutex_lock(&g_scan.lock);
  if (!g_scan.complete)
    {
      pthread_mutex_unlock(&g_scan.lock);
      return;
    }

  error = g_scan.error;
  more = g_scan.more;
  truncated = g_scan.truncated;
  snprintf(relative, sizeof(relative), "%s", g_scan.relative);
  g_scan.complete = false;
  pthread_mutex_unlock(&g_scan.lock);
  if (g_page == NULL || g_page != g_files_page)
    {
      return;
    }

  lv_obj_clean(g_page);
  snprintf(location, sizeof(location), "Read-only: /%s", relative);
  dolphin_label(g_page, location, LV_PCT(94));
  if (relative[0] != '\0')
    {
      dolphin_button(g_page, "UP", dolphin_files_up, NULL);
    }
  if (error < 0)
    {
      syslog(LOG_WARNING, "dolphin-ui: storage read failed: %d\n", error);
      snprintf(line, sizeof(line), "%s",
               error == -ENOENT ? "TF card is not ready" :
                                 "Cannot read this folder");
      dolphin_label(g_page, line, LV_PCT(94));
    }
  else
    {
      pthread_mutex_lock(&g_scan.lock);
      for (index = 0; index < g_scan.count; index++)
        {
          snprintf(line, sizeof(line), "%s%s", g_scan.files[index].name,
                   g_scan.files[index].directory ? "/" : "");
          dolphin_button(g_page, line, dolphin_open_directory, &g_scan.files[index]);
        }
      pthread_mutex_unlock(&g_scan.lock);
      if (index == 0)
        {
          dolphin_label(g_page, "No entries", LV_PCT(94));
        }
      if (more)
        {
          dolphin_label(g_page, "Showing first 64 entries", LV_PCT(94));
        }
      if (truncated)
        {
          dolphin_label(g_page, "Directory truncated after 256 entries",
                        LV_PCT(94));
        }
    }
  dolphin_button(g_page, error < 0 ? "RETRY" : "REFRESH", dolphin_files, NULL);
}

static void dolphin_preview_timer(lv_timer_t *timer)
{
  char name[DOLPHIN_NAME_SIZE];
  char text[DOLPHIN_PREVIEW_BYTES + 1];
  off_t size;
  size_t length;
  unsigned int request;
  unsigned int current_request;
  int error;
  bool truncated;
  char line[96];

  (void)timer;
  pthread_mutex_lock(&g_preview.lock);
  if (!g_preview.complete)
    {
      pthread_mutex_unlock(&g_preview.lock);
      return;
    }

  request = g_preview.completed_request;
  current_request = g_preview.request;
  memcpy(name, g_preview.name, sizeof(name));
  memcpy(text, g_preview.text, sizeof(text));
  size = g_preview.size;
  length = g_preview.length;
  error = g_preview.error;
  truncated = g_preview.truncated;
  g_preview.complete = false;
  pthread_mutex_unlock(&g_preview.lock);
  if (g_page == NULL || g_page != g_preview_page || request != current_request)
    {
      return;
    }

  lv_obj_clean(g_page);
  snprintf(line, sizeof(line), "%s\nSize: %lld bytes", name, (long long)size);
  dolphin_label(g_page, line, LV_PCT(94));
  if (error == -EILSEQ)
    {
      dolphin_label(g_page, "Preview unavailable: non-text data", LV_PCT(94));
    }
  else if (error == -ENOTSUP)
    {
      dolphin_label(g_page, "Preview unavailable: ASCII text only", LV_PCT(94));
    }
  else if (error < 0)
    {
      snprintf(line, sizeof(line), "Preview unavailable: %d", -error);
      dolphin_label(g_page, line, LV_PCT(94));
    }
  else if (length == 0)
    {
      dolphin_label(g_page, "Empty text file", LV_PCT(94));
    }
  else
    {
      dolphin_label(g_page, text, LV_PCT(94));
      lv_obj_set_style_text_align(lv_obj_get_child(g_page, -1),
                                  LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
      if (truncated)
        {
          dolphin_label(g_page, "Preview truncated at 2048 bytes", LV_PCT(94));
        }
    }
  dolphin_button(g_page, "BACK", dolphin_files, NULL);
}

static int dolphin_ui_task(int argc, FAR char *argv[])
{
  lv_nuttx_dsc_t descriptor;
  lv_nuttx_result_t result = {0};
#ifdef CONFIG_BK7258_LVGL_FB_ACCEL
  lv_display_t *accelerated;
#endif

  (void)argc;
  (void)argv;
  if (lv_is_initialized())
    {
      syslog(LOG_ERR, "dolphin-ui: LVGL already has an owner\n");
      g_ui_started = false;
      return -EBUSY;
    }

  lv_init();
  lv_nuttx_dsc_init(&descriptor);
  descriptor.fb_path = "/dev/fb0";
  descriptor.input_path = "/dev/input0";
#ifdef CONFIG_BK7258_LVGL_FB_ACCEL
  accelerated = bk7258_lvgl_fb_create(descriptor.fb_path);
  if (accelerated != NULL)
    {
      descriptor.fb_path = NULL;
    }
#endif
  lv_nuttx_init(&descriptor, &result);
#ifdef CONFIG_BK7258_LVGL_FB_ACCEL
  if (accelerated != NULL)
    {
      result.disp = accelerated;
      if (result.indev != NULL &&
          bk7258_lvgl_fb_bind_touch(accelerated, result.indev) < 0)
        {
          lv_display_delete(accelerated);
          result.disp = NULL;
        }
    }
  syslog(LOG_INFO, "dolphin-ui: render=%s\n",
         accelerated != NULL ? "sram-dma2d" : "psram-direct-fallback");
#endif
  if (result.disp == NULL || result.indev == NULL)
    {
      syslog(LOG_ERR, "dolphin-ui: LVGL display or touch initialization failed\n");
      lv_nuttx_deinit(&result);
      lv_deinit();
      g_ui_started = false;
      return -ENODEV;
    }

  g_display = result.disp;
  dolphin_home(NULL);
  syslog(LOG_INFO, "dolphin-ui: display and touch initialized\n");
  lv_timer_create(dolphin_scan_timer, 100, NULL);
  lv_timer_create(dolphin_preview_timer, 100, NULL);
#ifdef DOLPHIN_HAS_BLE_SCAN
  lv_timer_create(dolphin_ble_timer, 300, NULL);
#endif
#ifdef CONFIG_BK7258_WIFI_VNET
  lv_timer_create(dolphin_wifi_timer, 100, NULL);
  lv_timer_create(dolphin_channels_timer, 100, NULL);
#endif
  for (;;)
    {
      uint32_t delay = lv_timer_handler();
      usleep((delay > 20 ? 20 : (delay == 0 ? 1 : delay)) * 1000);
    }
}

int dolphin_ui_start(void)
{
  pid_t pid;

  if (g_ui_started)
    {
      return -EALREADY;
    }

  g_ui_started = true;
  pid = task_create("dolphin-ui", DOLPHIN_UI_PRIORITY, DOLPHIN_UI_STACKSIZE,
                    dolphin_ui_task, NULL);
  if (pid < 0)
    {
      g_ui_started = false;
      syslog(LOG_ERR, "dolphin-ui: task creation failed: %d\n", (int)pid);
      return (int)pid;
    }

  syslog(LOG_INFO, "dolphin-ui: task started\n");
  return OK;
}
