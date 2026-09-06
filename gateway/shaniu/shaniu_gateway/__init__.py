# SPDX-License-Identifier: Apache-2.0
"""Shaniu's minimal companion-v1 Gateway package."""

from .protocol import Frame, GatewaySession, MessageType, ProtocolError
from .server import GatewayConfig, GatewayServer

__all__ = [
    "Frame",
    "GatewayConfig",
    "GatewayServer",
    "GatewaySession",
    "MessageType",
    "ProtocolError",
]
