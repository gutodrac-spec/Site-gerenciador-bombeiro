// Robô carrinho: 1 motor DC via 2 relés (ponte H de relé) + servo de direção
// + sensor ultrassônico HC-SR04. Recebe comandos do PC pela serial (115200).
//
// Comandos (um por linha):
//   FRENTE <ms>   anda para frente (máx 3000 ms), para sozinho se obstáculo < 20 cm
//   TRAS <ms>     anda de ré (máx 3000 ms)
//   PARAR 0       para o motor
//   VIRAR <graus> direção: -40 (um lado) .. 0 (reto) .. +40 (outro lado)
//   LUZ <0|1>     relé 3
//   EXTRA <0|1>   relé 4
//   DIST 0        só lê a distância
// Resposta: "OK DIST <cm>" ou "ERRO <motivo>"

#include <Servo.h>

const int TRIG = 22, ECHO = 23;
const int R_A = 30, R_B = 31, R_LUZ = 32, R_EXTRA = 33; // IN1..IN4 do módulo de relés
const int PINO_SERVO = 44;
const int CENTRO = 90;          // ajuste até as rodas ficarem retas
const int VIRADA_MAX = 40;
const int DIST_SEGURANCA = 20;  // cm
const int TEMPO_MAX = 3000;     // ms

Servo direcao;

long distancia() {
  digitalWrite(TRIG, LOW); delayMicroseconds(2);
  digitalWrite(TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG, LOW);
  long d = pulseIn(ECHO, HIGH, 30000) / 58;
  return d == 0 ? 400 : d; // sem eco = longe
}

// 1 frente, -1 ré, 0 parado. Módulo de relés é ativo em LOW.
void motor(int sentido) {
  // desliga os dois antes de inverter, para não haver troca brusca
  digitalWrite(R_A, HIGH);
  digitalWrite(R_B, HIGH);
  if (sentido == 0) return;
  delay(20);
  digitalWrite(sentido == 1 ? R_A : R_B, LOW);
}

bool andar(int sentido, int ms) {
  ms = constrain(ms, 0, TEMPO_MAX);
  if (sentido == 1 && distancia() < DIST_SEGURANCA) return false;
  motor(sentido);
  unsigned long inicio = millis();
  bool completo = true;
  while (millis() - inicio < (unsigned long)ms) {
    if (sentido == 1 && distancia() < DIST_SEGURANCA) { completo = false; break; }
    if (Serial.available() && Serial.peek() == 'P') break; // PARAR interrompe
    delay(30);
  }
  motor(0);
  return completo;
}

void setup() {
  int reles[] = {R_A, R_B, R_LUZ, R_EXTRA};
  for (int r : reles) { digitalWrite(r, HIGH); pinMode(r, OUTPUT); } // evita clique ao ligar
  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);
  direcao.attach(PINO_SERVO);
  direcao.write(CENTRO);
  Serial.begin(115200);
  Serial.setTimeout(50);
  Serial.println("PRONTO");
}

void loop() {
  if (!Serial.available()) return;
  String linha = Serial.readStringUntil('\n');
  linha.trim();
  if (linha.length() == 0) return;

  int esp = linha.indexOf(' ');
  String cmd = esp < 0 ? linha : linha.substring(0, esp);
  int v = esp < 0 ? 0 : linha.substring(esp + 1).toInt();
  cmd.toUpperCase();

  if (cmd == "FRENTE") {
    if (!andar(1, v)) { Serial.print("OBSTACULO DIST "); Serial.println(distancia()); return; }
  }
  else if (cmd == "TRAS")  andar(-1, v);
  else if (cmd == "PARAR") motor(0);
  else if (cmd == "VIRAR") { direcao.write(CENTRO + constrain(v, -VIRADA_MAX, VIRADA_MAX)); delay(250); }
  else if (cmd == "LUZ")   digitalWrite(R_LUZ, v ? LOW : HIGH);
  else if (cmd == "EXTRA") digitalWrite(R_EXTRA, v ? LOW : HIGH);
  else if (cmd == "DIST")  { /* só responde */ }
  else { Serial.print("ERRO comando desconhecido: "); Serial.println(cmd); return; }

  Serial.print("OK DIST ");
  Serial.println(distancia());
}
