/* SPDX-License-Identifier: Apache-2.0 */
/* Host shim for the optional OTA-over-RPMsg service.
 *
 * The RPTUN core includes the public header unconditionally, while the
 * service API is used only when CONFIG_BK7258_OTA_RPMSG is enabled.  This
 * fixture normally leaves that option disabled so the transport unit tests
 * do not acquire OTA manager/source dependencies.  Focused OTA tests opt in
 * and use the public declarations while providing their own transport stub.
 */

#ifndef __TESTS_BK7258_OTA_RPMSG_H
#define __TESTS_BK7258_OTA_RPMSG_H

#ifdef CONFIG_BK7258_OTA_RPMSG
#  include <bk7258_ota_rpmsg.h>
#endif

#endif /* __TESTS_BK7258_OTA_RPMSG_H */
