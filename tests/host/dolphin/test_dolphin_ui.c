/* SPDX-License-Identifier: Apache-2.0 */
/* Real LVGL software rendering of the production UI. NuttX device/task
 * adapters are not exercised here; they must pass target acceptance.
 */
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <lvgl/lvgl.h>

static char storage_root[192];
#define CONFIG_DOLPHIN_STORAGE_ROOT storage_root
#define FAR
#define OK 0
typedef struct { const char *fb_path; const char *input_path; } lv_nuttx_dsc_t;
typedef struct { lv_display_t *disp; lv_indev_t *indev; } lv_nuttx_result_t;
static void lv_nuttx_dsc_init(lv_nuttx_dsc_t *d) { memset(d, 0, sizeof(*d)); }
static void lv_nuttx_init(lv_nuttx_dsc_t *d, lv_nuttx_result_t *r)
{ (void)d; (void)r; assert(!"host test must not initialize NuttX devices"); }
static void lv_nuttx_deinit(lv_nuttx_result_t *r) { (void)r; }
static pid_t task_create(const char *n, int p, int s,
                         int (*entry)(int, char **), char *const argv[])
{ (void)n; (void)p; (void)s; (void)entry; (void)argv; return -ENOSYS; }

#define CONFIG_BK7258_WIFI_VNET 1
#define CONFIG_BK7258_AP_CORE 1
#include <arch/chip/bk7258_wifi.h>
static int wifi_submit_error, wifi_poll_error = -EAGAIN;
static struct bk7258_wifi_scan_result_s wifi_result;
int bk7258_wifi_scan_async(uint32_t timeout, uint32_t *ticket)
{ assert(timeout == BK7258_WIFI_SCAN_DEFAULT_MS);
  if (wifi_submit_error) return wifi_submit_error;
  *ticket = 1; return 0; }
int bk7258_wifi_scan_poll(uint32_t ticket,
                          struct bk7258_wifi_scan_result_s *result)
{ assert(ticket == 1); *result = wifi_result; return wifi_poll_error; }

static int diagnostic_cancels;
int bk7258_wifi_diagnostic_cancel(uint32_t ticket)
{ assert(ticket); diagnostic_cancels++; return 0; }
int bk7258_wifi_channels_async(uint32_t dwell, uint32_t *ticket)
{ assert(dwell == 300); *ticket = 2; return 0; }
int bk7258_wifi_channels_poll(uint32_t ticket,
                              struct bk7258_wifi_channel_stats_s *result)
{ assert(ticket == 2); memset(result, 0, sizeof(*result));
  result->returned = 1; result->channels[0].channel = 1;
  result->channels[0].frame_count = 12; result->channels[0].byte_count = 900;
  return 0; }

#define CONFIG_BK7258_BT_IPC 1
#define CONFIG_WIRELESS_BLUETOOTH_HOST 1
#include <arch/chip/bk7258_ble_scan.h>
static struct bk7258_ble_scan_snapshot_s ble_snapshot;
int bk7258_ble_scan_start(void)
{ if (ble_snapshot.active) return -EBUSY;
  memset(&ble_snapshot, 0, sizeof(ble_snapshot));
  ble_snapshot.active=1;ble_snapshot.state=BK7258_BLE_SCAN_ACTIVE;return 0; }
int bk7258_ble_scan_stop(void)
{ ble_snapshot.active=0;ble_snapshot.state=BK7258_BLE_SCAN_IDLE;return 0; }
int bk7258_ble_scan_poll(struct bk7258_ble_scan_snapshot_s *s)
{*s=ble_snapshot;return 0;}

#include "../../../app/dolphin/dolphin_ui.c"

static uint32_t pixels[480 * 480];
static uint32_t frame[480 * 480];
static int width;
static int height;

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *data)
{
  uint32_t *src = (uint32_t *)data;
  for (int y = area->y1; y <= area->y2; y++)
    for (int x = area->x1; x <= area->x2; x++)
      frame[y * width + x] = *src++;
  lv_display_flush_ready(display);
}

static void screenshot(const char *path)
{
  FILE *f;
  lv_obj_update_layout(lv_screen_active());
  lv_refr_now(g_display);
  f = fopen(path, "wb");
  assert(f);
  fprintf(f, "P6\n%d %d\n255\n", width, height);
  for (int i = 0; i < width * height; i++)
    {
      uint8_t rgb[] = { frame[i] >> 16, frame[i] >> 8, frame[i] };
      assert(fwrite(rgb, 1, 3, f) == 3);
    }
  fclose(f);
}

static lv_obj_t *find_text(lv_obj_t *obj, const char *text)
{
  if (lv_obj_check_type(obj, &lv_label_class) &&
      strcmp(lv_label_get_text(obj), text) == 0) return obj;
  for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++)
    {
      lv_obj_t *found = find_text(lv_obj_get_child(obj, i), text);
      if (found) return found;
    }
  return NULL;
}

static void assert_label_widths(lv_obj_t *obj)
{
  if (lv_obj_check_type(obj, &lv_label_class))
    {
      lv_area_t area;
      lv_obj_get_coords(obj, &area);
      assert(area.x1 >= 0 && area.x2 < width);
    }
  for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++)
    assert_label_widths(lv_obj_get_child(obj, i));
}

static void click(const char *text)
{
  lv_obj_t *label = find_text(lv_screen_active(), text);
  assert(label);
  assert(lv_obj_check_type(lv_obj_get_parent(label), &lv_button_class));
  lv_obj_send_event(lv_obj_get_parent(label), LV_EVENT_CLICKED, NULL);
}

static void await_scan(void)
{
  for (int i = 0; i < 2000; i++)
    {
      bool busy;
      pthread_mutex_lock(&g_scan.lock);
      busy = g_scan.running;
      pthread_mutex_unlock(&g_scan.lock);
      if (!busy) { dolphin_scan_timer(NULL); return; }
      usleep(1000);
    }
  assert(!"file scan did not finish");
}

static void await_preview(void)
{
  for (int i = 0; i < 2000; i++)
    {
      bool busy;
      pthread_mutex_lock(&g_preview.lock);
      busy = g_preview.running;
      pthread_mutex_unlock(&g_preview.lock);
      if (!busy) { dolphin_preview_timer(NULL); return; }
      usleep(1000);
    }
  assert(!"file preview did not finish");
}

int main(int argc, char **argv)
{
  char base[] = "/tmp/dolphin-ui-XXXXXX";
  char path[256];
  char shot[512];
  FILE *file;
  assert(argc == 2);
  assert(mkdtemp(base));
  snprintf(storage_root, sizeof(storage_root), "%s/card", base);
  lv_init();
  width = 320;
  height = 480;
  g_display = lv_display_create(width, height);
  lv_display_set_buffers(g_display, pixels, NULL, width * height * 4,
                         LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(g_display, flush);
  dolphin_home(NULL);
  snprintf(shot, sizeof(shot), "%s/home-portrait.ppm", argv[1]);
  screenshot(shot);
  assert_label_widths(lv_screen_active());
  click("FILES");
  await_scan();
  assert(g_scan.error == -ENOENT);
  assert(find_text(lv_screen_active(), "RETRY"));
  snprintf(shot, sizeof(shot), "%s/storage-missing.ppm", argv[1]);
  screenshot(shot);
  assert(mkdir(storage_root, 0700) == 0);
  snprintf(path, sizeof(path), "%s/docs", storage_root);
  assert(mkdir(path, 0700) == 0);
  snprintf(path, sizeof(path), "%s/readme.txt", storage_root);
  file = fopen(path, "w"); assert(file); fputs("fixture", file); fclose(file);
  snprintf(path, sizeof(path), "%s/link", storage_root);
  assert(symlink("/", path) == 0);
  click("RETRY"); await_scan();
  assert(g_scan.error == 0 && g_scan.count == 2);
  assert(!find_text(lv_screen_active(), "link/"));
  snprintf(shot, sizeof(shot), "%s/files.ppm", argv[1]); screenshot(shot);
  click("docs/"); await_scan(); assert(strcmp(g_scan.relative, "docs") == 0);
  click("UP"); await_scan(); assert(g_scan.relative[0] == '\0');
  click("readme.txt");
  assert(find_text(lv_screen_active(), "FILE DETAIL"));
  await_preview();
  assert(find_text(lv_screen_active(), "fixture"));
  snprintf(shot, sizeof(shot), "%s/text-preview.ppm", argv[1]); screenshot(shot);
  click("BACK"); await_scan();
  click("readme.txt");
  click("HOME"); await_preview();
  assert(find_text(lv_screen_active(), "DOLPHIN"));
  assert(!find_text(lv_screen_active(), "fixture"));
  click("NETWORK");
  assert(find_text(lv_screen_active(), "NETWORK"));
  click("REFRESH");
  assert(find_text(lv_screen_active(), "NETWORK"));
  click("SCAN WI-FI");
  dolphin_wifi_timer(NULL);
  assert(g_wifi_ticket == 1);
  click("HOME");
  wifi_poll_error = 0;
  dolphin_wifi_timer(NULL);
  assert(g_wifi_ticket == 0 && find_text(lv_screen_active(), "DOLPHIN"));
  click("NETWORK"); click("SCAN WI-FI");
  wifi_result.found = 7; wifi_result.returned = 1;
  strcpy(wifi_result.aps[0].ssid, "test network");
  wifi_result.aps[0].rssi = -45; wifi_result.aps[0].channel = 6;
  dolphin_wifi_timer(NULL);
  assert(find_text(lv_screen_active(), "Found 7; showing strongest 1"));
  assert(find_text(lv_screen_active(), "test network\nChannel 6 | -45 dBm"));
  click("SCAN AGAIN"); wifi_result.status = -ETIMEDOUT;
  dolphin_wifi_timer(NULL);
  assert(find_text(lv_screen_active(), "Scan failed (-110)"));
  wifi_submit_error = -EBUSY; click("SCAN AGAIN");
  assert(g_wifi_ticket == 0);
  assert(find_text(lv_screen_active(), "RETRY"));
  click("HOME"); click("NETWORK"); click("CHANNEL STATS");
  click("STOP"); assert(diagnostic_cancels > 0);
  dolphin_channels_timer(NULL);
  assert(find_text(lv_screen_active(), "CH 1: 12 frames / 900 bytes"));
  click("START AGAIN"); click("HOME"); dolphin_channels_timer(NULL);
  assert(find_text(lv_screen_active(), "DOLPHIN"));
  click("HOME"); click("NETWORK"); click("BLE BROADCASTS");
  ble_snapshot.count=1;ble_snapshot.results[0].rssi=-42;
  ble_snapshot.results[0].payload_length=4;
  memcpy(ble_snapshot.results[0].payload, "\3\11Hi", 4);
  dolphin_ble_timer(NULL);
  assert(strstr(lv_label_get_text(g_ble_results), "Hi | -42 dBm"));
  click("STOP"); dolphin_ble_timer(NULL); assert(!g_ble_owned);
  click("START AGAIN"); click("HOME"); dolphin_ble_timer(NULL);
  assert(!g_ble_owned && find_text(lv_screen_active(), "DOLPHIN"));
  click("HOME"); click("DEVICE");
  assert(find_text(lv_screen_active(), "DEVICE"));
  click("HOME");
  width = 480; height = 320;
  lv_display_set_resolution(g_display, width, height);
  dolphin_home(NULL);
  snprintf(shot, sizeof(shot), "%s/home-landscape.ppm", argv[1]); screenshot(shot);
  for (int i = 0; i < 70; i++)
    {
      snprintf(path, sizeof(path), "%s/f%02d", storage_root, i);
      file = fopen(path, "w"); assert(file); fclose(file);
    }
  click("FILES"); await_scan();
  assert(g_scan.count == DOLPHIN_MAX_FILES && g_scan.more);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_t *home = find_text(lv_screen_active(), "HOME");
  lv_area_t area; lv_obj_get_coords(lv_obj_get_parent(home), &area);
  assert(area.y1 >= 0 && area.y2 < 60);
  click("HOME"); assert(find_text(lv_screen_active(), "DOLPHIN"));
  lv_display_delete(g_display); lv_deinit();
  for (int i = 0; i < 70; i++)
    { snprintf(path, sizeof(path), "%s/f%02d", storage_root, i); unlink(path); }
  snprintf(path, sizeof(path), "%s/link", storage_root); unlink(path);
  snprintf(path, sizeof(path), "%s/readme.txt", storage_root); unlink(path);
  snprintf(path, sizeof(path), "%s/docs", storage_root); rmdir(path);
  rmdir(storage_root); rmdir(base);
  puts("DOLPHIN_UI_HOST_PASS: real LVGL; host filesystem/network; no board proof");
  return 0;
}
