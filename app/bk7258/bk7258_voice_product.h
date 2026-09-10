/****************************************************************************
 * app/bk7258/bk7258_voice_product.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BKVoice product identity.  This is App policy: it must not leak into the
 * BK7258 chip or AIDK physical-board layers.
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_VOICE_PRODUCT_H
#define __APP_BK7258_BK7258_VOICE_PRODUCT_H

/* Keep the stable machine identity ASCII-only.  The display name is encoded
 * explicitly as UTF-8 so its bytes do not depend on the host compiler locale.
 */

#define BKVOICE_PRODUCT_PERSONA_ID       "shaniu"
#define BKVOICE_PRODUCT_DISPLAY_NAME     "\xe5\x82\xbb\xe5\xa6\x9e"
#define BKVOICE_PRODUCT_ROLE             "ai-companion"
#define BKVOICE_PRODUCT_RELATIONSHIP     "fictional-ex-girlfriend"
#define BKVOICE_PRODUCT_DISCLOSURE       "synthetic-ai"

_Static_assert(sizeof(BKVOICE_PRODUCT_DISPLAY_NAME) == 7,
               "BKVoice display name must be two UTF-8 Han characters");

#endif /* __APP_BK7258_BK7258_VOICE_PRODUCT_H */
