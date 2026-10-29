/*
 * MAVLink mínimo — só o que o ESP32 precisa para conversar com o ArduPilot.
 *
 * Envia (MAVLink 1): HEARTBEAT, DISTANCE_SENSOR, REQUEST_DATA_STREAM
 * Lê (MAVLink 1 e 2): HEARTBEAT, SYS_STATUS, GPS_RAW_INT,
 *                     GLOBAL_POSITION_INT, VFR_HUD
 *
 * Não depende do Arduino, para poder ser testado no computador.
 */
#pragma once
#include <stdint.h>
#include <string.h>

namespace mav {

const uint8_t MEU_SYSID = 1;     // mesmo sistema do avião
const uint8_t MEU_COMPID = 158;  // MAV_COMP_ID_PERIPHERAL

const uint32_t MSG_HEARTBEAT = 0;
const uint32_t MSG_SYS_STATUS = 1;
const uint32_t MSG_GPS_RAW_INT = 24;
const uint32_t MSG_GLOBAL_POSITION_INT = 33;
const uint32_t MSG_REQUEST_DATA_STREAM = 66;
const uint32_t MSG_VFR_HUD = 74;
const uint32_t MSG_DISTANCE_SENSOR = 132;

// Orientação do sensor de distância (MAV_SENSOR_ORIENTATION)
const uint8_t ORIENT_FRENTE = 0;
const uint8_t ORIENT_BAIXO = 25;

inline int crcExtra(uint32_t msgid) {
  switch (msgid) {
    case MSG_HEARTBEAT: return 50;
    case MSG_SYS_STATUS: return 124;
    case MSG_GPS_RAW_INT: return 24;
    case MSG_GLOBAL_POSITION_INT: return 104;
    case MSG_REQUEST_DATA_STREAM: return 148;
    case MSG_VFR_HUD: return 20;
    case MSG_DISTANCE_SENSOR: return 85;
    default: return -1;
  }
}

inline void crcAcumular(uint8_t b, uint16_t &crc) {
  uint8_t tmp = b ^ (uint8_t)(crc & 0xFF);
  tmp ^= (uint8_t)(tmp << 4);
  crc = (crc >> 8) ^ ((uint16_t)tmp << 8) ^ ((uint16_t)tmp << 3) ^ (tmp >> 4);
}

// Monta um pacote MAVLink 1 em `saida` (mínimo 8 + len bytes). Retorna o tamanho.
inline size_t montar(uint8_t *saida, uint8_t msgid, const uint8_t *payload, uint8_t len) {
  static uint8_t seq = 0;
  saida[0] = 0xFE;
  saida[1] = len;
  saida[2] = seq++;
  saida[3] = MEU_SYSID;
  saida[4] = MEU_COMPID;
  saida[5] = msgid;
  memcpy(saida + 6, payload, len);

  uint16_t crc = 0xFFFF;
  for (size_t i = 1; i < 6u + len; i++) crcAcumular(saida[i], crc);
  crcAcumular((uint8_t)crcExtra(msgid), crc);
  saida[6 + len] = crc & 0xFF;
  saida[7 + len] = crc >> 8;
  return 8 + len;
}

inline size_t heartbeat(uint8_t *saida) {
  uint8_t p[9] = {0};
  p[4] = 18;  // MAV_TYPE_ONBOARD_CONTROLLER
  p[5] = 8;   // MAV_AUTOPILOT_INVALID
  p[7] = 4;   // MAV_STATE_ACTIVE
  p[8] = 3;   // versão do MAVLink
  return montar(saida, MSG_HEARTBEAT, p, sizeof(p));
}

inline size_t distanceSensor(uint8_t *saida, uint32_t tempoMs, uint16_t minCm,
                             uint16_t maxCm, uint16_t distCm, uint8_t orientacao) {
  uint8_t p[14] = {0};
  memcpy(p + 0, &tempoMs, 4);
  memcpy(p + 4, &minCm, 2);
  memcpy(p + 6, &maxCm, 2);
  memcpy(p + 8, &distCm, 2);
  p[10] = 0;  // MAV_DISTANCE_SENSOR_LASER
  p[11] = 0;  // id do sensor
  p[12] = orientacao;
  p[13] = 255;  // covariância desconhecida
  return montar(saida, MSG_DISTANCE_SENSOR, p, sizeof(p));
}

// Pede para o ArduPilot mandar todos os dados neste canal, `hz` vezes por segundo
inline size_t pedirDados(uint8_t *saida, uint16_t hz) {
  uint8_t p[6] = {0};
  memcpy(p + 0, &hz, 2);
  p[2] = 1;  // sistema alvo
  p[3] = 1;  // componente alvo (piloto automático)
  p[4] = 0;  // MAV_DATA_STREAM_ALL
  p[5] = 1;  // ligar
  return montar(saida, MSG_REQUEST_DATA_STREAM, p, sizeof(p));
}

// Dados do avião que interessam para a telemetria
struct Aviao {
  bool viuHeartbeat = false;
  bool armado = false;
  uint32_t modo = 0;
  uint16_t bateriaMv = 0;
  int16_t correnteCa = -1;  // centiampères
  int8_t bateriaPct = -1;
  uint8_t gpsFix = 0;
  uint8_t satelites = 0;
  int32_t lat = 0, lon = 0;   // graus * 1e7
  int32_t altRelMm = 0;
  uint16_t rumoCdeg = 0;
  float velAr = 0, velSolo = 0;
  uint16_t acelerador = 0;
  uint32_t ultimaPosicaoMs = 0;
};

// Lê bytes vindos da controladora e atualiza `Aviao`
class Leitor {
 public:
  // Retorna o msgid quando um pacote válido do piloto automático foi lido, senão -1
  long receber(uint8_t b, Aviao &a, uint32_t agoraMs) {
    if (idx >= sizeof(buf)) idx = 0;
    buf[idx++] = b;

    while (idx > 0) {
      if (buf[0] != 0xFE && buf[0] != 0xFD) { descartar(1); continue; }
      if (idx < 3) return -1;

      size_t total = (buf[0] == 0xFE ? 8 : 12) + buf[1];
      if (buf[0] == 0xFD && (buf[2] & 0x01)) total += 13;  // assinatura
      if (idx < total) return -1;

      long r = decodificar(a, agoraMs);
      if (r == DESCONHECIDA) {
        // Sem CRC para conferir: só aceita se logo depois começar outro pacote
        if (idx == total) return -1;
        if (buf[total] != 0xFE && buf[total] != 0xFD) { descartar(1); continue; }
        r = -1;
      }
      if (r == CRC_INVALIDO) { descartar(1); continue; }  // falso início: ressincroniza
      descartar(total);
      return r;
    }
    return -1;
  }

 private:
  static const long CRC_INVALIDO = -2;
  static const long DESCONHECIDA = -3;
  uint8_t buf[300];
  size_t idx = 0;

  void descartar(size_t n) {
    memmove(buf, buf + n, idx - n);
    idx -= n;
  }

  static uint32_t u32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
  static int32_t i32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
  static uint16_t u16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }
  static int16_t i16(const uint8_t *p) { int16_t v; memcpy(&v, p, 2); return v; }
  static float f32(const uint8_t *p) { float v; memcpy(&v, p, 4); return v; }

  long decodificar(Aviao &a, uint32_t agoraMs) {
    bool v2 = buf[0] == 0xFD;
    size_t cab = v2 ? 10 : 6;
    uint8_t len = buf[1];
    uint8_t compid = v2 ? buf[6] : buf[4];
    uint32_t msgid = v2 ? (buf[7] | (buf[8] << 8) | ((uint32_t)buf[9] << 16)) : buf[5];

    int extra = crcExtra(msgid);
    if (extra < 0) return DESCONHECIDA;  // mensagem que não usamos

    uint16_t crc = 0xFFFF;
    for (size_t i = 1; i < cab + len; i++) crcAcumular(buf[i], crc);
    crcAcumular((uint8_t)extra, crc);
    if (crc != (buf[cab + len] | (buf[cab + len + 1] << 8))) return CRC_INVALIDO;
    if (compid != 1) return -1;  // só o piloto automático interessa

    // MAVLink 2 corta zeros do fim do pacote; completa com zeros
    uint8_t p[64] = {0};
    memcpy(p, buf + cab, len < sizeof(p) ? len : sizeof(p));

    switch (msgid) {
      case MSG_HEARTBEAT:
        if (p[5] != 3) return -1;  // MAV_AUTOPILOT_ARDUPILOTMEGA
        a.viuHeartbeat = true;
        a.modo = u32(p + 0);
        a.armado = p[6] & 0x80;  // MAV_MODE_FLAG_SAFETY_ARMED
        break;
      case MSG_SYS_STATUS:
        a.bateriaMv = u16(p + 14);
        a.correnteCa = i16(p + 16);
        a.bateriaPct = (int8_t)p[30];
        break;
      case MSG_GPS_RAW_INT:
        a.gpsFix = p[28];
        a.satelites = p[29];
        break;
      case MSG_GLOBAL_POSITION_INT:
        a.lat = i32(p + 4);
        a.lon = i32(p + 8);
        a.altRelMm = i32(p + 16);
        a.rumoCdeg = u16(p + 26);
        a.ultimaPosicaoMs = agoraMs;
        break;
      case MSG_VFR_HUD:
        a.velAr = f32(p + 0);
        a.velSolo = f32(p + 4);
        a.acelerador = u16(p + 18);
        break;
    }
    return (long)msgid;
  }
};

// Nome do modo de voo do ArduPlane
inline const char *nomeModo(uint32_t modo) {
  switch (modo) {
    case 0: return "MANUAL";
    case 1: return "CIRCLE";
    case 2: return "STABILIZE";
    case 3: return "TRAINING";
    case 4: return "ACRO";
    case 5: return "FBWA";
    case 6: return "FBWB";
    case 7: return "CRUISE";
    case 8: return "AUTOTUNE";
    case 10: return "AUTO";
    case 11: return "RTL";
    case 12: return "LOITER";
    case 13: return "TAKEOFF";
    case 15: return "GUIDED";
    default: return "OUTRO";
  }
}

}  // namespace mav
