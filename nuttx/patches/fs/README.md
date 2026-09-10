# FAT allocation errors

Baseline: OpenVela NuttX `76354c637858ecb0aa4601629327acb6f44a26bb`.
License: Apache-2.0, inherited from the patched sources.

`0001-preserve-fat-allocation-errors.patch` returns ENOSPC when cluster
allocation reports no free clusters and preserves negative transport errors.
Invalid existing chains remain EIO; they must not be misreported as full.
Both allocation branches in fat_get_sectors are covered. Apply only in the
isolated NuttX validation tree; the official checkout remains untouched.
