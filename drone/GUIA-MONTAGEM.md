# Guia de montagem — Drone solar dos bombeiros

Avião planador com placas solares na asa, piloto automático ArduPilot,
LiDAR, GoPro HERO7 Black, **IA que procura focos de incêndio** com câmera
térmica e telemetria por rádio LoRa até uma base em terra.

**Dois caminhos para o avião:**
- **Comprar pronto** em espuma (ex: Volantex Ranger 2000, item 1 da lista).
  É mais rápido, e o avião já foi testado.
- **Imprimir em 3D:** um planador de 2,6 m desenhado para este projeto, em
  [`modelo-3d/`](modelo-3d/README.md). Tem mais área para as células
  solares, mas é um projeto novo, ainda não voado.

> **Mudança em relação à primeira conversa:** troquei a bateria de 3S para
> **2S** e o carregador solar de CN3791 para **BQ24650**. O CN3791 só
> carrega uma célula, e com 3S seriam precisas ~26 células solares em
> série, que não cabem na asa. Com 2S bastam 18 a 20 células.

---

## 1. Como tudo se liga

```
                 ┌──────────── ASA ────────────┐
                 │ 18–20 células solares (série)│
                 └──────────────┬───────────────┘
                                │ ~10 V, até 5 A
                         [INA226: mede o solar]
                                │
                        [Carregador MPPT BQ24650]
                                │ 8,4 V
   ┌────────────────────────────┴──────────────────────┐
   │       Bateria Li-ion 2S3P (Molicel P42A) + BMS     │
   └──────┬──────────────────────────────┬─────────────┘
          │ XT60                          │
   [Controladora Matek + ArduPilot]   [BEC 5 V 3 A] ── USB-C ── GoPro HERO7
     │   │   │   │   │                                          ▲
   ESC  GPS Rec. Servos 5 V ──► TTGO LoRa32 (ESP32) ◄──Wi-Fi────┘
    │                              │   │
  Motor                         LiDAR  Rádio LoRa 915 MHz
                                           │
                                   ~~~~ ar ~~~~
                                           │
                     BASE: TTGO LoRa32 ── Wi-Fi ── seu celular
```

---

## 2. Lista de compras

Preços aproximados em reais, só como referência: confira antes de comprar.
Os links abrem a **busca** no Mercado Livre (ML) e no AliExpress (Ali),
porque os anúncios mudam toda hora.

### Avião e voo

| # | Item | Qtd | Preço aprox. | Links |
|---|---|---|---|---|
| 1 | Planador EPO 2 m, versão PNP (ex: **Volantex Ranger 2000**) | 1 | R$ 900–1.500 | [ML](https://lista.mercadolivre.com.br/volantex-ranger-2000) · [Ali](https://pt.aliexpress.com/w/wholesale-volantex-ranger-2000.html) |
| 2 | Controladora de voo **Matek F405-WTE** (ou H743-WING) | 1 | R$ 300–500 | [ML](https://lista.mercadolivre.com.br/matek-f405-wte) · [Ali](https://pt.aliexpress.com/w/wholesale-matek-f405-wte.html) |
| 3 | GPS com bússola **M10** (ex: Matek M10Q-5883) | 1 | R$ 150–250 | [ML](https://lista.mercadolivre.com.br/gps-m10-bussola-drone) · [Ali](https://pt.aliexpress.com/w/wholesale-matek-m10q-5883.html) |
| 4 | Sensor de velocidade do ar digital (ex: Matek ASPD-4525). Opcional, mas muito recomendado para voo automático | 1 | R$ 150–250 | [Ali](https://pt.aliexpress.com/w/wholesale-matek-aspd-4525.html) |
| 5 | Rádio controle **ExpressLRS 2.4 GHz** (ex: RadioMaster Pocket ou Boxer) + receptor ELRS | 1 | R$ 700–1.500 | [ML](https://lista.mercadolivre.com.br/radiomaster-elrs) · [Ali](https://pt.aliexpress.com/w/wholesale-radiomaster-elrs.html) |
| 6 | Motor para 2S, ~1.400–1.800 KV (só se o PNP vier para 3S) + hélice dobrável | 1 | R$ 80–150 | [Ali](https://pt.aliexpress.com/w/wholesale-2216-motor-1400kv.html) |
| 7 | Wattímetro (para medir o consumo do motor na bancada) | 1 | R$ 40–80 | [ML](https://lista.mercadolivre.com.br/wattimetro-xt60) |

> Use o rádio em **2.4 GHz**, não em 900 MHz, para não brigar com o LoRa de
> 915 MHz da telemetria.

### Energia e solar

| # | Item | Qtd | Preço aprox. | Links |
|---|---|---|---|---|
| 8 | Célula solar **SunPower Maxeon C60** 125×125 mm (compre umas 4 a mais, elas quebram fácil) | 22–24 | R$ 15–30 cada | [Ali](https://pt.aliexpress.com/w/wholesale-sunpower-c60-solar-cell.html) · [ML](https://lista.mercadolivre.com.br/celula-solar-sunpower) |
| 9 | Fita de solda para célula solar (tabbing wire) + caneta de fluxo | 1 kit | R$ 30–60 | [Ali](https://pt.aliexpress.com/w/wholesale-solar-cell-tabbing-wire.html) |
| 10 | Filme de laminação transparente, ou fita transparente larga, para proteger as células | 1 | R$ 30–80 | [ML](https://lista.mercadolivre.com.br/filme-laminacao-aeromodelo) |
| 11 | Carregador solar **MPPT BQ24650** (ajustável) | 1 | R$ 30–60 | [Ali](https://pt.aliexpress.com/w/wholesale-bq24650-mppt.html) · [ML](https://lista.mercadolivre.com.br/bq24650) |
| 12 | Célula Li-ion 21700 **Molicel P42A** (ou Samsung 40T) | 6 | R$ 40–60 cada | [ML](https://lista.mercadolivre.com.br/molicel-p42a) |
| 13 | BMS 2S balanceado 20 A + fita de níquel (ou peça o pack 2S3P já soldado) | 1 | R$ 20–40 | [ML](https://lista.mercadolivre.com.br/bms-2s-20a-balanceado) |
| 14 | Módulo **INA226** (CJMCU-226) + resistor shunt **0,01 Ω 2512 (R010)** | 1 + 1 | R$ 20–40 | [Ali](https://pt.aliexpress.com/w/wholesale-ina226-module.html) |
| 15 | BEC 5 V 3 A (para a GoPro) + cabo USB-C curto | 1 | R$ 30–60 | [ML](https://lista.mercadolivre.com.br/ubec-5v-3a) |
| 16 | Conectores XT60, fio de silicone 16 AWG e 22 AWG, termo-retrátil | 1 kit | R$ 50–80 | [ML](https://lista.mercadolivre.com.br/kit-xt60-fio-silicone) |

> O módulo INA226 comum vem com shunt de 0,1 Ω (escrito **R100**), que só
> mede até 0,8 A. Troque pelo de **0,01 Ω (R010)** para medir até 8 A. O
> código já vem configurado para 0,01 Ω.

### Eletrônica de bordo e base

| # | Item | Qtd | Preço aprox. | Links |
|---|---|---|---|---|
| 17 | **LilyGO TTGO LoRa32 V2.1** (T3 v1.6.1), **915 MHz**, com antena. Um vai no avião e outro na base | 2 | R$ 120–180 cada | [ML](https://lista.mercadolivre.com.br/ttgo-lora32-915mhz) · [Ali](https://pt.aliexpress.com/w/wholesale-ttgo-lora32-915.html) |
| 18 | LiDAR **Benewake TF-Luna** (8 m). Para mais alcance: TFmini-S (12 m) ou TF02-Pro (40 m) | 1 | R$ 150–600 | [ML](https://lista.mercadolivre.com.br/tf-luna-lidar) · [Ali](https://pt.aliexpress.com/w/wholesale-benewake-tf-luna.html) |
| 18b | Câmera térmica **MLX90640, versão BAA (55°)**, placa com I2C (ex: Adafruit 4407). É o "olho" da IA | 1 | R$ 250–450 | [ML](https://lista.mercadolivre.com.br/mlx90640) · [Ali](https://pt.aliexpress.com/w/wholesale-mlx90640-55.html) |
| 19 | Power bank (para ligar a base) | 1 | — | você já deve ter |
| ✔ | ESP32 e GoPro HERO7 Black | — | — | você já tem |

> Na câmera térmica, evite a placa **GY-MCU90640**: ela tem outro
> processador no meio e fala por serial, não por I2C. Escolha a de
> **55°**: a de 110° vê uma área maior, mas cada pixel cobre um pedaço
> maior do chão, e um foco pequeno some.

> O seu ESP32 atual serve para os **testes de bancada**. No avião vale a
> pena usar o TTGO, que já vem com o rádio LoRa.

### Ferramentas

Ferro de solda (60 W ou mais), estanho, multímetro, cola quente, fita
dupla-face, velcro, abraçadeiras e carregador balanceador para Li-ion
(ex: ToolkitRC M6 ou ISDT).

---

## 3. Ligações elétricas

### 3.1 Energia

| De | Para | Observação |
|---|---|---|
| Células solares (+) | INA226 **IN+** | Fio 16 AWG |
| INA226 **IN−** | MPPT **IN+** | O INA226 fica no meio do fio positivo |
| Células solares (−) | MPPT **IN−** | |
| MPPT **OUT+ / OUT−** | Bateria (+ / −), no mesmo XT60 da controladora (cabo Y) | Ajuste a saída em **8,4 V** antes de ligar |
| Bateria (XT60) | Controladora Matek, entrada VBAT | A Matek já mede tensão e corrente |
| Bateria | BEC 5 V 3 A → USB-C da GoPro | Deixe a bateria dentro da GoPro |
| 5 V da controladora | TTGO pino **5V**, LiDAR **+5V** | GND comum em tudo |

> ⚠ **Antes de ligar o MPPT na bateria**, alimente o MPPT pelas células
> solares no sol e meça a saída com o multímetro. Gire o trimpot até dar
> **8,40 V**. Mais que isso estraga as células Li-ion e pode pegar fogo.

### 3.2 Sinais (TTGO LoRa32)

| Pino do TTGO | Liga em | Para quê |
|---|---|---|
| GPIO **34** (RX) | **TX** de uma UART livre da controladora | MAVLink |
| GPIO **4** (TX) | **RX** da mesma UART | MAVLink |
| GPIO **35** (RX) | **TX** do LiDAR (TF-Luna pino 3) | Distância |
| GPIO **13** (TX) | **RX** do LiDAR (TF-Luna pino 2) | |
| GPIO **21** (SDA) | INA226 **SDA** | Medidor solar |
| GPIO **22** (SCL) | INA226 **SCL** | |
| **3V3** | INA226 **VCC** e MLX90640 **VIN** | |
| GPIO **21 / 22** | MLX90640 **SDA / SCL** | Mesmo barramento do INA226 |
| **GND** | GND da controladora, do LiDAR, do INA226 e da MLX90640 | **Obrigatório** |

**Pinos do TF-Luna:** 1 = +5V · 2 = RX · 3 = TX · 4 = GND · 5 = deixar
**solto** (modo serial) · 6 = não usar. No TFmini-S, pelos fios: vermelho
+5V, preto GND, verde TX, branco RX. Confirme no manual do seu modelo.

**LiDAR para baixo** (padrão): mede a altura do chão, ajuda no pouso
automático e mostra a altura real na base.
**LiDAR para frente**: avisa na base se houver obstáculo. Mude
`LIDAR_PARA_BAIXO` para `false` no `config.h` e `RNGFND1_ORIENT` para `0`.

> **Sobre obstáculos:** com o avião a ~50 km/h, o TF-Luna (8 m) dá menos de
> 1 segundo de aviso. Isso não basta para desviar. O ArduPlane também não
> desvia sozinho de obstáculos. Para evitar colisão, planeje rotas com
> altura de sobra. O LiDAR para baixo é o uso mais útil num avião.

---

## 4. Montagem passo a passo

Faça **na ordem**. Cada fase precisa funcionar antes de passar para a
próxima.

### Fase 1 — Bancada (dá para começar já, com o que você tem)

1. Teste a GoPro com o ESP32: veja `esp32-gopro/` e o `README.md`.
2. Ligue o LiDAR no ESP32 (pinos 35 e 13, +5V e GND) e grave o
   `aviao-esp32`. Sem rádio LoRa, ele mostra a telemetria no Monitor
   Serial. O campo do LiDAR deve mudar quando você mexe a mão na frente
   dele.

### Fase 2 — Avião voando sem solar

1. Monte o planador conforme o manual.
2. Instale a controladora, o GPS (longe de fios de energia, por causa da
   bússola) e o receptor.
3. Instale o [Mission Planner](https://ardupilot.org/planner/) no
   computador e grave o firmware **ArduPlane** na controladora.
4. Siga a [configuração inicial do ArduPlane](https://ardupilot.org/plane/docs/initial-setup.html):
   calibre acelerômetro, bússola, rádio e servos.
5. Carregue o arquivo `ardupilot/drone-solar.param`, depois de trocar o
   número da UART.
6. Meça o motor com o wattímetro: na bateria 2S, uns 120–150 W no máximo.
7. Primeiros voos em modo **FBWA**, depois faça **AUTOTUNE**. Teste o
   **RTL** (volta para casa) e o failsafe: desligue o rádio com o avião
   no ar, em lugar seguro.

### Fase 3 — ESP32, LiDAR e GoPro a bordo

1. Fixe o TTGO e o LiDAR. A antena LoRa deve ficar **na vertical** e
   longe de fios de energia.
2. Prenda a GoPro com o suporte dela, sem a caixa estanque, para pesar
   menos.
3. Na GoPro: Wi-Fi **ligado**, Desligamento automático **Nunca** e tela
   desligada.
4. Grave o `base-lora` no segundo TTGO, ligue no power bank, conecte o
   celular na rede **Drone-Bombeiro** e abra `http://192.168.4.1`.
5. Arme o motor no chão (sem hélice!): a GoPro deve começar a gravar
   sozinha e a base deve mostrar **GRAVANDO**.
6. No Mission Planner, confira se o LiDAR aparece (aba Status,
   `rangefinder1`).

### Fase 4 — Placas solares

1. Solde as células **em série** com a fita: o (+) de uma, na frente,
   vai no (−) da próxima, atrás. Faça em grupos de 4 a 6, em cima de uma
   superfície plana.
2. Teste cada grupo no sol com o multímetro: ~0,6 V por célula sem carga.
3. Cole na parte de cima da asa, seguindo a curva, sem dobrar demais. As
   SunPower aguentam curva suave, mas trincam se dobrar forte.
4. Cubra com o filme de laminação.
5. Junte os grupos em série: 18 células dão ~10,4 V, 20 células ~11,6 V.
   Passe os fios por dentro da asa até a fuselagem.
6. Ligue no INA226 e no MPPT, **com a saída já ajustada em 8,4 V**.
7. No sol, a base deve mostrar a potência **Solar** subindo: 40–55 W com
   sol forte ao meio-dia.
8. **Equilibre o avião de novo** (centro de gravidade). As células e a
   bateria mudam o peso.

### Fase 5 — Missões automáticas

Planeje rotas no Mission Planner (modo **AUTO**) e use **LOITER** para
circular sobre um ponto, como um foco de incêndio. Comece com missões
curtas, sempre com o rádio na mão para assumir o controle.

### Fase 6 — IA caçadora de focos

Veja a [seção 5](#5-ia-de-bordo--caçadora-de-focos). Faça os testes
**nesta ordem**.

---

## 5. IA de bordo — caçadora de focos

O ESP32 olha pela câmera térmica (32 × 24 pixels, 2 vezes por segundo)
enquanto o avião faz a missão de patrulha em **AUTO**:

1. **Vigiando:** procura um ponto bem mais quente que o resto da imagem
   (padrão: acima de 70 °C e 25 °C acima da média do chão).
2. **Confirmou** em 3 imagens seguidas: calcula onde o foco está no chão.
   Para isso usa o GPS, a altura e a inclinação do avião. Manda o alerta
   para a base: **🔥 FOCO DE INCÊNDIO**, com a temperatura e o link do
   mapa.
3. **Circula sobre o foco:** pede ao ArduPilot para dar voltas de 70 m de
   raio em cima dele, **na mesma altura** (modo GUIDED). A GoPro continua
   gravando.
4. **Volta para a missão** depois de 3 minutos, ou quando você aperta
   **↩ Voltar à missão** na base.

**Quando a IA NÃO assume o controle** (ela só avisa a base):
- se o avião não estiver em AUTO (por exemplo, você pilotando em FBWA);
- abaixo de 40 m, ou com a bateria abaixo de 35%;
- se for o mesmo foco que acabou de circular, ou nos 5 minutos seguintes;
- se o botão **🤖 IA: só avisar** estiver ligado na base.

**Você sempre manda:** mudou o modo no rádio, a IA solta na hora e não
insiste. Os failsafes do ArduPilot (bateria, perda de rádio, volta para
casa) continuam valendo por cima de tudo.

> **Limites honestos:** com 32 × 24 pixels, a 100 m de altura cada pixel
> vê ~3 × 3 m do chão. Um foco pequeno aparece "misturado" com o chão em
> volta e pode não passar de 70 °C. Por isso a busca fica mais confiável
> entre 60 e 100 m. Telhado de metal, asfalto e pedra no sol do meio-dia
> chegam a 60–70 °C e podem dar **alarme falso**: ajuste `IA_LIMITE_C` e
> `IA_CONTRASTE_C` no `config.h` depois de ver as temperaturas da sua
> região. A IA é um **ajudante**: quem decide é sempre o bombeiro.

### Como testar a IA

1. **Bancada:** com o avião desarmado, aponte a câmera térmica para uma
   caneca de água fervendo, a uns 30 cm. O Monitor Serial deve mostrar
   `[IA] foco: avião não está em AUTO`, e a base deve mostrar o alerta de
   foco.
2. **Direção da imagem:** mova a caneca para a frente do avião e veja se
   o foco aparece na frente no mapa. Se aparecer invertido, mude
   `IA_INVERTER_LINHAS` ou `IA_INVERTER_COLUNAS`.
3. **Voos só avisando:** voe algumas missões com `IA_PODE_CONTROLAR false`
   e anote os alarmes falsos.
4. **Simulador (recomendado):** teste os comandos no
   [simulador SITL do ArduPilot](https://ardupilot.org/dev/docs/sitl-simulator-software-in-the-loop.html)
   antes de deixar a IA mexer no avião de verdade.
5. Só então ligue o controle, primeiro com um foco controlado, como uma
   fogueira autorizada num campo aberto.

---

## 6. Checklist antes de cada voo

- [ ] Bateria carregada (8,4 V) e bem presa
- [ ] GoPro ligada, com cartão e Wi-Fi ligado
- [ ] Base ligada e recebendo (sinal "há 2s")
- [ ] GPS com 10 satélites ou mais
- [ ] Modo, LiDAR e bateria aparecendo na base
- [ ] Superfícies de comando mexendo para o lado certo
- [ ] Área livre de pessoas, com o vento anotado
- [ ] IA: "vigiando" na base, e o botão de controle do jeito que você quer
- [ ] Voo autorizado no SARPAS

---

## 7. Regras no Brasil

- **ANAC:** cadastre o drone no [SISANT](https://sistemas.anac.gov.br/sisant).
  Acima de 250 g é obrigatório. Veja também as
  [regras de drones da ANAC](https://www.gov.br/anac/pt-br/assuntos/drones).
- **DECEA:** peça autorização de cada voo no
  [SARPAS](https://servicos.decea.mil.br/sarpas/). Limite de 120 m de
  altura. Voos fora da linha de visada precisam de autorização especial.
- **ANATEL:** use rádios homologados. O LoRa em 915 MHz é faixa livre,
  mas o equipamento precisa ser homologado.
- **Bombeiros e segurança pública** têm regras próprias para operações
  oficiais. Consulte o setor de aviação ou de drones da sua corporação.

---

## 8. Links úteis

- ArduPlane, documentação: <https://ardupilot.org/plane/>
- Mission Planner: <https://ardupilot.org/planner/>
- Comandos Wi-Fi da GoPro (não oficial): <https://github.com/KonradIT/goprowifihack>
- LilyGO (TTGO LoRa32): <https://lilygo.cc>
- Biblioteca LoRa para Arduino: <https://github.com/sandeepmistry/arduino-LoRa>
- Biblioteca da câmera térmica: <https://github.com/adafruit/Adafruit_MLX90640>
