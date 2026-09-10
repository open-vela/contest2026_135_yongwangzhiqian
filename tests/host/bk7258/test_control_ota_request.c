/* SPDX-License-Identifier: Apache-2.0 */

#include "bk7258_control_ota_request.h"

#include <assert.h>
#include <errno.h>
#include <string.h>

#define HEADER_SIZE 44u

static size_t make_record(uint8_t *record, const char *url)
{
  static const char ca_pem[] =
    "-----BEGIN CERTIFICATE-----\n"
    "X\n"
    "-----END CERTIFICATE-----\n";
  size_t url_length = strlen(url);
  size_t ca_length = strlen(ca_pem);

  assert(url_length <= BKCONTROL_OTA_URL_MAX);
  memset(record, 0, BKCONTROL_OTA_RECORD_MAX);
  memcpy(record, "SOU1", 4u);
  record[4] = (uint8_t)(url_length >> 8);
  record[5] = (uint8_t)url_length;
  record[6] = (uint8_t)(ca_length >> 8);
  record[7] = (uint8_t)ca_length;
  record[8] = 192u;
  record[9] = 168u;
  record[10] = 1u;
  record[11] = 2u;
  record[12] = 1u;
  memcpy(record + HEADER_SIZE, url, url_length);
  memcpy(record + HEADER_SIZE + url_length, ca_pem, ca_length);
  return HEADER_SIZE + url_length + ca_length;
}

static void expect_invalid(uint8_t *record, size_t size)
{
  struct bkcontrol_ota_request_s request;

  memset(&request, 0xa5, sizeof(request));
  assert(bkcontrol_ota_request_parse(record, size, &request) == -EINVAL);
  assert(request.url[0] == '\0');
  assert(request.ca_pem[0] == '\0');
  assert(request.catalog_sha256[0] == 0);
}

int main(void)
{
  uint8_t record[BKCONTROL_OTA_RECORD_MAX];
  struct bkcontrol_ota_request_s request;
  char long_url[8u + 64u + sizeof("/catalog.json")];
  size_t size;

  size = make_record(record,
                     "https://ota.local:443/firmware/v1/abc/catalog.json");
  assert(bkcontrol_ota_request_parse(record, size, &request) == 0);
  assert(strcmp(request.url,
                "https://ota.local:443/firmware/v1/abc/catalog.json") == 0);
  assert(request.ipv4[0] == 192u);

  size = make_record(record, "https://ota.local/catalog.json");
  expect_invalid(record, size - 1u);
  expect_invalid(record, size + 1u);

  size = make_record(record, "http://ota.local/catalog.json");
  expect_invalid(record, size);
  size = make_record(record, "https://ota.local/catalog.jsonx");
  expect_invalid(record, size);
  size = make_record(record, "https://ota.local/other.json");
  expect_invalid(record, size);

  size = make_record(record, "https://-bad.local/catalog.json");
  expect_invalid(record, size);
  size = make_record(record, "https://bad-.local/catalog.json");
  expect_invalid(record, size);
  size = make_record(record, "https://good.-bad.local/catalog.json");
  expect_invalid(record, size);
  memcpy(long_url, "https://", 8u);
  memset(long_url + 8u, 'a', 64u);
  memcpy(long_url + 8u + 64u, "/catalog.json", sizeof("/catalog.json"));
  size = make_record(record, long_url);
  expect_invalid(record, size);

  size = make_record(record, "https://ota.local:65536/catalog.json");
  expect_invalid(record, size);
  size = make_record(record, "https://ota.local:0/catalog.json");
  expect_invalid(record, size);

  size = make_record(record, "https://ota.local/catalog.json");
  record[HEADER_SIZE + 3u] = '\t';
  expect_invalid(record, size);
  size = make_record(record, "https://ota.local/catalog.json");
  record[HEADER_SIZE + 3u] = 0x80u;
  expect_invalid(record, size);

  size = make_record(record, "https://ota.local/catalog.json");
  memset(record + 12u, 0, 32u);
  expect_invalid(record, size);
  size = make_record(record, "https://ota.local/catalog.json");
  record[HEADER_SIZE + strlen("https://ota.local/catalog.json") + 1u] = '\0';
  expect_invalid(record, size);

  return 0;
}
