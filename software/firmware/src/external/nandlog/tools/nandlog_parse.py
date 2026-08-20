#!/usr/bin/env python3
"""Reference parser for the nandlog on-flash and offload formats.

This is the specification's second implementation. The device computes CRC-32 with the IEEE 802.3 polynomial
precisely so that this file can verify it with ``zlib.crc32`` and nothing bespoke -- if the two ever disagree,
one of them is wrong, and having a reader written independently of the writer is what makes that visible.

Two inputs are understood:

  * a **raw image**, a byte-for-byte dump of the part, parsed by walking every page; and
  * an **offload stream**, the framing a device sends over the wire, parsed sequentially.

Records inside a page are deliberately *not* interpreted. The log stores opaque bytes, and the length of a
record is a property of the application that wrote it, so a payload is handed back whole.

    ./nandlog_parse.py image  dump.bin --page-size 4096 --spare-size 256
    ./nandlog_parse.py stream offload.bin
    ./nandlog_parse.py image  dump.bin --json
"""

import argparse
import json
import struct
import sys
import zlib

PAGE_MAGIC = 0x31505454        # 'TTP1', little-endian: records are opaque
PAGE_MAGIC_FRAMED = 0x32505454 # 'TTP2': each record carries its own length
FRAMING_LENGTH = struct.Struct("<H")   # the per-record data length that framing prefixes
META_MAGIC = 0x314D5454        # 'TTM1'
STREAM_MAGIC = 0x31535454      # 'TTS1'
NO_TIMESTAMP = 0xFFFFFFFF

PAGE_HEADER = struct.Struct("<IIIIIHHII")     # magic epoch seq first last length count payload_crc header_crc
META_HEADER = struct.Struct("<IIIIHHIII")     # magic epoch start created length version details_crc header_crc reserved
STREAM_HEADER = struct.Struct("<IHHII")       # magic version details_length total_pages total_payload_bytes
WIRE_PAGE = struct.Struct("<IIIHHI")          # seq first last length count payload_crc

PAGE_HEADER_CRC_BYTES = PAGE_HEADER.size - 4          # the header CRC covers everything before itself
META_HEADER_CRC_BYTES = META_HEADER.size - 8          # ... and the metadata header excludes its trailing reserved word


def _crc(data):
    return zlib.crc32(data) & 0xFFFFFFFF


def _timestamp(value):
    return None if value == NO_TIMESTAMP else value


def walk_records(payload):
    """Split a framed payload into records.

    Only pages carrying PAGE_MAGIC_FRAMED can be walked: without the length prefix a record's extent is a
    property of the application that wrote it, and guessing would invent records. Returns the records parsed
    and however many bytes were left over, which should be zero.
    """
    records, offset = [], 0
    while offset + FRAMING_LENGTH.size <= len(payload):
        length = FRAMING_LENGTH.unpack_from(payload, offset)[0]
        offset += FRAMING_LENGTH.size
        if offset + 5 + length > len(payload):
            break            # a truncated tail, not a record
        record_type = payload[offset]
        timestamp = struct.unpack_from("<I", payload, offset + 1)[0]
        offset += 5
        records.append({"type": record_type, "timestamp": timestamp,
                        "data": bytes(payload[offset:offset + length])})
        offset += length
    return records, len(payload) - offset


class Page:
    """One log page, and the verdict on whether it can be trusted."""

    __slots__ = ("index", "epoch", "seq", "first_timestamp", "last_timestamp",
                 "record_count", "payload", "header_ok", "payload_ok", "framed")

    def __init__(self, index, raw):
        (magic, self.epoch, self.seq, first, last,
         length, self.record_count, payload_crc, header_crc) = PAGE_HEADER.unpack_from(raw)
        self.index = index
        self.first_timestamp = _timestamp(first)
        self.last_timestamp = _timestamp(last)
        self.payload = b""
        self.payload_ok = False

        # A page is trustworthy only if it carries the magic and its header checksums correctly. Nothing else
        # in the header -- including the payload length -- means anything until that holds
        # The magic says which format the page is in, so a log written across a firmware change parses
        # correctly page by page rather than needing to be told
        self.framed = (magic == PAGE_MAGIC_FRAMED)
        self.header_ok = (magic in (PAGE_MAGIC, PAGE_MAGIC_FRAMED) and
                          header_crc == _crc(raw[:PAGE_HEADER_CRC_BYTES]) and
                          length <= len(raw) - PAGE_HEADER.size)
        if self.header_ok:
            self.payload = bytes(raw[PAGE_HEADER.size:PAGE_HEADER.size + length])
            self.payload_ok = (payload_crc == _crc(self.payload))

    @property
    def ok(self):
        return self.header_ok and self.payload_ok

    def records(self):
        """Records in this page, or None if it is not framed and therefore not walkable."""
        if not (self.framed and self.payload_ok):
            return None
        records, leftover = walk_records(self.payload)
        return records if not leftover else None

    def as_dict(self):
        out = {"index": self.index, "epoch": self.epoch, "seq": self.seq,
               "first_timestamp": self.first_timestamp, "last_timestamp": self.last_timestamp,
               "record_count": self.record_count, "payload_length": len(self.payload),
               "framed": self.framed, "header_ok": self.header_ok, "payload_ok": self.payload_ok}
        records = self.records()
        if records is not None:
            out["records_parsed"] = len(records)
        return out


class Metadata:
    """One slot of the metadata ring, describing an epoch."""

    __slots__ = ("index", "epoch", "log_start_page", "created_timestamp",
                 "format_version", "details", "header_ok", "details_ok")

    def __init__(self, index, raw):
        (magic, self.epoch, self.log_start_page, self.created_timestamp,
         length, self.format_version, details_crc, header_crc, _reserved) = META_HEADER.unpack_from(raw)
        self.index = index
        self.details = b""
        self.details_ok = False
        self.header_ok = (magic == META_MAGIC and
                          header_crc == _crc(raw[:META_HEADER_CRC_BYTES]) and
                          length <= len(raw) - META_HEADER.size)
        if self.header_ok:
            self.details = bytes(raw[META_HEADER.size:META_HEADER.size + length])
            self.details_ok = (details_crc == _crc(self.details))

    @property
    def ok(self):
        return self.header_ok and self.details_ok

    def as_dict(self):
        return {"index": self.index, "epoch": self.epoch, "log_start_page": self.log_start_page,
                "created_timestamp": self.created_timestamp, "format_version": self.format_version,
                "details_length": len(self.details), "header_ok": self.header_ok, "details_ok": self.details_ok}


def parse_image(data, page_size, spare_size):
    """Walk a raw dump. Pages are found by their magic rather than by knowing the layout, so a dump taken
    without knowing where the log region starts still parses."""
    stride = page_size + spare_size
    pages, metadata = [], []
    for index in range(len(data) // stride):
        raw = data[index * stride: index * stride + page_size]
        if len(raw) < PAGE_HEADER.size:
            break
        magic = struct.unpack_from("<I", raw)[0]
        if magic in (PAGE_MAGIC, PAGE_MAGIC_FRAMED):
            pages.append(Page(index, raw))
        elif magic == META_MAGIC:
            metadata.append(Metadata(index, raw))
    return pages, metadata


def parse_stream(data):
    """Walk an offload stream: one header, optional caller metadata, then a framed page each."""
    if len(data) < STREAM_HEADER.size:
        raise ValueError("stream is shorter than its header")
    magic, version, details_length, total_pages, total_payload_bytes = STREAM_HEADER.unpack_from(data)
    if magic != STREAM_MAGIC:
        raise ValueError(f"stream magic is 0x{magic:08X}, expected 0x{STREAM_MAGIC:08X}")
    if version not in (1, 2):
        raise ValueError(f"unsupported format version {version}")

    offset = STREAM_HEADER.size
    details = bytes(data[offset:offset + details_length])
    offset += details_length

    pages = []
    while offset + WIRE_PAGE.size <= len(data) and len(pages) < total_pages:
        seq, first, last, length, count, payload_crc = WIRE_PAGE.unpack_from(data, offset)
        offset += WIRE_PAGE.size
        payload = bytes(data[offset:offset + length])
        offset += length
        pages.append({
            "seq": seq,
            "first_timestamp": _timestamp(first),
            "last_timestamp": _timestamp(last),
            "record_count": count,
            "payload_length": length,
            # A zero-length page is not corruption: it is the device saying "this one is a gap"
            "payload_ok": (length == 0) or (payload_crc == _crc(payload)),
            "payload": payload,
        })
        # A stream carries no per-page magic, so its framing comes from the header version it announced
        if (version == 2) and pages[-1]["payload_ok"] and length:
            walked, leftover = walk_records(payload)
            pages[-1]["records"] = walked if not leftover else None

    return {
        "format_version": version,
        "total_pages": total_pages,
        "total_payload_bytes": total_payload_bytes,
        "details": details,
        "pages": pages,
    }


def _report_image(pages, metadata, as_json):
    if as_json:
        json.dump({"metadata": [m.as_dict() for m in metadata],
                   "pages": [p.as_dict() for p in pages]}, sys.stdout, indent=2)
        print()
        return

    print(f"metadata slots: {len(metadata)}")
    for m in sorted(metadata, key=lambda m: m.epoch):
        status = "ok" if m.ok else ("details failed CRC" if m.header_ok else "header failed CRC")
        print(f"  page {m.index:6d}  epoch {m.epoch:5d}  log starts at page {m.log_start_page:6d}  "
              f"{len(m.details):4d} bytes of details  [{status}]")

    print(f"\nlog pages: {len(pages)}")
    epochs = {}
    for p in pages:
        epochs.setdefault(p.epoch, []).append(p)
    for epoch in sorted(epochs):
        group = epochs[epoch]
        good = [p for p in group if p.ok]
        payload = sum(len(p.payload) for p in good)
        records = sum(p.record_count for p in good)
        seqs = sorted(p.seq for p in good)
        gaps = [s for s in range(seqs[0], seqs[-1] + 1) if s not in set(seqs)] if seqs else []
        span = [p for p in good if p.first_timestamp is not None]
        framed = sum(1 for p in good if p.framed)
        walkable = sum(1 for p in good if p.records() is not None)
        print(f"  epoch {epoch:5d}: {len(good)}/{len(group)} pages verified, "
              f"{payload} payload bytes, {records} records")
        if framed:
            print(f"                 {framed} page(s) length-framed, {walkable} fully walkable")
        if span:
            print(f"                 timestamps {min(p.first_timestamp for p in span)} .. "
                  f"{max(p.last_timestamp for p in span)}")
        if gaps:
            print(f"                 missing sequence numbers: {gaps[:16]}"
                  f"{' ...' if len(gaps) > 16 else ''}")

    bad = [p for p in pages if not p.ok]
    if bad:
        print(f"\n{len(bad)} page(s) failed verification:")
        for p in bad[:16]:
            print(f"  page {p.index:6d}  epoch {p.epoch:5d}  seq {p.seq:6d}  "
                  f"{'payload' if p.header_ok else 'header'} CRC mismatch")


def _report_stream(stream, as_json):
    pages = stream["pages"]
    if as_json:
        printable = dict(stream)
        printable["details"] = stream["details"].hex()
        printable["pages"] = [{k: v for k, v in p.items() if k != "payload"} for p in pages]
        json.dump(printable, sys.stdout, indent=2)
        print()
        return

    verified = [p for p in pages if p["payload_ok"] and p["payload_length"]]
    gaps = [p for p in pages if p["payload_length"] == 0]
    failed = [p for p in pages if not p["payload_ok"]]
    print(f"format version {stream['format_version']}, {len(stream['details'])} bytes of caller metadata")
    print(f"pages: {len(pages)} received of {stream['total_pages']} advertised")
    print(f"  {len(verified)} verified, {len(gaps)} reported as gaps, {len(failed)} failed CRC")
    print(f"payload: {sum(p['payload_length'] for p in verified)} bytes verified "
          f"of {stream['total_payload_bytes']} advertised")
    if gaps:
        missing = [p["seq"] for p in gaps]
        print(f"  request retransmission of: {missing[:16]}{' ...' if len(missing) > 16 else ''}")


def main():
    parser = argparse.ArgumentParser(description="Parse and verify nandlog images and offload streams.")
    sub = parser.add_subparsers(dest="mode", required=True)

    image = sub.add_parser("image", help="a raw dump of the part")
    image.add_argument("path")
    image.add_argument("--page-size", type=int, default=4096)
    image.add_argument("--spare-size", type=int, default=256,
                       help="spare bytes per page in the dump; 0 if the dump is main-array only")
    image.add_argument("--json", action="store_true")

    stream = sub.add_parser("stream", help="an offload stream as sent by a device")
    stream.add_argument("path")
    stream.add_argument("--json", action="store_true")

    args = parser.parse_args()
    data = open(args.path, "rb").read()

    if args.mode == "image":
        pages, metadata = parse_image(data, args.page_size, args.spare_size)
        _report_image(pages, metadata, args.json)
        return 1 if any(not p.ok for p in pages) else 0

    parsed = parse_stream(data)
    _report_stream(parsed, args.json)
    return 1 if any(not p["payload_ok"] for p in parsed["pages"]) else 0


if __name__ == "__main__":
    sys.exit(main())
