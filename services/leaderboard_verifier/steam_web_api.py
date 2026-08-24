from __future__ import annotations

import json
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from typing import Any


class SteamWebApiError(RuntimeError):
    pass


@dataclass(frozen=True)
class SteamWebApi:
    publisher_key: str
    app_id: int
    base_url: str = "https://partner.steam-api.com"
    timeout_seconds: float = 15.0

    def _request_json(self, path: str, parameters: dict[str, Any], post: bool) -> dict[str, Any]:
        fields = {"key": self.publisher_key, **parameters}
        encoded = urllib.parse.urlencode(fields).encode("ascii")
        url = self.base_url.rstrip("/") + path
        request = urllib.request.Request(
            url if post else url + "?" + encoded.decode("ascii"),
            data=encoded if post else None,
            method="POST" if post else "GET",
            headers={
                "Accept": "application/json",
                "Content-Type": "application/x-www-form-urlencoded",
                "User-Agent": "MaxX-Throttle-Leaderboard-Verifier/1",
            },
        )
        try:
            with urllib.request.urlopen(request, timeout=self.timeout_seconds) as response:
                payload = response.read(2 * 1024 * 1024)
        except (urllib.error.HTTPError, urllib.error.URLError, TimeoutError) as exc:
            raise SteamWebApiError(f"Steam Web API request failed: {exc}") from exc
        try:
            parsed = json.loads(payload)
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise SteamWebApiError("Steam Web API returned invalid JSON") from exc
        if not isinstance(parsed, dict):
            raise SteamWebApiError("Steam Web API returned a non-object response")
        return parsed

    def authenticate_ticket(self, ticket_hex: str, identity: str, app_id: int | None = None) -> int:
        response = self._request_json(
            "/ISteamUserAuth/AuthenticateUserTicket/v1/",
            {
                "appid": self.app_id if app_id is None else app_id,
                "ticket": ticket_hex,
                "identity": identity,
            },
            post=False,
        )
        params = response.get("response", {}).get("params", {})
        if not isinstance(params, dict) or str(params.get("result", "")).upper() != "OK":
            raise SteamWebApiError("Steam rejected the authentication ticket")
        steam_id_text = str(params.get("steamid", ""))
        if not steam_id_text.isdecimal():
            raise SteamWebApiError("Steam authentication response omitted the Steam ID")
        return int(steam_id_text)

    def check_app_ownership(self, steam_id: int, app_id: int | None = None) -> bool:
        response = self._request_json(
            "/ISteamUser/CheckAppOwnership/v4/",
            {"appid": self.app_id if app_id is None else app_id, "steamid": steam_id},
            post=False,
        )
        ownership = response.get("appownership", {})
        return isinstance(ownership, dict) and ownership.get("ownsapp") is True

    def get_player_summary(self, steam_id: int) -> dict[str, str]:
        response = self._request_json(
            "/ISteamUser/GetPlayerSummaries/v2/",
            {"steamids": steam_id},
            post=False,
        )
        players = response.get("response", {}).get("players", [])
        if not isinstance(players, list) or not players or not isinstance(players[0], dict):
            return {}
        player = players[0]
        return {
            "persona_name": str(player.get("personaname", ""))[:128],
            "avatar_url": str(player.get("avatarfull", ""))[:512],
            "profile_url": str(player.get("profileurl", ""))[:512],
        }

    def find_or_create_leaderboard(self, name: str) -> dict[str, Any]:
        return self._request_json(
            "/ISteamLeaderboards/FindOrCreateLeaderboard/v2/",
            {
                "appid": self.app_id,
                "name": name,
                "sortmethod": "Ascending",
                "displaytype": "TimeMilliSeconds",
                "createifnotfound": "true",
                "onlytrustedwrites": "true",
                "onlyfriendsreads": "false",
            },
            post=True,
        )

    def get_leaderboards(self) -> dict[str, Any]:
        return self._request_json(
            "/ISteamLeaderboards/GetLeaderboardsForGame/v2/",
            {"appid": self.app_id},
            post=False,
        )
