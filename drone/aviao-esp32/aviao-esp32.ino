/*
 * Drone solar dos bombeiros — ESP32 de bordo
 *
 * Placa: LilyGO TTGO LoRa32 V2.1 (ESP32 + rádio LoRa SX1276 915 MHz)
 * Bibliotecas: "LoRa" de Sandeep Mistry e "Adafruit MLX90640"
 *
 * O que este ESP32 faz (quem pilota é a controladora com ArduPilot):
 *   1. Lê o LiDAR e repassa a distância para o ArduPilot (MAVLink)
 *   2. Lê da controladora: posição GPS, altitude, velocidade, bateria, modo
 *   3. Mede a energia que as placas solares estão gerando (INA226)
 *   4. Controla a GoPro HERO7 pelo Wi-Fi: grava sozinha ao armar o motor
 *   5. Manda tudo por rádio LoRa para a base e recebe comandos dela
 *   6. IA: procura focos de incêndio com a câmera térmica e manda o avião
 *      circular sobre eles (veja ia_fogo.h)
 *
 * Núcleo 1 (loop): LiDAR e MAVLink — nunca pode travar
 * Núcleo 0 (tarefas): GoPro (Wi-Fi), rádio LoRa e sensores I2C com a IA
 *
 * Configuração: arquivo config.h
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <Wire.h>
#include <LoRa.h>

#include "config.h"
#include "mavlink_mini.h"
#include "ia_fogo.h"
#if USAR_TERMICA
#include <Adafruit_MLX90640.h>
#endif

HardwareSerial SerialFC(1);
HardwareSerial SerialLidar(2);

// ---------- Estado compartilhado entre os dois núcleos ----------
portMUX_TYPE trava = portMUX_INITIALIZER_UNLOCKED;

struct Lidar {
  uint16_t cm = 0;
  bool valido = false;       // false = sem retorno (longe demais ou céu)
  uint32_t ultimaMs = 0;     // última leitura recebida (0 = nunca)
};

struct Solar {
  uint16_t mv = 0;
  int16_t ma = 0;
  bool ok = false;
};

enum EstadoGoPro : uint8_t { GOPRO_DESCONECTADA = 0, GOPRO_PARADA = 1, GOPRO_GRAVANDO = 2 };

struct GoPro {
  EstadoGoPro estado = GOPRO_DESCONECTADA;
  int8_t bateria = -1;
  int16_t segundos = 0;
};

mav::Aviao aviao;
Lidar lidar;
Solar solar;
GoPro gopro;
uint32_t ultimoHeartbeatFcMs = 0;

volatile bool pedidoGravar = false;
volatile bool pedidoParar = false;
volatile bool pedidoFoto = false;
volatile bool pedidoRetomar = false;           // base: voltar à missão
volatile bool iaControle = IA_PODE_CONTROLAR;  // base pode ligar/desligar

// Pedidos da IA para o loop mandar à controladora (só o loop escreve na serial)
struct PedidoVoo {
  bool irPara = false;
  bool voltarMissao = false;
  int32_t lat = 0, lon = 0;
  float altM = 0;
};
PedidoVoo pedidoVoo;

// O que a IA está fazendo (para a telemetria)
struct EstadoIA {
  ia::Estado estado = ia::DESLIGADA;
  ia::Foco foco;
};
EstadoIA estadoIA;
bool loraOk = false;  // sem rádio, a telemetria só aparece no Monitor Serial

// =================================================================
//                        LiDAR (TF-Luna / TFmini)
// =================================================================
// Pacote de 9 bytes: 0x59 0x59 dist_L dist_H força_L força_H temp_L temp_H soma
void lerLidar() {
  static uint8_t f[9];
  static uint8_t n = 0;

  while (SerialLidar.available()) {
    uint8_t b = SerialLidar.read();
    if (n < 2) {
      if (b == 0x59) f[n++] = b; else n = 0;
      continue;
    }
    f[n++] = b;
    if (n < 9) continue;
    n = 0;

    uint8_t soma = 0;
    for (int i = 0; i < 8; i++) soma += f[i];
    if (soma != f[8]) continue;

    uint16_t dist = f[2] | (f[3] << 8);
    uint16_t forca = f[4] | (f[5] << 8);
    bool valido = forca >= 100 && forca != 0xFFFF && dist >= LIDAR_MIN_CM && dist <= LIDAR_MAX_CM;

    portENTER_CRITICAL(&trava);
    lidar.cm = dist;
    lidar.valido = valido;
    lidar.ultimaMs = millis();
    portEXIT_CRITICAL(&trava);
  }
}

// =================================================================
//                   MAVLink com a controladora de voo
// =================================================================
mav::Leitor leitorMav;
mav::Aviao aviaoLocal;

void enviarFC(const uint8_t *pacote, size_t tam) { SerialFC.write(pacote, tam); }

void lerControladora() {
  while (SerialFC.available()) {
    long msg = leitorMav.receber(SerialFC.read(), aviaoLocal, millis());
    if (msg < 0) continue;
    portENTER_CRITICAL(&trava);
    aviao = aviaoLocal;
    if (msg == mav::MSG_HEARTBEAT) ultimoHeartbeatFcMs = millis();
    portEXIT_CRITICAL(&trava);
  }
}

void enviarPedidosDaIA() {
  PedidoVoo p;
  portENTER_CRITICAL(&trava);
  p = pedidoVoo;
  pedidoVoo.irPara = pedidoVoo.voltarMissao = false;
  portEXIT_CRITICAL(&trava);

  uint8_t pacote[48];
  if (p.irPara) {
    enviarFC(pacote, mav::irPara(pacote, p.lat, p.lon, p.altM, IA_RAIO_M));
    Serial.printf("[IA] Circulando foco em %.6f, %.6f a %.0f m\n", p.lat / 1e7, p.lon / 1e7, p.altM);
  }
  if (p.voltarMissao) {
    enviarFC(pacote, mav::mudarModo(pacote, mav::MODO_AUTO));
    Serial.println("[IA] Voltando para a missão (AUTO)");
  }
}

void enviarDistancia() {
  Lidar l;
  portENTER_CRITICAL(&trava);
  l = lidar;
  portEXIT_CRITICAL(&trava);

  // LiDAR parou de mandar dados: não envia nada, o ArduPilot marca como falha
  if (l.ultimaMs == 0 || millis() - l.ultimaMs > 300) return;

  // Sem retorno = além do alcance
  uint16_t cm = l.valido ? l.cm : LIDAR_MAX_CM + 1;
  uint8_t orient = LIDAR_PARA_BAIXO ? mav::ORIENT_BAIXO : mav::ORIENT_FRENTE;
  uint8_t pacote[32];
  enviarFC(pacote, mav::distanceSensor(pacote, millis(), LIDAR_MIN_CM, LIDAR_MAX_CM, cm, orient));
}

// =================================================================
//                    INA226 — energia das placas solares
// =================================================================
bool lerRegistroINA(uint8_t reg, uint16_t &valor) {
  Wire.beginTransmission(INA226_ENDERECO);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom((uint8_t)INA226_ENDERECO, (uint8_t)2) != 2) return false;
  uint8_t alto = Wire.read();
  uint8_t baixo = Wire.read();
  valor = (alto << 8) | baixo;
  return true;
}

void lerSolar() {
  uint16_t shunt, barramento;
  bool ok = lerRegistroINA(0x01, shunt) && lerRegistroINA(0x02, barramento);

  portENTER_CRITICAL(&trava);
  solar.ok = ok;
  if (ok) {
    solar.mv = barramento * 1.25;                                 // 1,25 mV por bit
    solar.ma = (int16_t)shunt * 0.0025 / INA226_SHUNT_OHMS;       // 2,5 µV por bit
  }
  portEXIT_CRITICAL(&trava);
}

// =================================================================
//            Sensores I2C + IA de focos de incêndio (núcleo 0)
// =================================================================
#if USAR_TERMICA
Adafruit_MLX90640 termica;
float quadro[ia::PIXELS];
#endif

void tarefaSensores(void *) {
  ia::Piloto piloto;
  piloto.cfg.limiteC = IA_LIMITE_C;
  piloto.cfg.contrasteC = IA_CONTRASTE_C;
  piloto.cfg.altMinM = IA_ALT_MIN_M;
  piloto.cfg.bateriaMinPct = IA_BATERIA_MIN_PCT;
  piloto.cfg.raioM = IA_RAIO_M;
  piloto.cfg.circularMaxMs = IA_CIRCULAR_MAX_S * 1000UL;
  piloto.cfg.esperaMs = IA_ESPERA_S * 1000UL;
  piloto.cfg.inverterLinhas = IA_INVERTER_LINHAS;
  piloto.cfg.inverterColunas = IA_INVERTER_COLUNAS;

#if USAR_TERMICA
  if (termica.begin(MLX90640_I2CADDR_DEFAULT, &Wire)) {
    termica.setMode(MLX90640_CHESS);
    termica.setResolution(MLX90640_ADC_18BIT);
    termica.setRefreshRate(MLX90640_2_HZ);
    piloto.ligar();
    Serial.println("[IA] Câmera térmica pronta, vigiando");
  } else {
    Serial.println("[IA] Câmera térmica não encontrada: IA desligada");
  }
#endif

  const char *ultimoMotivo = "";
  for (;;) {
    if (USAR_INA226) lerSolar();

    ia::Deteccao det;
    bool quadroNovo = false;
#if USAR_TERMICA
    // Com a câmera a 2 Hz, getFrame espera o quadro ficar pronto (~0,5–1 s)
    if (piloto.estado != ia::DESLIGADA && termica.getFrame(quadro) == 0) {
      det = ia::detectar(quadro, piloto.cfg);
      quadroNovo = true;
    }
#endif
    if (piloto.estado == ia::DESLIGADA) vTaskDelay(pdMS_TO_TICKS(500));

    ia::Situacao s;
    portENTER_CRITICAL(&trava);
    s.armado = aviao.armado;
    s.modo = aviao.modo;
    s.bateriaPct = aviao.bateriaPct;
    s.gpsFix = aviao.gpsFix;
    s.pose.lat = aviao.lat / 1e7;
    s.pose.lon = aviao.lon / 1e7;
    s.pose.altM = aviao.altRelMm / 1000.0f;
    s.pose.rolagem = aviao.rolagem;
    s.pose.arfagem = aviao.arfagem;
    s.pose.rumo = aviao.rumo;
    portEXIT_CRITICAL(&trava);

    piloto.cfg.podeControlar = iaControle;
    bool retomar = pedidoRetomar;
    pedidoRetomar = false;
    ia::Acao acao = piloto.passo(millis(), quadroNovo ? &det : nullptr, s, retomar);

    portENTER_CRITICAL(&trava);
    if (acao == ia::IR_PARA_FOCO) {
      pedidoVoo.irPara = true;
      pedidoVoo.lat = (int32_t)(piloto.foco.lat * 1e7);
      pedidoVoo.lon = (int32_t)(piloto.foco.lon * 1e7);
      pedidoVoo.altM = piloto.alvoAltM;
    }
    if (acao == ia::VOLTAR_MISSAO) pedidoVoo.voltarMissao = true;
    estadoIA.estado = piloto.estado;
    estadoIA.foco = piloto.foco;
    portEXIT_CRITICAL(&trava);

    if (piloto.motivo() != ultimoMotivo) {
      ultimoMotivo = piloto.motivo();
      Serial.printf("[IA] %s\n", ultimoMotivo);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// =================================================================
//                   GoPro HERO7 pelo Wi-Fi (núcleo 0)
// =================================================================
const char *GOPRO_URL = "http://10.5.5.9/gp/gpControl";

bool comandoGoPro(const char *caminho, String *resposta) {
  HTTPClient http;
  http.setTimeout(3000);
  http.begin(String(GOPRO_URL) + caminho);
  int codigo = http.GET();
  if (resposta && codigo == HTTP_CODE_OK) *resposta = http.getString();
  http.end();
  if (codigo != HTTP_CODE_OK) Serial.printf("[GoPro] Falha em %s (%d)\n", caminho, codigo);
  return codigo == HTTP_CODE_OK;
}

// Lê um número do bloco "status" do JSON da GoPro, ex: "70":85
int lerStatusGoPro(const String &json, const char *chave) {
  int fim = json.indexOf("\"settings\"");
  String bloco = fim > 0 ? json.substring(0, fim) : json;
  String busca = String("\"") + chave + "\":";
  int pos = bloco.indexOf(busca);
  return pos < 0 ? -1 : bloco.substring(pos + busca.length()).toInt();
}

void atualizarGoPro() {
  String json;
  if (!comandoGoPro("/status", &json)) return;
  // HERO7: 10 = gravando, 13 = segundos gravados, 70 = bateria %
  bool gravando = lerStatusGoPro(json, "10") == 1;
  portENTER_CRITICAL(&trava);
  gopro.estado = gravando ? GOPRO_GRAVANDO : GOPRO_PARADA;
  gopro.segundos = lerStatusGoPro(json, "13");
  gopro.bateria = lerStatusGoPro(json, "70");
  portEXIT_CRITICAL(&trava);
  digitalWrite(PINO_LED, gravando);
}

void tarefaGoPro(void *) {
  bool armadoAntes = false;
  uint32_t desarmouEm = 0;
  uint32_t ultimoStatus = 0;

  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      portENTER_CRITICAL(&trava);
      gopro.estado = GOPRO_DESCONECTADA;
      portEXIT_CRITICAL(&trava);
      digitalWrite(PINO_LED, LOW);

      Serial.println("[GoPro] Procurando a rede da câmera...");
      WiFi.begin(GOPRO_SSID, GOPRO_SENHA);
      for (int i = 0; i < 30 && WiFi.status() != WL_CONNECTED; i++) vTaskDelay(pdMS_TO_TICKS(500));
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[GoPro] Conectada!");
        ultimoStatus = 0;
      } else {
        WiFi.disconnect();
        vTaskDelay(pdMS_TO_TICKS(2000));
      }
      continue;
    }

    portENTER_CRITICAL(&trava);
    bool armado = aviao.armado;
    portEXIT_CRITICAL(&trava);

    if (GRAVAR_AO_ARMAR) {
      if (armado && !armadoAntes) pedidoGravar = true;
      if (armado) desarmouEm = 0;
      if (!armado && armadoAntes) desarmouEm = millis();
      if (desarmouEm && millis() - desarmouEm > PARAR_APOS_DESARMAR_S * 1000UL) {
        pedidoParar = true;
        desarmouEm = 0;
      }
    }
    armadoAntes = armado;

    if (pedidoGravar) {
      pedidoGravar = false;
      comandoGoPro("/command/mode?p=0", nullptr);  // modo vídeo
      vTaskDelay(pdMS_TO_TICKS(300));
      if (comandoGoPro("/command/shutter?p=1", nullptr)) Serial.println("[GoPro] Gravando");
      ultimoStatus = 0;
    }
    if (pedidoParar) {
      pedidoParar = false;
      if (comandoGoPro("/command/shutter?p=0", nullptr)) Serial.println("[GoPro] Gravação parada");
      ultimoStatus = 0;
    }
    if (pedidoFoto) {
      pedidoFoto = false;
      if (gopro.estado == GOPRO_GRAVANDO) {
        Serial.println("[GoPro] Não dá para tirar foto gravando vídeo");
      } else {
        comandoGoPro("/command/mode?p=1", nullptr);  // modo foto
        vTaskDelay(pdMS_TO_TICKS(300));
        if (comandoGoPro("/command/shutter?p=1", nullptr)) Serial.println("[GoPro] Foto");
      }
      ultimoStatus = 0;
    }

    if (ultimoStatus == 0 || millis() - ultimoStatus > 3000) {
      vTaskDelay(pdMS_TO_TICKS(500));  // deixa a câmera processar o último comando
      atualizarGoPro();
      ultimoStatus = millis();
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

// =================================================================
//                    Rádio LoRa com a base (núcleo 0)
// =================================================================
// Linha de telemetria (CSV), na ordem:
// BMB,seq,lat,lon,alt_dm,vsolo_dms,var_dms,rumo,armado,modo,fc_ok,
// bat_mv,bat_ca,bat_pct,sats,fix,solar_mv,solar_ma,lidar_cm,obstaculo,
// gopro,gopro_bat,gopro_seg,ia,ia_controle,foco_temp,foco_lat,foco_lon,foco_idade_s
void montarTelemetria(char *linha, size_t tam, uint16_t seq) {
  mav::Aviao a;
  Lidar l;
  Solar s;
  GoPro g;
  EstadoIA e;
  uint32_t hb;
  portENTER_CRITICAL(&trava);
  e = estadoIA;
  a = aviao;
  l = lidar;
  s = solar;
  g = gopro;
  hb = ultimoHeartbeatFcMs;
  portEXIT_CRITICAL(&trava);

  bool fcOk = hb && millis() - hb < 3000;
  bool lidarOk = l.ultimaMs && millis() - l.ultimaMs < 300;
  int lidarCm = !lidarOk ? -2 : (l.valido ? l.cm : -1);  // -2 = sem sensor, -1 = sem retorno
  bool obstaculo = !LIDAR_PARA_BAIXO && lidarOk && l.valido && l.cm < OBSTACULO_ALERTA_CM;

  int focoTemp = e.foco.existe ? (int)e.foco.temp : -999;
  long focoIdade = e.foco.existe ? (long)((millis() - e.foco.quandoMs) / 1000) : -1;

  snprintf(linha, tam, "BMB,%u,%ld,%ld,%ld,%d,%d,%u,%d,%s,%d,%u,%d,%d,%u,%u,%u,%d,%d,%d,%d,%d,%d,"
           "%d,%d,%d,%ld,%ld,%ld",
           seq, (long)a.lat, (long)a.lon, (long)(a.altRelMm / 100),
           (int)(a.velSolo * 10), (int)(a.velAr * 10), a.rumoCdeg / 100,
           a.armado, mav::nomeModo(a.modo), fcOk,
           a.bateriaMv, a.correnteCa, a.bateriaPct, a.satelites, a.gpsFix,
           s.ok ? s.mv : 0, s.ok ? s.ma : 0, lidarCm, obstaculo,
           g.estado, g.bateria, g.segundos,
           e.estado, (int)iaControle, focoTemp,
           (long)(e.foco.lat * 1e7), (long)(e.foco.lon * 1e7), focoIdade);
}

// Comandos da base: "CMD,<chave>,<letra>,<id>"
//   G gravar | P parar | F foto | M voltar à missão | A IA pode controlar | D IA só avisa
// A base repete cada comando 3 vezes; o id evita executar de novo.
void tratarComando(const String &msg) {
  static long ultimoId = -1;
  String prefixo = String("CMD,") + LORA_CHAVE + ",";
  if (!msg.startsWith(prefixo)) return;
  char c = msg.charAt(prefixo.length());
  long id = msg.substring(prefixo.length() + 2).toInt();
  if (id == ultimoId) return;
  ultimoId = id;
  Serial.printf("[LoRa] Comando da base: %c (RSSI %d)\n", c, LoRa.packetRssi());
  if (c == 'G') pedidoGravar = true;
  if (c == 'P') pedidoParar = true;
  if (c == 'F') pedidoFoto = true;
  if (c == 'M') pedidoRetomar = true;
  if (c == 'A') iaControle = true;
  if (c == 'D') iaControle = false;
}

void tarefaRadio(void *) {
  uint32_t ultimoEnvio = 0;
  uint16_t seq = 0;
  char linha[240];

  for (;;) {
    if (loraOk && LoRa.parsePacket() > 0) {
      String msg;
      while (LoRa.available()) msg += (char)LoRa.read();
      tratarComando(msg);
    }

    if (millis() - ultimoEnvio >= TELEMETRIA_INTERVALO_MS) {
      ultimoEnvio = millis();
      montarTelemetria(linha, sizeof(linha), seq++);
      if (loraOk) {
        LoRa.beginPacket();
        LoRa.print(linha);
        LoRa.endPacket();
      }
      Serial.println(linha);
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// =================================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Drone solar — ESP32 de bordo ===");
  pinMode(PINO_LED, OUTPUT);

  SerialFC.setRxBufferSize(2048);
  SerialFC.begin(FC_BAUD, SERIAL_8N1, PINO_FC_RX, PINO_FC_TX);
  SerialLidar.setRxBufferSize(512);
  SerialLidar.begin(LIDAR_BAUD, SERIAL_8N1, PINO_LIDAR_RX, PINO_LIDAR_TX);

  if (USAR_INA226 || USAR_TERMICA) {
    Wire.begin(PINO_I2C_SDA, PINO_I2C_SCL);
    Wire.setClock(400000);
  }

  SPI.begin(PINO_LORA_SCK, PINO_LORA_MISO, PINO_LORA_MOSI, PINO_LORA_SS);
  LoRa.setPins(PINO_LORA_SS, PINO_LORA_RST, PINO_LORA_DIO0);
  loraOk = LoRa.begin(LORA_FREQ);
  if (loraOk) {
    LoRa.setSpreadingFactor(LORA_SF);
    LoRa.setSyncWord(LORA_SYNC);
    LoRa.setTxPower(LORA_POTENCIA_DBM);
    LoRa.enableCrc();
    Serial.println("[LoRa] Rádio pronto");
  } else {
    Serial.println("[LoRa] Rádio não encontrado: telemetria só no Monitor Serial");
  }
  xTaskCreatePinnedToCore(tarefaRadio, "radio", 6144, nullptr, 1, nullptr, 0);

  WiFi.mode(WIFI_STA);
  xTaskCreatePinnedToCore(tarefaGoPro, "gopro", 8192, nullptr, 1, nullptr, 0);
  // pilha grande: a IA copia o quadro térmico (3 KB) para achar a mediana
  xTaskCreatePinnedToCore(tarefaSensores, "sensores", 12288, nullptr, 1, nullptr, 0);

  uint8_t pacote[32];
  enviarFC(pacote, mav::pedirDados(pacote, 4));
}

void loop() {
  static uint32_t tDistancia = 0, tHeartbeat = 0, tPedido = 0;
  uint32_t agora = millis();

  lerControladora();
  lerLidar();

  if (agora - tDistancia >= 50) {  // 20 vezes por segundo
    tDistancia = agora;
    enviarDistancia();
  }

  uint8_t pacote[32];
  if (agora - tHeartbeat >= 1000) {
    tHeartbeat = agora;
    enviarFC(pacote, mav::heartbeat(pacote));
  }

  // Se a controladora não está mandando posição, pede de novo
  if (agora - tPedido >= 5000) {
    tPedido = agora;
    if (agora - aviaoLocal.ultimaPosicaoMs > 5000) enviarFC(pacote, mav::pedirDados(pacote, 4));
  }

  enviarPedidosDaIA();

  delay(1);
}
