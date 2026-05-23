# Equivalent Python de la librairie Arduino SerialTransfer (PowerBroker2)
# Utilise COBS (Consistent Overhead Byte Stuffing) + CRC8 pour l'integrite
# des trames entre le Raspberry Pi et l'Arduino Mega


class SerialTransfer:
    START_BYTE      = 0x7E
    STOP_BYTE       = 0x81
    MAX_PACKET_SIZE = 0xFE  # 254 bytes max de payload

    # IDs des packets (miroir de pmul2-com.h)
    PID_PING          = 0x00  # diag
    PID_LOCAL_ORDER   = 0x04  # Arduino vers Pi: commande keypad
    PID_COLOR_LIST    = 0x05  # Pi vers Arduino: couleurs actives
    PID_COMPLETED_COUNT = 0x06  # Pi vers Arduino: nb commandes completes
    PID_ITEM_INFO     = 0x10  # Pi vers Arduino: info sur le bloc scanne
    PID_SCAN_RESULT   = 0x11  # Arduino vers Pi: resultat du tri
    PID_SENSOR_STATUS = 0x12  # Arduino vers Pi: etat capteurs IR
    PID_STATUS        = 0xFE  # status Arduino

    # Codes de status internes (miroir de pmul2-com.cpp)
    STATUS_READY       = 0x00
    STATUS_BUSY        = 0x01
    STATUS_DONE        = 0x02
    STATUS_SCAN_NEEDED = 0x03  # bloc en position, scanne !

    # Status de parsing (miroir de Packet.h)
    NEW_DATA = 2

    def __init__(self, port):
        """
        port: objet serial.Serial deja ouvert
        """
        self.port = port
        self._buf = bytearray()  # buffer interne pour les bytes partiels
        self._generate_crc_table()

    # CRC8 (polynome 0x9B, identique a PacketCRC.h)

    def _generate_crc_table(self):
        table = [0] * 256
        poly = 0x9B
        for i in range(256):
            curr = i
            for _ in range(8):
                if curr & 0x80:
                    curr = (curr << 1) ^ poly
                else:
                    curr <<= 1
            table[i] = curr & 0xFF
        self._crc_table = table

    def _crc8(self, data):
        crc = 0
        for b in data:
            crc = self._crc_table[crc ^ b]
        return crc

    # COBS encode/decode (identique a Packet.cpp)

    def _cobs_encode(self, data):
        # data est modifie en place ! (comme stuffPacket en C++)
        overhead = 0xFF
        for i, b in enumerate(data):
            if b == self.START_BYTE:
                overhead = i
                break

        # trouve le dernier START_BYTE dans le payload
        ref_byte = -1
        for i in range(len(data) - 1, -1, -1):
            if data[i] == self.START_BYTE:
                ref_byte = i
                break

        if ref_byte != -1:
            # parcours inverse pour remplacer les START_BYTE par des deltas
            for i in range(len(data) - 1, -1, -1):
                if data[i] == self.START_BYTE:
                    data[i] = ref_byte - i
                    ref_byte = i

        return overhead

    def _cobs_decode(self, data, overhead):
        # data est modifie en place ! (comme unpackPacket en C++)
        if overhead <= self.MAX_PACKET_SIZE:
            test_index = overhead
            while data[test_index]:
                delta = data[test_index]
                data[test_index] = self.START_BYTE
                test_index += delta
            data[test_index] = self.START_BYTE

    # API publique

    def send(self, packet_id, payload):
        """
        Envoie un packet avec l'ID et le payload donnés.
        payload: bytes ou liste d'entiers
        """
        payload = list(payload)  # copie pour pas muter l'original

        if len(payload) > self.MAX_PACKET_SIZE:
            payload = payload[:self.MAX_PACKET_SIZE]

        # COBS stuff le payload
        overhead = self._cobs_encode(payload)

        # CRC8 sur le payload COBS-stuffe
        crc_val = self._crc8(payload)

        # Construction de la trame: [START][PID][overhead][len][payload][CRC][STOP]
        packet = bytes([self.START_BYTE, packet_id, overhead, len(payload)])
        packet += bytes(payload)
        packet += bytes([crc_val, self.STOP_BYTE])

        self.port.write(packet)
        self.port.flush()

    def available(self):
        """
        Verifie si un packet complet est disponible.
        Retourne (packet_id, payload_decode) ou None.
        """
        # aspire tous les bytes dispo dans le buffer interne
        while self.port.in_waiting:
            b = self.port.read(1)
            if b:
                self._buf.append(b[0])

        # minimum: START(1) + PID(1) + overhead(1) + len(1) + CRC(1) + STOP(1) = 6
        if len(self._buf) < 6:
            return None

        # cherche le START_BYTE (si on est desynchro, on jette ce qu'il y a avant)
        start_idx = -1
        for i in range(len(self._buf)):
            if self._buf[i] == self.START_BYTE:
                start_idx = i
                break

        if start_idx == -1:
            self._buf.clear()
            return None

        if start_idx > 0:
            del self._buf[:start_idx]

        if len(self._buf) < 6:
            return None

        packet_id  = self._buf[1]
        overhead   = self._buf[2]
        payload_len = self._buf[3]

        if payload_len == 0 or payload_len > self.MAX_PACKET_SIZE:
            del self._buf[0]  # on jette ce byte foireux et on retente
            return None

        total_len = 4 + payload_len + 2  # header + payload + postamble
        if len(self._buf) < total_len:
            return None  # pas encore tout recu

        payload  = list(self._buf[4:4 + payload_len])
        crc_rx   = self._buf[4 + payload_len]
        stop_byte = self._buf[4 + payload_len + 1]

        # on consomme les bytes de cette trame
        del self._buf[:total_len]

        if stop_byte != self.STOP_BYTE:
            return None

        # verification CRC
        calc_crc = self._crc8(bytes(payload))
        if calc_crc != crc_rx:
            return None

        # COBS decode
        self._cobs_decode(payload, overhead)

        # TADAAAA ! du loot
        return (packet_id, bytes(payload))
