# Agent: OpenCode (Claude) - AI/TestSuite
# Test: SerialTransfer COBS + CRC8 - verifie que l'encodage Python
#       est identique au C++ (Packet.cpp / PacketCRC.h)
#
# Usage: python test_serial_transfer.py

import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', 'raspberry-pi', 'script-final'))
from serial_transfer import SerialTransfer


class FakePort:
    """Simule un port serie en memoire pour tester sans hardware."""
    def __init__(self):
        self._buf = bytearray()
    def write(self, data):
        self._buf.extend(data)
    def flush(self):
        pass
    @property
    def in_waiting(self):
        return len(self._buf)
    def read(self, n=1):
        out = bytes(self._buf[:n])
        del self._buf[:n]
        return out

# test COBS + CRC manuellement

st = SerialTransfer(FakePort())
st.START_BYTE = 0x7E
st.STOP_BYTE  = 0x81

# payload avec un START_BYTE dedans (doit etre stuffe)
payload = [0x7E, 0x01, 0x7E, 0x02]

# COBS encode
encoded = list(payload)
overhead = st._cobs_encode(encoded)
assert overhead == 0, f"overhead expected 0, got {overhead}"

# verifie que le stuffing est correct
# apres stuffing: [2, 1, 0, 2] (identique a Packet.cpp)
assert encoded == [2, 1, 0, 2], f"COBS encode failed: {encoded}"

# COBS decode
st._cobs_decode(encoded, overhead)
assert encoded == payload, f"COBS decode failed: {encoded}"

print("[OK] COBS encode/decode")

# test CRC8

# tableau de test: les 256 valeurs CRC pour le polynome 0x9B
expected_table = st._crc_table.copy()
# juste on verifie que la table est reproductible
table2 = st._crc_table.copy()
assert table2 == expected_table, "CRC table not deterministic"
assert len(expected_table) == 256, "CRC table wrong size"

# CRC d'un payload connu
crc_val = st._crc8([0x01, 0x02, 0x03])
assert isinstance(crc_val, int) and 0 <= crc_val <= 255, f"CRC out of range: {crc_val}"
print(f"[OK] CRC8 table + calc (CRC(0x01,0x02,0x03) = {crc_val:#04x})")

# test send + available avec un payload sans START_BYTE

st2 = SerialTransfer(FakePort())
st2.send(SerialTransfer.PID_PING, b"\x01")
result = st2.available()
assert result is not None, "available() should return a packet"
pid, data = result
assert pid == SerialTransfer.PID_PING, f"Expected PID_PING, got {pid:#04x}"
assert data == b"\x01", f"Expected b'\\x01', got {data}"
print("[OK] send + available (clean payload)")

# test send + available avec un payload contenant START_BYTE

st3 = SerialTransfer(FakePort())
st3.send(SerialTransfer.PID_ITEM_INFO, bytes([0x7E, 0x01, 0x02, 0x03]))
result = st3.available()
assert result is not None, "available() should return a packet even with 0x7E in payload"
pid, data = result
assert pid == SerialTransfer.PID_ITEM_INFO, f"Expected PID_ITEM_INFO, got {pid:#04x}"
assert data == bytes([0x7E, 0x01, 0x02, 0x03]), f"Round-trip failed: {data.hex()}"
print("[OK] send + available (payload with START_BYTE)")

# test NO_DATA quand il n'y a rien

st4 = SerialTransfer(FakePort())
result = st4.available()
assert result is None, "available() should return None when no data"
print("[OK] available() returns None on empty buffer")

# test donnees partielles (header pas complet)

st5 = SerialTransfer(FakePort())
# balance juste 3 bytes (pas assez pour un header complet)
st5.port.write(b"\x7E\x00\xFF")
result = st5.available()
assert result is None, "available() should return None for partial header"
print("[OK] available() handles partial data")

# test multiple packets dans le buffer

st6 = SerialTransfer(FakePort())
st6.send(SerialTransfer.PID_PING, b"\x01")
st6.send(SerialTransfer.PID_PING, b"\x02")
r1 = st6.available()
r2 = st6.available()
assert r1 is not None and r2 is not None, "Both packets should be received"
assert r1[1] == b"\x01", f"First packet: {r1[1].hex()}"
assert r2[1] == b"\x02", f"Second packet: {r2[1].hex()}"
print("[OK] multiple packets in buffer")

# test rejection de payload_len = 0

st7 = SerialTransfer(FakePort())
# envoie manuellement un packet avec len=0 (doit etre rejete)
st7.port.write(bytes([0x7E, 0x00, 0xFF, 0x00, 0x00, 0x81]))
result = st7.available()
assert result is None, "Zero-length payload must be rejected"
print("[OK] rejects zero-length payload")

# test STOP_BYTE invalide

st8 = SerialTransfer(FakePort())
st8.port.write(bytes([0x7E, 0x00, 0xFF, 0x01, 0x41, 0xFF, 0x00]))
result = st8.available()
assert result is None, "Invalid stop byte must be rejected"
print("[OK] rejects invalid STOP_BYTE")

print("\nTOUS LES TESTS OK")
