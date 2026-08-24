from __future__ import annotations

import unittest
from unittest import mock

from server import LeaderboardRequestHandler, TicketReplayGuard, TokenBucketRateLimiter


class VerifierAbuseControlTests(unittest.TestCase):
    def test_ticket_can_only_be_consumed_once_within_window(self) -> None:
        guard = TicketReplayGuard(60)
        self.assertTrue(guard.consume("aabb"))
        self.assertFalse(guard.consume("aabb"))

    def test_rate_limiter_refills_continuously(self) -> None:
        limiter = TokenBucketRateLimiter(2, 10)
        with mock.patch("server.time.monotonic", side_effect=[0.0, 0.0, 0.0, 5.0]):
            self.assertEqual(limiter.consume("player"), (True, 0))
            self.assertEqual(limiter.consume("player"), (True, 0))
            allowed, retry_after = limiter.consume("player")
            self.assertFalse(allowed)
            self.assertEqual(retry_after, 5)
            self.assertEqual(limiter.consume("player"), (True, 0))

    def test_proxy_source_is_used_only_for_loopback_peer(self) -> None:
        handler = object.__new__(LeaderboardRequestHandler)
        handler.client_address = ("127.0.0.1", 1234)
        handler.headers = {"CF-Connecting-IP": "203.0.113.7"}
        self.assertEqual(handler._network_source(), "203.0.113.7")

        handler.client_address = ("198.51.100.9", 1234)
        self.assertEqual(handler._network_source(), "198.51.100.9")

    def test_invalid_proxy_source_falls_back_to_loopback_peer(self) -> None:
        handler = object.__new__(LeaderboardRequestHandler)
        handler.client_address = ("::1", 1234)
        handler.headers = {"CF-Connecting-IP": "not-an-address"}
        self.assertEqual(handler._network_source(), "::1")


if __name__ == "__main__":
    unittest.main()
