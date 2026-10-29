"""Cérebro do robô: conversa com o Claude e manda comandos ao Arduino pela serial.

Uso:
    python robo_claude.py --porta COM3            # Windows
    python robo_claude.py --porta /dev/ttyACM0    # Linux
    python robo_claude.py --porta COM3 --manual   # testa comandos sem o Claude
    python robo_claude.py --simular               # sem Arduino (para testar o Claude)

Requer a variável de ambiente ANTHROPIC_API_KEY (exceto no modo --manual).
"""

import argparse
import time

MODELO = "claude-sonnet-5-5"

SISTEMA = """Você é o cérebro de um pequeno robô-carrinho.
Hardware: um motor traseiro (só frente/ré, sem controle de velocidade), direção dianteira
por servo (-40 a +40 graus) e um sensor de distância apontado para frente.
A direção só muda o rumo enquanto o robô anda. Para fazer curva: vire, depois mova.
Ande em passos curtos (300 a 1000 ms) e confira a distância após cada movimento.
Se a distância for menor que 30 cm, não vá para frente: dê ré com a direção virada.
Responda em português, de forma breve."""

TOOLS = [
    {
        "name": "mover",
        "description": "Anda para FRENTE ou TRAS por ms milissegundos (máx 3000). "
        "Para sozinho se houver obstáculo a menos de 20 cm à frente. Retorna a distância à frente em cm.",
        "input_schema": {
            "type": "object",
            "properties": {
                "direcao": {"type": "string", "enum": ["FRENTE", "TRAS"]},
                "ms": {"type": "integer", "minimum": 0, "maximum": 3000},
            },
            "required": ["direcao", "ms"],
        },
    },
    {
        "name": "virar",
        "description": "Posiciona a direção: -40 = máximo para um lado, 0 = reto, +40 = máximo para o outro.",
        "input_schema": {
            "type": "object",
            "properties": {"angulo": {"type": "integer", "minimum": -40, "maximum": 40}},
            "required": ["angulo"],
        },
    },
    {
        "name": "medir_distancia",
        "description": "Lê a distância até o obstáculo à frente, em cm.",
        "input_schema": {"type": "object", "properties": {}},
    },
    {
        "name": "luz",
        "description": "Liga (true) ou desliga (false) a luz.",
        "input_schema": {
            "type": "object",
            "properties": {"ligar": {"type": "boolean"}},
            "required": ["ligar"],
        },
    },
]


class Arduino:
    def __init__(self, porta):
        import serial

        self.ser = serial.Serial(porta, 115200, timeout=5)
        time.sleep(2)  # o Arduino reinicia ao abrir a serial
        self.ser.reset_input_buffer()

    def enviar(self, cmd):
        self.ser.write((cmd + "\n").encode())
        resposta = self.ser.readline().decode(errors="ignore").strip()
        return resposta or "ERRO sem resposta do Arduino"


class Simulador:
    def __init__(self):
        self.dist = 120

    def enviar(self, cmd):
        nome, _, valor = cmd.partition(" ")
        if nome == "FRENTE":
            self.dist = max(15, self.dist - int(valor) // 20)
        elif nome == "TRAS":
            self.dist += int(valor) // 20
        print(f"  [simulado] {cmd} -> dist {self.dist}")
        return f"OK DIST {self.dist}"


def executar(robo, nome, a):
    if nome == "mover":
        return robo.enviar(f"{a['direcao']} {int(a['ms'])}")
    if nome == "virar":
        return robo.enviar(f"VIRAR {int(a['angulo'])}")
    if nome == "medir_distancia":
        return robo.enviar("DIST 0")
    if nome == "luz":
        return robo.enviar(f"LUZ {1 if a['ligar'] else 0}")
    return f"ERRO ferramenta desconhecida: {nome}"


def modo_manual(robo):
    print("Digite comandos (ex: FRENTE 500, VIRAR -30, LUZ 1). 'sair' encerra.")
    while (cmd := input("> ").strip()) != "sair":
        if cmd:
            print(robo.enviar(cmd))


def modo_claude(robo):
    import anthropic

    client = anthropic.Anthropic()
    msgs = []
    print("Converse com o robô. 'sair' encerra.")
    while (texto := input("Você: ").strip()) != "sair":
        if not texto:
            continue
        msgs.append({"role": "user", "content": texto})
        while True:
            r = client.messages.create(
                model=MODELO, max_tokens=1024, system=SISTEMA, tools=TOOLS, messages=msgs
            )
            msgs.append({"role": "assistant", "content": r.content})
            if r.stop_reason != "tool_use":
                print("Robô:", "".join(b.text for b in r.content if b.type == "text"))
                break
            resultados = []
            for b in r.content:
                if b.type == "tool_use":
                    saida = executar(robo, b.name, b.input)
                    print(f"  {b.name}({b.input}) -> {saida}")
                    resultados.append({"type": "tool_result", "tool_use_id": b.id, "content": saida})
            msgs.append({"role": "user", "content": resultados})


def main():
    p = argparse.ArgumentParser(description="Robô controlado pelo Claude")
    p.add_argument("--porta", help="porta serial do Arduino (ex: COM3, /dev/ttyACM0)")
    p.add_argument("--manual", action="store_true", help="envia comandos digitados direto ao Arduino")
    p.add_argument("--simular", action="store_true", help="roda sem Arduino")
    args = p.parse_args()

    if args.simular:
        robo = Simulador()
    elif args.porta:
        robo = Arduino(args.porta)
    else:
        p.error("informe --porta ou use --simular")

    try:
        modo_manual(robo) if args.manual else modo_claude(robo)
    finally:
        robo.enviar("PARAR 0")


if __name__ == "__main__":
    main()
