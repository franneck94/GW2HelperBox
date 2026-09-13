#!/usr/bin/env python3
"""Scrape Black Lion Chest skin collections and emit a C++ header.

Pipeline
--------
1. Gather weapon collection achievements and skin IDs from the official GW2 API.
2. Read every weapon collection from the Black Lion Collections wiki page,
    follow each collection's weapon-skin links, and extract the skin ID from the
    skin page's ``gamelink`` element.
3. Write the collection names and ``(skin_id, skin_name)`` pairs to
    ``src/CollectionSkinIDs.h``.

Run this on a network that can reach ``wiki.guildwars2.com`` and
``api.guildwars2.com`` (a corporate proxy may block them).

Examples
--------
    py scripts/scrape_bl_collections.py
    py scripts/scrape_bl_collections.py --insecure   # skip TLS verification
"""

from __future__ import annotations

import argparse
from html.parser import HTMLParser
from pathlib import Path
import time
from urllib.parse import urljoin

import requests

WIKI_COLLECTIONS_URL = "https://wiki.guildwars2.com/wiki/Black_Lion_Collections"
GW2_API = "https://api.guildwars2.com/v2"

USER_AGENT = (
    "GW2TP-collection-scraper/1.0 (https://github.com/franneck94/Gw2TP)"
)


def make_session(insecure: bool) -> requests.Session:
    session = requests.Session()
    session.headers.update({"User-Agent": USER_AGENT})
    if insecure:
        session.verify = False
        requests.packages.urllib3.disable_warnings()  # type: ignore[attr-defined]
    return session


def get_json(session: requests.Session, url: str, params: dict) -> dict:
    for attempt in range(4):
        try:
            response = session.get(url, params=params, timeout=30)
            response.raise_for_status()
            return response.json()
        except requests.RequestException:
            if attempt == 3:
                raise
            time.sleep(1.5 * (attempt + 1))
    return {}


def fetch_api_collections(session: requests.Session) -> dict[str, list[int]]:
    """Return weapon collections and skin IDs from achievement data."""
    print("API: listing achievement ids")
    all_ids = get_json(session, f"{GW2_API}/achievements", {})
    ids = [achievement["id"] if isinstance(achievement, dict) else achievement for achievement in all_ids]
    print(f"API: {len(ids)} achievements; fetching details")

    collections: dict[str, list[int]] = {}
    for start in range(0, len(ids), 200):
        chunk = ids[start : start + 200]
        for achievement in get_json(
            session,
            f"{GW2_API}/achievements",
            {"ids": ",".join(str(achievement_id) for achievement_id in chunk)},
        ):
            if achievement.get("type") != "ItemSet":
                continue
            skin_ids = [
                bit["id"]
                for bit in achievement.get("bits", [])
                if bit.get("type") == "Skin" and "id" in bit
            ]
            if skin_ids and "Weapon Collection" in achievement.get("name", ""):
                collections[achievement["name"].strip()] = skin_ids
    print(f"API: found {len(collections)} weapon collections")
    return collections


class CollectionLinkParser(HTMLParser):
    """Extract weapon collection links from the Black Lion Collections page."""

    def __init__(self) -> None:
        super().__init__()
        self.collections: dict[str, str] = {}
        self._collection_href = ""
        self._collection_title = ""
        self._in_collection_heading = False

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        attributes = dict(attrs)
        element_id = attributes.get("id") or ""
        if tag == "th" and "Weapon_Collection" in element_id:
            self._in_collection_heading = True
        elif tag == "a" and self._in_collection_heading:
            href = attributes.get("href")
            title = attributes.get("title")
            if href and title and title.endswith("Weapon Collection"):
                self._collection_href = href
                self._collection_title = title

    def handle_endtag(self, tag: str) -> None:
        if tag == "th" and self._in_collection_heading:
            if self._collection_href and self._collection_title:
                self.collections[self._collection_title] = self._collection_href
            self._in_collection_heading = False
            self._collection_href = ""
            self._collection_title = ""


class SkinLinkParser(HTMLParser):
    """Extract weapon links matching a collection's base name."""

    def __init__(self, collection_base_name: str) -> None:
        super().__init__()
        self.collection_base_name = collection_base_name
        self.skins: dict[str, str] = {}

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        if tag != "a":
            return
        attributes = dict(attrs)
        href = attributes.get("href") or ""
        title = attributes.get("title") or ""
        if (
            href.startswith("/wiki/")
            and title.startswith(f"{self.collection_base_name} ")
        ):
            self.skins[title] = href


class SkinIdParser(HTMLParser):
    """Extract skin and item IDs from a skin page."""

    def __init__(self) -> None:
        super().__init__()
        self.skin_id: int | None = None
        self.item_id: int | None = None

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        if tag != "span":
            return
        attributes = dict(attrs)
        if attributes.get("class") != "gamelink":
            return
        raw_id = attributes.get("data-id")
        if not raw_id or not raw_id.isdigit():
            return
        if attributes.get("data-type") == "skin":
            self.skin_id = int(raw_id)
        elif attributes.get("data-type") == "item":
            self.item_id = int(raw_id)


class AcquisitionSkinLinkParser(HTMLParser):
    """Extract the consumable skin link from an item's Acquisition section."""

    def __init__(self, item_name: str) -> None:
        super().__init__()
        self.skin_href = ""
        self.skin_name = f"{item_name} Skin"

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        if tag != "a":
            return
        attributes = dict(attrs)
        if attributes.get("title") == self.skin_name:
            self.skin_href = attributes.get("href") or ""


def fetch_wiki_collections(
    session: requests.Session,
) -> tuple[dict[str, list[int]], dict[int, str], dict[int, int]]:
    """Fetch weapon collections and skin IDs directly from the wiki pages."""
    print(f"Wiki: reading {WIKI_COLLECTIONS_URL}")
    page = session.get(WIKI_COLLECTIONS_URL, timeout=30)
    page.raise_for_status()
    collection_parser = CollectionLinkParser()
    collection_parser.feed(page.text)
    print(f"Wiki: found {len(collection_parser.collections)} weapon collections")

    collections: dict[str, list[int]] = {}
    skin_names: dict[int, str] = {}
    skin_to_item: dict[int, int] = {}
    for collection_name, collection_href in collection_parser.collections.items():
        collection_url = urljoin(WIKI_COLLECTIONS_URL, collection_href)
        collection_page = session.get(collection_url, timeout=30)
        collection_page.raise_for_status()
        collection_base_name = collection_name.removesuffix(" Weapon Collection")
        skin_parser = SkinLinkParser(collection_base_name)
        skin_parser.feed(collection_page.text)

        skin_ids: list[int] = []
        for item_name, item_href in skin_parser.skins.items():
            item_page = session.get(urljoin(WIKI_COLLECTIONS_URL, item_href), timeout=30)
            item_page.raise_for_status()
            acquisition_parser = AcquisitionSkinLinkParser(item_name)
            acquisition_parser.feed(item_page.text)
            if not acquisition_parser.skin_href:
                continue

            skin_page = session.get(
                urljoin(WIKI_COLLECTIONS_URL, acquisition_parser.skin_href),
                timeout=30,
            )
            skin_page.raise_for_status()
            id_parser = SkinIdParser()
            id_parser.feed(skin_page.text)
            if id_parser.skin_id is not None:
                skin_ids.append(id_parser.skin_id)
                skin_names[id_parser.skin_id] = item_name
                if id_parser.item_id is not None:
                    skin_to_item[id_parser.skin_id] = id_parser.item_id
        if skin_ids:
            collections[collection_name] = skin_ids
        print(f"  {collection_name}: {len(skin_ids)} skins")

    return collections, skin_names, skin_to_item


def write_header(
    output: Path,
    matched: dict[str, list[int]],
    skin_names: dict[int, str],
    skin_to_item: dict[int, int],
) -> None:
    def escape(value: str) -> str:
        return value.replace("\\", "\\\\").replace('"', '\\"')

    lines = [
        "// Auto-generated by scripts/scrape_bl_collections.py — do not edit by hand.",
        f"// Black Lion Chest skin collections: {len(matched)}",
        "",
        "#pragma once",
        "",
        "#include <map>",
        "#include <string>",
        "#include <utility>",
        "#include <vector>",
        "",
        "namespace CollectionSkins",
        "{",
        "    inline const std::map<std::string, std::vector<std::pair<int, std::string>>> COLLECTIONS = {",
    ]
    for name in sorted(matched):
        entries = ", ".join(
            f'{{{skin_id}, "{escape(skin_names.get(skin_id, "Skin "+str(skin_id)))}"}}'
            for skin_id in matched[name]
        )
        lines.append(f'        {{"{escape(name)}", {{{entries}}}}},')
    lines.append("    };")
    lines += [
        "",
        "    inline const std::map<int, int> SKIN_TO_ITEM = {",
    ]
    for skin_id in sorted(skin_to_item):
        lines.append(f"        {{{skin_id}, {skin_to_item[skin_id]}}},")
    lines.append("    };")
    lines += ["}", ""]
    output.write_text("\n".join(lines), encoding="utf-8")
    print(
        f"Wrote {output} ({len(matched)} collections, {len(skin_names)} skins, {len(skin_to_item)} items)"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--insecure",
        action="store_true",
        help="Disable TLS certificate verification (corporate proxies).",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(__file__).resolve().parent.parent
        / "src"
        / "CollectionSkinIDs.h",
        help="Header file to write.",
    )
    args = parser.parse_args()

    session = make_session(args.insecure)

    api_collections = fetch_api_collections(session)
    wiki_collections, skin_names, skin_to_item = fetch_wiki_collections(session)
    for collection_name, skin_ids in wiki_collections.items():
        api_collections.setdefault(collection_name, skin_ids)

    write_header(args.output, api_collections, skin_names, skin_to_item)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
