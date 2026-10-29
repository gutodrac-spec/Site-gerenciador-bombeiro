# Robô carrinho controlado pelo Claude

Carrinho com **1 motor DC** (tração traseira) ligado por **relés**, **servo** na direção
dianteira e **sensor ultrassônico HC-SR04** na frente. O PC roda um script Python que
conversa com o Claude; o Claude decide os movimentos e o script manda comandos ao
Arduino Mega pela USB.

```
Você (texto no PC)
      ↓
robo_claude.py  ←→  API do Claude (ferramentas: mover, virar, medir_distancia, luz)
      ↓ USB serial, 115200
Arduino Mega → relés (motor/luz), servo (direção), HC-SR04 (distância)
```

## Peças

| Peça | Uso |
|---|---|
| Arduino Mega 2560 | controle do hardware |
| Módulo de 4 relés | relés 1 e 2: motor (frente/ré); relé 3: luz; relé 4: livre |
| 1 motor DC **com redução** (ex.: motor amarelo TT) | tração |
| Servo (SG90/MG90S) | direção |
| HC-SR04 | distância à frente |
| Bateria + regulador step-down 5 V | motor e servo |
| Capacitor cerâmico 100 nF ("104") | nos terminais do motor, contra ruído |
| ESP32 | fase 2: comunicação Wi-Fi |

## Ligações

### Motor — ponte H com 2 relés

```
        Relé 1                     Relé 2
  NO  ── Bateria +           NO  ── Bateria +
  COM ── Motor fio A         COM ── Motor fio B
  NC  ── Bateria −           NC  ── Bateria −
```

| Relé 1 | Relé 2 | Motor |
|---|---|---|
| off | off | parado (freio) |
| **on** | off | frente |
| off | **on** | ré |
| on | on | parado |

Nenhuma combinação causa curto na bateria. Sem controle de velocidade: por isso o motor
precisa ter redução. Se o carrinho andar para trás no comando FRENTE, inverta os fios do motor.

### Pinos do Arduino Mega

| Ligação | Pino |
|---|---|
| Relé IN1 (motor A) | 30 |
| Relé IN2 (motor B) | 31 |
| Relé IN3 (luz) | 32 |
| Relé IN4 (extra) | 33 |
| Relé VCC / GND | 5V / GND |
| Servo sinal (laranja) | 44 |
| Servo + / − | **5 V do regulador** / GND |
| HC-SR04 TRIG / ECHO | 22 / 23 |
| HC-SR04 VCC / GND | 5V / GND |

**Importante**
- Não alimente o servo pelo 5V do Arduino: ele puxa corrente e reinicia a placa.
- Todos os GND (Arduino, regulador, bateria −, relés) ligados juntos.
- Use apenas baixa tensão (bateria). Nada de tomada nos relés neste projeto.

## Instalação

### Arduino
1. Abra `arduino/robo/robo.ino` na Arduino IDE.
2. Placa: **Arduino Mega or Mega 2560**. Envie o código.
3. No Monitor Serial (115200, fim de linha "Nova linha") deve aparecer `PRONTO`.
4. Ajuste `CENTRO` no código até as rodas ficarem retas com `VIRAR 0`.

### PC
```bash
cd pc
pip install -r requirements.txt
export ANTHROPIC_API_KEY=sua_chave      # Windows (PowerShell): $env:ANTHROPIC_API_KEY="sua_chave"
```

Feche o Monitor Serial da Arduino IDE antes de rodar (só um programa usa a porta por vez).

```bash
python robo_claude.py --simular              # testa o Claude sem Arduino
python robo_claude.py --porta COM3 --manual  # testa o hardware digitando comandos
python robo_claude.py --porta COM3           # Claude no controle
```

No Linux a porta costuma ser `/dev/ttyACM0`.

Exemplos para falar com o robô:
- "ande um pouco para frente e me diga a distância"
- "faça uma curva para a esquerda"
- "dê ré, vire as rodas e manobre para desviar da parede"

## Comandos seriais (protocolo)

| Comando | Efeito |
|---|---|
| `FRENTE <ms>` | anda para frente (máx 3000 ms); para se obstáculo < 20 cm |
| `TRAS <ms>` | anda de ré (máx 3000 ms) |
| `PARAR 0` | para o motor |
| `VIRAR <graus>` | direção de -40 a +40 (0 = reto) |
| `LUZ <0/1>` | relé 3 |
| `EXTRA <0/1>` | relé 4 |
| `DIST 0` | só lê a distância |

Resposta: `OK DIST <cm>`, `OBSTACULO DIST <cm>` ou `ERRO ...`.

## Ordem de testes
1. Servo sozinho (`VIRAR -30`, `VIRAR 0`, `VIRAR 30`).
2. Relés **sem motor**, só ouvindo o clique (`FRENTE 500`, `TRAS 500`, `LUZ 1`).
3. Motor com o carrinho **suspenso** (rodas no ar).
4. No chão, em modo `--manual`.
5. Com o Claude.

## Impressão 3D

Busque no Printables/Thingiverse: **"servo steering RC car"**, **"Arduino car single motor servo steering"**.

- **Eixo dianteiro** com direção e encaixe para o servo.
- **Suporte traseiro** para o motor DC (roda direto no eixo do motor TT).
- **Base** com furação do Mega (101,6 × 53,3 mm) e espaço para bateria e relés.
- **Suporte frontal** do HC-SR04.

Sugestão: PLA ou PETG, preenchimento 20–30%, 3 paredes.

## Próximas fases
1. **Wi-Fi**: ESP32 ligado ao `Serial1` do Mega (pinos 18/19), com conversor de nível 5 V → 3,3 V no RX do ESP32.
2. **Visão**: ESP32-CAM ou celular enviando fotos para o Claude.
3. **Voz** no PC.
