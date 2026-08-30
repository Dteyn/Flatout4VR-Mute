#!/usr/bin/env python3
"""FlatOut 4 VR Mute v0.4.0 developer compatibility scanner.

DEVELOPER TOOL - NOT PART OF THE RELEASE PACKAGE. This script never modifies
Flatout.exe. It conservatively rediscovers the NetworkManager voice/profile ABI
and local voice transmit path so the hardcoded runtime constants can be reviewed
and updated when FlatOut 4 changes. It does not create a runtime override file.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple

SCANNER_VERSION = "0.4.0"
OFFICIAL_SHA256 = "0ec9c645f3cb17e3921d54a5cef58729f077bf92e4c501a72b2acc67c5da5a9c"
OFFICIAL_SIZE = 18_359_296
MAX_VTABLE_SCAN_SLOTS = 512
MAX_ACCEPTED_PROFILE_TARGETS = 16
MAX_CAPTURE_TO_SEND_DISTANCE = 0x800

CAPTURE_SIGNATURE = bytes.fromhex(
    "40 55 53 41 56 48 8D AC 24 20 FF FF FF 48 81 EC"
)
GETVOICE_DISCARD_PREFIX = bytes.fromhex("FF 50 50 85 C0 0F 85")
SEND_SIGNATURE = bytes.fromhex("FF 90 98 00 00 00")

EXPORTS = {
    "GetInstance": "?GetInstance@NetworkManager@Game@PlayAll@@SAPEAV123@XZ",
    "GetMaxPlayerSlots": "?GetMaxPlayerSlots@NetworkManager@Game@PlayAll@@QEBAHXZ",
    "SlotGetIndexLocal": "?SlotGetIndexLocal@NetworkManager@Game@PlayAll@@UEBAHXZ",
    "SlotIsOccupied": "?SlotIsOccupied@NetworkManager@Game@PlayAll@@UEBA_NH@Z",
    "VoiceChatMutePlayer": "?VoiceChatMutePlayer@NetworkManager@Game@PlayAll@@UEAAXH_N@Z",
    "VoiceChatIsPlayerMuted": "?VoiceChatIsPlayerMuted@NetworkManager@Game@PlayAll@@UEBA_NH@Z",
    "VoiceChatTogglePlayerMuted": "?VoiceChatTogglePlayerMuted@NetworkManager@Game@PlayAll@@UEAAXH@Z",
    "ShowPlayerProfile": "?ShowPlayerProfile@NetworkManager@Game@PlayAll@@UEAAXH@Z",
    "BaseNetworkManagerVtable": "??_7NetworkManager@Game@PlayAll@@6B@",
}


@dataclass(frozen=True)
class Section:
    name: str
    virtual_address: int
    virtual_size: int
    raw_offset: int
    raw_size: int
    characteristics: int

    @property
    def executable(self) -> bool:
        return bool(self.characteristics & 0x20000000)


class PE64:
    def __init__(self, path: Path):
        self.path = path
        self.data = path.read_bytes()
        if len(self.data) < 0x100 or self.data[:2] != b"MZ":
            raise ValueError("not a DOS/PE executable")
        self.pe_offset = struct.unpack_from("<I", self.data, 0x3C)[0]
        if self.data[self.pe_offset : self.pe_offset + 4] != b"PE\0\0":
            raise ValueError("PE signature not found")
        coff = self.pe_offset + 4
        machine = struct.unpack_from("<H", self.data, coff)[0]
        if machine != 0x8664:
            raise ValueError(f"expected x64 machine 0x8664, got 0x{machine:X}")
        self.section_count = struct.unpack_from("<H", self.data, coff + 2)[0]
        optional_size = struct.unpack_from("<H", self.data, coff + 16)[0]
        self.optional_offset = coff + 20
        if struct.unpack_from("<H", self.data, self.optional_offset)[0] != 0x20B:
            raise ValueError("expected PE32+ (x64) optional header")
        self.image_base = struct.unpack_from("<Q", self.data, self.optional_offset + 24)[0]
        self.size_of_image = struct.unpack_from("<I", self.data, self.optional_offset + 56)[0]
        self.sections: List[Section] = []
        section_table = self.optional_offset + optional_size
        for index in range(self.section_count):
            offset = section_table + index * 40
            if offset + 40 > len(self.data):
                raise ValueError("truncated PE section table")
            name = self.data[offset : offset + 8].split(b"\0", 1)[0].decode("ascii", "replace")
            virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
                "<IIII", self.data, offset + 8
            )
            characteristics = struct.unpack_from("<I", self.data, offset + 36)[0]
            self.sections.append(
                Section(name, virtual_address, virtual_size, raw_offset, raw_size, characteristics)
            )

    def directory(self, index: int) -> Tuple[int, int]:
        return struct.unpack_from("<II", self.data, self.optional_offset + 112 + index * 8)

    def section_for_rva(self, rva: int) -> Optional[Section]:
        for section in self.sections:
            span = max(section.virtual_size, section.raw_size)
            if section.virtual_address <= rva < section.virtual_address + span:
                return section
        return None

    def rva_to_offset(self, rva: int) -> int:
        section = self.section_for_rva(rva)
        if section is None:
            raise ValueError(f"RVA 0x{rva:X} is not mapped")
        offset = section.raw_offset + (rva - section.virtual_address)
        if offset < 0 or offset >= len(self.data):
            raise ValueError(f"RVA 0x{rva:X} maps outside file")
        return offset

    def offset_to_rva(self, offset: int) -> int:
        for section in self.sections:
            if section.raw_offset <= offset < section.raw_offset + section.raw_size:
                return section.virtual_address + (offset - section.raw_offset)
        raise ValueError(f"file offset 0x{offset:X} is not mapped")

    def cstring(self, offset: int) -> str:
        end = self.data.find(b"\0", offset)
        if end < 0:
            raise ValueError("unterminated export name")
        return self.data[offset:end].decode("ascii", "replace")

    def exports(self) -> Dict[str, int]:
        export_rva, _ = self.directory(0)
        if export_rva == 0:
            return {}
        offset = self.rva_to_offset(export_rva)
        fields = struct.unpack_from("<IIHHIIIIIII", self.data, offset)
        function_count, name_count = fields[6], fields[7]
        function_table = self.rva_to_offset(fields[8])
        name_table = self.rva_to_offset(fields[9])
        ordinal_table = self.rva_to_offset(fields[10])
        result: Dict[str, int] = {}
        for index in range(name_count):
            name_rva = struct.unpack_from("<I", self.data, name_table + index * 4)[0]
            name = self.cstring(self.rva_to_offset(name_rva))
            ordinal_index = struct.unpack_from("<H", self.data, ordinal_table + index * 2)[0]
            if ordinal_index >= function_count:
                continue
            function_rva = struct.unpack_from("<I", self.data, function_table + ordinal_index * 4)[0]
            result[name] = function_rva
        return result

    def qword_at_rva(self, rva: int) -> int:
        return struct.unpack_from("<Q", self.data, self.rva_to_offset(rva))[0]

    def bytes_at_rva(self, rva: int, size: int) -> bytes:
        offset = self.rva_to_offset(rva)
        end = offset + size
        if end > len(self.data):
            raise ValueError(f"RVA 0x{rva:X} byte range is truncated")
        return self.data[offset:end]

    def find_bytes(self, needle: bytes) -> List[int]:
        hits: List[int] = []
        cursor = 0
        while True:
            offset = self.data.find(needle, cursor)
            if offset < 0:
                break
            try:
                hits.append(self.offset_to_rva(offset))
            except ValueError:
                pass
            cursor = offset + 1
        return hits


def qword_sequence(image_base: int, rvas: Iterable[int]) -> bytes:
    return b"".join(struct.pack("<Q", image_base + rva) for rva in rvas)


def require_exports(exports: Dict[str, int]) -> None:
    missing = [f"{key}: {name}" for key, name in EXPORTS.items() if name not in exports]
    if missing:
        raise ValueError(
            "required exported signatures are missing; this build needs a real DLL review:\n  "
            + "\n  ".join(missing)
        )


def discover_vtable_layout(pe: PE64, exports: Dict[str, int]) -> dict:
    mute_rva = exports[EXPORTS["VoiceChatMutePlayer"]]
    is_muted_rva = exports[EXPORTS["VoiceChatIsPlayerMuted"]]
    toggle_rva = exports[EXPORTS["VoiceChatTogglePlayerMuted"]]
    base_show_rva = exports[EXPORTS["ShowPlayerProfile"]]
    base_vtable_rva = exports[EXPORTS["BaseNetworkManagerVtable"]]

    expected = [
        pe.image_base + mute_rva,
        pe.image_base + is_muted_rva,
        pe.image_base + toggle_rva,
        pe.image_base + base_show_rva,
    ]
    slot_hits: List[int] = []
    for slot in range(MAX_VTABLE_SCAN_SLOTS - 3):
        try:
            values = [pe.qword_at_rva(base_vtable_rva + (slot + i) * 8) for i in range(4)]
        except ValueError:
            break
        if values == expected:
            slot_hits.append(slot)
    if len(slot_hits) != 1:
        raise ValueError(
            "could not uniquely derive the base NetworkManager voice/profile vtable layout "
            f"(slot hits={slot_hits})"
        )

    mute_slot = slot_hits[0]
    is_muted_slot = mute_slot + 1
    toggle_slot = mute_slot + 2
    profile_slot = mute_slot + 3

    trio = qword_sequence(pe.image_base, (mute_rva, is_muted_rva, toggle_rva))
    sequence_hits = pe.find_bytes(trio)
    accepted: List[int] = []
    derived_tables: List[int] = []
    for hit in sequence_hits:
        try:
            target_va = pe.qword_at_rva(hit + 24)
        except ValueError:
            continue
        if not (pe.image_base <= target_va < pe.image_base + pe.size_of_image):
            continue
        target_rva = target_va - pe.image_base
        section = pe.section_for_rva(target_rva)
        if section is None or not section.executable:
            continue
        if hit >= mute_slot * 8:
            derived_tables.append(hit - mute_slot * 8)
        if target_rva != base_show_rva:
            accepted.append(target_rva)

    accepted = sorted(set(accepted))
    derived_tables = sorted(set(derived_tables))
    if not accepted:
        raise ValueError("no derived ShowPlayerProfile target was found")
    if len(accepted) > MAX_ACCEPTED_PROFILE_TARGETS:
        raise ValueError(
            f"too many derived ShowPlayerProfile targets ({len(accepted)}); refusing ambiguous build"
        )

    return {
        "mute_slot": mute_slot,
        "is_muted_slot": is_muted_slot,
        "toggle_slot": toggle_slot,
        "profile_slot": profile_slot,
        "base_show_rva": base_show_rva,
        "accepted_profile_rvas": accepted,
        "derived_vtable_rvas": derived_tables,
    }


def discover_transmit_gate(pe: PE64) -> dict:
    capture_hits = [
        rva
        for rva in pe.find_bytes(CAPTURE_SIGNATURE)
        if (section := pe.section_for_rva(rva)) is not None and section.executable
    ]
    if len(capture_hits) != 1:
        raise ValueError(
            "could not uniquely locate the known Steam voice capture routine signature "
            f"(hits={[f'0x{x:X}' for x in capture_hits]})"
        )
    capture_rva = capture_hits[0]

    send_hits = [
        rva
        for rva in pe.find_bytes(SEND_SIGNATURE)
        if capture_rva < rva <= capture_rva + MAX_CAPTURE_TO_SEND_DISTANCE
        and (section := pe.section_for_rva(rva)) is not None
        and section.executable
    ]
    if len(send_hits) != 1:
        raise ValueError(
            "could not uniquely locate the RakNet send call inside the capture routine "
            f"(nearby hits={[f'0x{x:X}' for x in send_hits]})"
        )
    send_rva = send_hits[0]

    # The desired discard gate is specifically the TEST/JNE immediately after
    # ISteamUser::GetVoice (CALL [RAX+0x50]). An earlier TEST/JNE in the same
    # routine handles GetAvailableVoice and must not be selected.
    gate_prefix_hits = [
        rva
        for rva in pe.find_bytes(GETVOICE_DISCARD_PREFIX)
        if capture_rva < rva < send_rva
        and (section := pe.section_for_rva(rva)) is not None
        and section.executable
    ]
    if len(gate_prefix_hits) != 1:
        raise ValueError(
            "could not uniquely locate the post-GetVoice discard gate "
            f"(nearby hits={[f'0x{x:X}' for x in gate_prefix_hits]})"
        )

    # Prefix is: FF 50 50 | 85 C0 0F 85 rel32
    discard_rva = gate_prefix_hits[0] + 3
    discard_bytes = pe.bytes_at_rva(discard_rva, 8)
    if discard_bytes[:4] != bytes.fromhex("85 C0 0F 85"):
        raise ValueError("post-GetVoice discard gate has an unexpected instruction shape")
    rel32 = struct.unpack_from("<i", discard_bytes, 4)[0]
    cleanup_rva = discard_rva + 8 + rel32
    if not (send_rva < cleanup_rva <= capture_rva + MAX_CAPTURE_TO_SEND_DISTANCE):
        raise ValueError(
            "post-GetVoice discard branch does not target the expected cleanup region "
            f"(target=0x{cleanup_rva:X}, send=0x{send_rva:X})"
        )

    return {
        "capture_rva": capture_rva,
        "capture_bytes": pe.bytes_at_rva(capture_rva, len(CAPTURE_SIGNATURE)),
        "discard_rva": discard_rva,
        "discard_bytes": discard_bytes,
        "cleanup_rva": cleanup_rva,
        "send_rva": send_rva,
        "send_bytes": pe.bytes_at_rva(send_rva, len(SEND_SIGNATURE)),
    }


def scan(executable: Path, game_version: Optional[str]) -> dict:
    pe = PE64(executable)
    exports = pe.exports()
    require_exports(exports)
    vtable = discover_vtable_layout(pe, exports)
    transmit = discover_transmit_gate(pe)
    sha256 = hashlib.sha256(pe.data).hexdigest()
    return {
        "path": executable,
        "filename": executable.name,
        "sha256": sha256,
        "size": len(pe.data),
        "official_v187": sha256 == OFFICIAL_SHA256 and len(pe.data) == OFFICIAL_SIZE,
        "game_version": game_version or f"auto-{sha256[:12]}",
        "image_base": pe.image_base,
        "image_size": pe.size_of_image,
        "vtable": vtable,
        "transmit": transmit,
    }


def format_bytes(value: bytes) -> str:
    return " ".join(f"{x:02X}" for x in value)


def render_cpp_constants(report: dict) -> str:
    vtable = report["vtable"]
    transmit = report["transmit"]
    accepted = ", ".join(f"0x{x:08X}" for x in vtable["accepted_profile_rvas"])
    capture_bytes = ", ".join(f"0x{x:02X}" for x in transmit["capture_bytes"])
    discard_bytes = ", ".join(f"0x{x:02X}" for x in transmit["discard_bytes"])
    send_bytes = ", ".join(f"0x{x:02X}" for x in transmit["send_bytes"])
    size_literal = f"{report['size']:_}".replace("_", "'")
    return f"""// Scanner v{SCANNER_VERSION} result for {report['filename']}
// Game label: {report['game_version']}
constexpr char kExpectedSha256[] =
    "{report['sha256']}";
constexpr std::uintmax_t kExpectedFileSize = {size_literal};

constexpr std::size_t kVoiceChatMutePlayerVtableIndex = {vtable['mute_slot']};
constexpr std::size_t kVoiceChatIsPlayerMutedVtableIndex = {vtable['is_muted_slot']};
constexpr std::size_t kVoiceChatTogglePlayerMutedVtableIndex = {vtable['toggle_slot']};
constexpr std::size_t kShowPlayerProfileVtableIndex = {vtable['profile_slot']};
constexpr std::array<std::uintptr_t, {len(vtable['accepted_profile_rvas'])}>
    kAcceptedShowPlayerProfileRvas = {{{accepted}}};
constexpr std::uintptr_t kBaseShowPlayerProfileRva = 0x{vtable['base_show_rva']:08X};

constexpr std::uintptr_t kVoiceCaptureRoutineRva = 0x{transmit['capture_rva']:08X};
constexpr std::array<std::uint8_t, {len(transmit['capture_bytes'])}>
    kVoiceCaptureExpectedPrologue = {{{capture_bytes}}};
constexpr std::uintptr_t kVoiceDiscardGateRva = 0x{transmit['discard_rva']:08X};
constexpr std::array<std::uint8_t, {len(transmit['discard_bytes'])}>
    kVoiceDiscardExpectedBytes = {{{discard_bytes}}};
constexpr std::uintptr_t kVoiceSendCallRva = 0x{transmit['send_rva']:08X};
constexpr std::array<std::uint8_t, {len(transmit['send_bytes'])}>
    kVoiceSendExpectedBytes = {{{send_bytes}}};
"""


def render_developer_report(report: dict) -> str:
    vtable = report["vtable"]
    transmit = report["transmit"]
    lines = [
        f"FlatOut 4 VR Mute developer scan v{SCANNER_VERSION}",
        f"Executable: {report['path']}",
        f"Game label: {report['game_version']}",
        f"SHA-256: {report['sha256']}",
        f"Size: {report['size']}",
        f"Official v1.87 build: {'YES' if report['official_v187'] else 'NO'}",
        "",
        "Voice/profile ABI: PASS",
        (f"vtable slots: mute={vtable['mute_slot']} is-muted={vtable['is_muted_slot']} "
         f"toggle={vtable['toggle_slot']} profile={vtable['profile_slot']}"),
        "accepted profile RVAs: " + ", ".join(
            f"0x{x:X}" for x in vtable["accepted_profile_rvas"]
        ),
        f"base profile RVA: 0x{vtable['base_show_rva']:X}",
        "",
        "Transmit path: PASS",
        f"capture RVA: 0x{transmit['capture_rva']:X}",
        f"capture bytes: {format_bytes(transmit['capture_bytes'])}",
        f"discard gate RVA: 0x{transmit['discard_rva']:X}",
        f"discard bytes: {format_bytes(transmit['discard_bytes'])}",
        f"cleanup RVA: 0x{transmit['cleanup_rva']:X}",
        f"send validation RVA: 0x{transmit['send_rva']:X}",
        f"send bytes: {format_bytes(transmit['send_bytes'])}",
        "",
        "C++ constants",
        "-------------",
        render_cpp_constants(report).rstrip(),
        "",
    ]
    return "\n".join(lines)


def discover_default_executable(script_dir: Path) -> Path:
    candidates = [script_dir / "Flatout.exe", script_dir / "Flatout64_s.exe"]
    present = [path for path in candidates if path.is_file()]
    if not present:
        raise ValueError(
            "Flatout.exe was not found beside the scanner. Place this .py next to the game EXE "
            "or pass the EXE path explicitly."
        )
    if len(present) == 1:
        return present[0]
    hashes = [hashlib.sha256(path.read_bytes()).hexdigest() for path in present]
    if len(set(hashes)) != 1:
        raise ValueError(
            "both Flatout.exe and Flatout64_s.exe exist and differ; pass the intended EXE path explicitly"
        )
    return present[0]


def print_report(report: dict) -> None:
    print(render_developer_report(report), end="")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "executable",
        nargs="?",
        type=Path,
        help="Flatout.exe to scan (default: Flatout.exe beside this script)",
    )
    parser.add_argument(
        "--game-version",
        help="human-readable game version label for the generated developer report",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="optional text report path; no file is written unless this is supplied",
    )
    parser.add_argument(
        "--cpp-only",
        action="store_true",
        help="print only the C++ constants block",
    )
    args = parser.parse_args()

    script_dir = Path(__file__).resolve().parent
    try:
        executable = (
            args.executable.resolve()
            if args.executable
            else discover_default_executable(script_dir)
        )
        if not executable.is_file():
            raise ValueError(f"executable not found: {executable}")
        report = scan(executable, args.game_version)
    except Exception as exc:
        print(f"SCAN FAILED: {exc}", file=sys.stderr)
        return 2

    text = render_cpp_constants(report) if args.cpp_only else render_developer_report(report)
    print(text, end="")

    if args.output:
        output = args.output.resolve()
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(text, encoding="utf-8", newline="\n")
        print(f"Wrote developer report: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
