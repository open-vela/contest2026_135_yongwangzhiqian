/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_control_session.h"
#include <errno.h>
#include <string.h>

static void wipe(void *data, size_t size)
{
  volatile uint8_t *p = data;
  while (size-- != 0) *p++ = 0;
}
static uint32_t get32(const uint8_t *p)
{ return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static void put32(uint8_t *p, uint32_t n)
{ p[0]=n>>24; p[1]=n>>16; p[2]=n>>8; p[3]=n; }

static void status_unknown(struct bkcontrol_status_s *status)
{
  memset(status, 0xff, sizeof(*status));
  status->flags = 0;
  status->error = 0;
}

void bkcontrol_session_close(struct bkcontrol_session_s *s)
{ if (s != NULL) wipe(s, sizeof(*s)); }

int bkcontrol_session_open(struct bkcontrol_session_s *s,
                           const uint8_t key[32], bkcontrol_execute_t execute,
                           void *context)
{
  uint8_t bits = 0;
  if (s == NULL || key == NULL || execute == NULL) return -EINVAL;
  if (s->open) return -EBUSY;
  for (size_t i = 0; i < 32; i++) bits |= key[i];
  if (bits == 0) return -EINVAL;
  memset(s, 0, sizeof(*s));
  memcpy(s->secret, key, 32);
  s->execute = execute;
  s->context = context;
  s->open = true;
  return 0;
}
int bkcontrol_session_set_ota_handler(struct bkcontrol_session_s *s, bkcontrol_ota_t ota)
{ if (!s || !s->open || s->authenticated) return -EINVAL; s->ota=ota; return 0; }

int bkcontrol_session_packet(struct bkcontrol_session_s *s, const uint8_t *p,
                             size_t size, uint8_t response[BKCONTROL_RESPONSE_SIZE])
{
  struct bkcontrol_status_s status;
  uint32_t command, payload, argument = 0;
  int ret = -EPROTO;
  status_unknown(&status);
  if (response != NULL) memset(response, 0, BKCONTROL_RESPONSE_SIZE);
  if (s == NULL || !s->open) return -ENOTCONN;
  if (p == NULL || response == NULL || size < 16 || size > BKCONTROL_REQUEST_MAX)
    goto fail;
  command = get32(p+4);
  payload = get32(p+12);
  if (memcmp(p, "SDC1", 4) || get32(p+8) != s->sequence ||
      s->sequence == UINT32_MAX || payload != size - 16) goto fail;
  if (!s->authenticated)
    {
      uint8_t difference = 0;
      if (command != BKCONTROL_AUTH || payload != 32) goto fail;
      for (size_t i = 0; i < 32; i++) difference |= s->secret[i] ^ p[16+i];
      wipe(s->secret, sizeof(s->secret));
      if (difference != 0) { ret = -EACCES; goto fail; }
      s->authenticated = true;
      ret = 0;
    }
  else
    {
      if (command < BKCONTROL_STATUS || command > BKCONTROL_OTA_CANCEL) goto fail;
      if (command >= BKCONTROL_OTA_BEGIN)
        {
          if (command == BKCONTROL_OTA_BEGIN)
            {
              if (payload != 4) goto fail;
              argument=get32(p+16);
              if (argument < 44 || argument > sizeof(s->ota_record)) goto fail;
              if (s->ota == NULL) { ret = -ENOTSUP; goto ota_done; }
              if (s->ota_total != 0) { ret = -EBUSY; goto ota_done; }
              s->ota_total=argument; s->ota_received=0; ret=0;
            }
          else if (command == BKCONTROL_OTA_APPEND)
            {
              if (payload == 0 || payload > 32 || s->ota_total == 0 ||
                  s->ota_received > s->ota_total ||
                  payload > s->ota_total-s->ota_received) goto fail;
              memcpy(s->ota_record+s->ota_received,p+16,payload); s->ota_received+=payload; ret=0;
            }
          else if (command == BKCONTROL_OTA_START)
            {
              if (payload != 0 || s->ota == NULL || s->ota_total == 0 || s->ota_received != s->ota_total) goto fail;
              ret=s->ota(s->context,(enum bkcontrol_command_e)command,s->ota_record,s->ota_total,&status);
              if (!ret) { wipe(s->ota_record,sizeof(s->ota_record));s->ota_total=s->ota_received=0; }
            }
          else if (command == BKCONTROL_OTA_STATUS || command == BKCONTROL_OTA_CANCEL)
            {
              if (payload != 0) goto fail;
              if (s->ota == NULL) { ret = -ENOTSUP; goto ota_done; }
              ret=s->ota(s->context,(enum bkcontrol_command_e)command,NULL,0,&status);
              if (command == BKCONTROL_OTA_CANCEL && !ret) { wipe(s->ota_record,sizeof(s->ota_record));s->ota_total=s->ota_received=0; }
            }
          else goto fail;
ota_done:  if (ret > 0) ret=-EIO;
        }
      else
        {
      if (command == BKCONTROL_VOLUME || command == BKCONTROL_PERSONA || command == BKCONTROL_MEMORY_SET)
        {
          if (payload != 4) goto fail;
          argument = get32(p+16);
          if (argument > (command == BKCONTROL_VOLUME ? 100u : command == BKCONTROL_PERSONA ? 4u : 1u)) goto fail;
        }
      else if (payload != 0) goto fail;
      ret = s->execute(s->context, (enum bkcontrol_command_e)command, argument, &status);
      if (ret > 0) ret = -EIO;
      if (ret < 0)
        status_unknown(&status);
        }
    }
  memcpy(response, "SDC1", 4);
  put32(response+4, command | 0x80000000u);
  put32(response+8, s->sequence++);
  put32(response+12, 24);
  put32(response+16, (uint32_t)ret);
  if (command >= BKCONTROL_OTA_BEGIN)
    {
      put32(response+20, status.ota.state);
      put32(response+24, status.ota.phase);
      put32(response+28, status.ota.progress);
      put32(response+32, status.ota.total);
      put32(response+36, (uint32_t)status.ota.result);
    }
  else if (command == BKCONTROL_INFO)
    {
      put32(response+20, status.device_info.major);
      put32(response+24, status.device_info.minor);
      put32(response+28, status.device_info.revision);
      put32(response+32, status.device_info.build);
      put32(response+36, status.device_info.security_counter);
    }
  else
    {
      put32(response+20, status.flags);
      put32(response+24, status.volume);
      put32(response+28, status.persona);
      put32(response+32, status.turn);
      put32(response+36, (uint32_t)status.error);
    }
  return 0;
fail:
  bkcontrol_session_close(s);
  return ret;
}
