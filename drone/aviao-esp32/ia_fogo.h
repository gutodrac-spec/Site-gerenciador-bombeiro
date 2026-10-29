/*
 * IA de bordo: procura focos de incêndio com a câmera térmica MLX90640
 * (32 x 24 pixels) e decide o que o avião faz.
 *
 *   VIGIANDO   -> voando a missão (modo AUTO), olhando a imagem térmica
 *   CIRCULANDO -> achou um foco: manda o ArduPilot circular sobre ele
 *                 (modo GUIDED) e avisa a base. Depois de um tempo, ou se
 *                 a base mandar, volta para a missão.
 *
 * Segurança:
 *   - só assume o controle se o avião estiver em AUTO, alto e com bateria;
 *   - nunca desce: circula na altura em que já está;
 *   - se o piloto mudar o modo no rádio, ou o ArduPilot entrar em
 *     failsafe (ex: RTL), a IA solta o controle e não insiste;
 *   - os failsafes do ArduPilot (bateria, rádio, cerca) continuam valendo.
 *
 * Não depende do Arduino, para poder ser testado no computador.
 */
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <algorithm>

namespace ia {

const int COLUNAS = 32;
const int LINHAS = 24;
const int PIXELS = COLUNAS * LINHAS;

struct Config {
  float limiteC = 70;           // temperatura mínima do foco
  float contrasteC = 25;        // quanto acima da mediana da imagem
  int confirmacoes = 3;         // quadros seguidos com foco para confirmar
  float campoLarguraGraus = 55; // MLX90640 "BAA" (55 x 35 graus)
  float campoAlturaGraus = 35;
  bool inverterLinhas = false;  // se a imagem estiver de cabeça para baixo
  bool inverterColunas = false; // se estiver espelhada
  bool podeControlar = true;    // false = só avisa a base, não mexe no voo
  float altMinM = 40;           // não assume o controle abaixo disso
  int bateriaMinPct = 35;
  float raioM = 70;             // raio da volta em cima do foco
  uint32_t circularMaxMs = 180000;
  uint32_t esperaMs = 300000;   // depois de largar um foco, quanto espera
  float distNovoFocoM = 250;    // ignora focos perto do último que circulou
};

struct Deteccao {
  bool achou = false;
  float tempMax = 0;
  float mediana = 0;
  float linha = 0, coluna = 0;  // centro do foco (pode ser fracionário)
};

// Procura o ponto mais quente e confere se ele se destaca do resto
inline Deteccao detectar(const float *quadro, const Config &cfg) {
  Deteccao d;
  float copia[PIXELS];
  memcpy(copia, quadro, sizeof(copia));
  std::nth_element(copia, copia + PIXELS / 2, copia + PIXELS);
  d.mediana = copia[PIXELS / 2];

  int maior = 0;
  for (int i = 1; i < PIXELS; i++)
    if (quadro[i] > quadro[maior]) maior = i;
  d.tempMax = quadro[maior];
  if (d.tempMax < cfg.limiteC || d.tempMax - d.mediana < cfg.contrasteC) return d;

  // Centro do foco: média dos pixels quase tão quentes quanto o máximo
  float soma = 0, sl = 0, sc = 0;
  for (int i = 0; i < PIXELS; i++) {
    float peso = quadro[i] - (d.tempMax - 5);
    if (peso <= 0) continue;
    soma += peso;
    sl += peso * (i / COLUNAS);
    sc += peso * (i % COLUNAS);
  }
  d.linha = sl / soma;
  d.coluna = sc / soma;
  d.achou = true;
  return d;
}

struct Pose {
  double lat = 0, lon = 0;        // graus
  float altM = 0;                 // acima da decolagem
  float rolagem = 0, arfagem = 0, rumo = 0;  // radianos
};

// Onde está o foco no chão. A câmera aponta para baixo; a linha 0 da
// imagem fica para o lado do nariz e a coluna 0 para a asa esquerda.
inline void localizar(const Deteccao &d, const Pose &p, const Config &cfg,
                      double &lat, double &lon) {
  const float GRAU = (float)M_PI / 180;
  float linha = cfg.inverterLinhas ? LINHAS - 1 - d.linha : d.linha;
  float coluna = cfg.inverterColunas ? COLUNAS - 1 - d.coluna : d.coluna;

  float angDireita = ((coluna + 0.5f) / COLUNAS - 0.5f) * cfg.campoLarguraGraus * GRAU;
  float angFrente = (0.5f - (linha + 0.5f) / LINHAS) * cfg.campoAlturaGraus * GRAU;
  // Asa direita baixa faz a câmera olhar para a esquerda; nariz alto, para trás
  angDireita -= p.rolagem;
  angFrente -= p.arfagem;
  const float LIMITE = 70 * GRAU;
  angDireita = std::max(-LIMITE, std::min(LIMITE, angDireita));
  angFrente = std::max(-LIMITE, std::min(LIMITE, angFrente));

  float frente = p.altM * tanf(angFrente);
  float direita = p.altM * tanf(angDireita);
  float norte = frente * cosf(p.rumo) - direita * sinf(p.rumo);
  float leste = frente * sinf(p.rumo) + direita * cosf(p.rumo);

  lat = p.lat + norte / 111320.0;
  lon = p.lon + leste / (111320.0 * cos(p.lat * M_PI / 180));
}

inline float distanciaM(double lat1, double lon1, double lat2, double lon2) {
  double n = (lat2 - lat1) * 111320.0;
  double l = (lon2 - lon1) * 111320.0 * cos(lat1 * M_PI / 180);
  return (float)sqrt(n * n + l * l);
}

enum Estado : uint8_t { DESLIGADA = 0, VIGIANDO = 1, CIRCULANDO = 2 };
enum Acao : uint8_t { NADA, IR_PARA_FOCO, VOLTAR_MISSAO };

struct Situacao {
  bool armado = false;
  uint32_t modo = 0;         // modo do ArduPlane (10 = AUTO, 15 = GUIDED)
  int bateriaPct = -1;       // -1 = desconhecida
  uint8_t gpsFix = 0;
  Pose pose;
};

struct Foco {
  bool existe = false;
  double lat = 0, lon = 0;
  float temp = 0;
  uint32_t quandoMs = 0;
};

class Piloto {
 public:
  Config cfg;
  Estado estado = DESLIGADA;
  Foco foco;                 // último foco confirmado (vai para a base)
  float alvoAltM = 0;        // altura da volta sobre o foco

  void ligar() { estado = VIGIANDO; }

  // Chamar a cada quadro térmico (det) ou, sem quadro novo, com det = nullptr.
  // retomar = a base pediu para voltar à missão.
  Acao passo(uint32_t agora, const Deteccao *det, const Situacao &s, bool retomar) {
    if (estado == DESLIGADA) return NADA;
    if (estado == VIGIANDO) return vigiar(agora, det, s);
    return circular(agora, s, retomar);
  }

  const char *motivo() const { return ultimoMotivo; }

 private:
  int seguidos = 0;
  uint32_t bloqueadoAteMs = 0;
  uint32_t inicioMs = 0, comandoMs = 0;
  int tentativas = 0;
  bool viuGuided = false;
  bool temUltimoCirculado = false;
  double ultLat = 0, ultLon = 0;
  const char *ultimoMotivo = "";

  Acao vigiar(uint32_t agora, const Deteccao *det, const Situacao &s) {
    if (!det) return NADA;
    seguidos = det->achou ? seguidos + 1 : 0;
    if (seguidos < cfg.confirmacoes) return NADA;
    seguidos = 0;
    if (s.gpsFix < 3) { ultimoMotivo = "foco visto, mas sem GPS"; return NADA; }

    localizar(*det, s.pose, cfg, foco.lat, foco.lon);
    foco.existe = true;
    foco.temp = det->tempMax;
    foco.quandoMs = agora;

    if (!cfg.podeControlar) { ultimoMotivo = "foco: só aviso (controle desligado)"; return NADA; }
    if (!s.armado || s.modo != mav_AUTO) { ultimoMotivo = "foco: avião não está em AUTO"; return NADA; }
    if (s.pose.altM < cfg.altMinM) { ultimoMotivo = "foco: avião baixo demais"; return NADA; }
    if (s.bateriaPct >= 0 && s.bateriaPct < cfg.bateriaMinPct) { ultimoMotivo = "foco: bateria baixa"; return NADA; }
    if ((int32_t)(agora - bloqueadoAteMs) < 0) { ultimoMotivo = "foco: esperando após o último"; return NADA; }
    if (temUltimoCirculado &&
        distanciaM(ultLat, ultLon, foco.lat, foco.lon) < cfg.distNovoFocoM) {
      ultimoMotivo = "foco: mesmo lugar do último";
      return NADA;
    }

    estado = CIRCULANDO;
    alvoAltM = std::max(s.pose.altM, cfg.altMinM);
    inicioMs = comandoMs = agora;
    tentativas = 1;
    viuGuided = false;
    temUltimoCirculado = true;
    ultLat = foco.lat;
    ultLon = foco.lon;
    ultimoMotivo = "foco: indo circular";
    return IR_PARA_FOCO;
  }

  Acao circular(uint32_t agora, const Situacao &s, bool retomar) {
    if (s.modo == mav_GUIDED) viuGuided = true;

    if (!s.armado) return largar(agora, NADA, "desarmou");
    if (viuGuided && s.modo != mav_GUIDED)
      return largar(agora, NADA, "piloto ou failsafe assumiu");

    if (!viuGuided && agora - comandoMs > 3000) {
      if (tentativas >= 3) return largar(agora, NADA, "ArduPilot não aceitou o comando");
      tentativas++;
      comandoMs = agora;
      return IR_PARA_FOCO;
    }
    if (retomar) return largar(agora, VOLTAR_MISSAO, "base pediu para voltar à missão");
    if (viuGuided && agora - inicioMs > cfg.circularMaxMs)
      return largar(agora, VOLTAR_MISSAO, "tempo sobre o foco acabou");
    return NADA;
  }

  Acao largar(uint32_t agora, Acao acao, const char *motivo) {
    estado = VIGIANDO;
    seguidos = 0;
    bloqueadoAteMs = agora + cfg.esperaMs;
    ultimoMotivo = motivo;
    return acao;
  }

  static const uint32_t mav_AUTO = 10;
  static const uint32_t mav_GUIDED = 15;
};

}  // namespace ia
