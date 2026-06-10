#  Projeto E.D.E.N
### Ecological Development in Exo-Environments

> Sistema IoT embarcado para monitoramento de biocápsula botânica orbital simulada.  
> **ExoGenesis · FIAP Global Solution 2026 — Ciências da Computação (1CCPZ)**

---

## Integrantes

| Nome | RM |
|---|---|
| Arthur Vettorazzo de Souza | 569445 |
| Brayan Barbosa Dos Santos | 573682 |
| Giovanne Gomes Petenuci | 574091 |

**Professor:** Marcus Grilo — Computer Organization and Architecture

---

## Sobre o Projeto

O E.D.E.N parte de uma hipótese científica: a exposição controlada de espécimes vegetais da Mata Atlântica à microgravidade e à radiação cósmica pode induzir adaptações fisiológicas que aumentem sua resiliência a condições climáticas extremas aqui na Terra.

Para que esse experimento seja viável, as condições internas da biocápsula precisam ser mantidas dentro de faixas precisas durante toda a missão. O sistema IoT desenvolvido neste projeto monitora continuamente temperatura, umidade, luminosidade e vibração, classifica o estado operacional da cápsula em 4 fases e aciona respostas automáticas proporcionais à gravidade dos eventos detectados.

**ODS relacionados:** 9 (Inovação), 13 (Ação Climática), 15 (Vida Terrestre)

---

##  Hardware Utilizado

| Componente | Função | Interface / Pino |
|---|---|---|
| ESP32 DevKit V1 | Microcontrolador principal | — |
| DHT22 | Temperatura e umidade | Digital — GPIO 4 |
| LDR (fotoresistor) | Luminosidade ambiental | Analógico — GPIO 34 |
| MPU-6050 | Aceleração / vibração (3 eixos) | I²C — SDA 21 / SCL 22 |
| LCD 16×2 I²C | Display de dados e alertas | I²C — addr 0x27 |
| LED Verde | Indicador: Missão Nominal | GPIO 25 |
| LED Amarelo | Indicador: Eclipse / Alerta | GPIO 26 |
| LED Vermelho | Indicador: Evacuação | GPIO 27 |
| LED Azul | Propulsor piscante (evacuação) | GPIO 32 |
| Buzzer | Alertas sonoros | GPIO 23 |
| Resistores 220Ω | Limitadores de corrente dos LEDs | Em série com cada LED |

---

## Fases Operacionais

O sistema classifica continuamente o estado da missão em 4 fases, em ordem decrescente de prioridade:

| Fase | Condição | LEDs | Buzzer |
|---|---|---|---|
| 🟢 Missão Nominal | Todos os sensores dentro do esperado | Verde | — |
| 🟡 Zona de Eclipse | Luminosidade abaixo do threshold orbital | Amarelo | — |
| 🟡🔴 Alerta Cápsula | Sensor fora do limite seguro — atuador acionado | Amarelo + Vermelho | 1 bip |
| 🔴 Evacuação | Condição crítica irreversível detectada | Vermelho + Azul piscando | Contínuo |

### Thresholds de decisão

| Variável | Alerta | Evacuação |
|---|---|---|
| Temperatura | < 0°C ou > 60°C | < −5°C ou > 80°C |
| Umidade | < 40% ou > 85% | < 10% ou > 90% |
| Vibração | > 2,5 g | > 3,4 g |
| Luminosidade (ADC) | > 3.800 | > 4.000 |

---

## Decisões Técnicas

**1. Controle não-bloqueante com `millis()`**  
O loop principal usa `millis()` no lugar de `delay()`, permitindo que leitura de sensores (2s), rotação do LCD (3s), buzzer e pisca do LED azul operem simultaneamente sem travar o sistema.

**2. Magnitude vetorial para vibração**  
A vibração não é lida de um único eixo. Calculamos a magnitude vetorial nos 3 eixos do MPU-6050:

```
vibração = √(ax² + ay² + az²)
```

Isso captura qualquer impacto independente da direção.

**3. Máquina de estados com prioridade em cascata**  
A função `evaluatePhase()` verifica as fases em ordem decrescente de gravidade — Evacuação → Alerta → Eclipse → Nominal — garantindo que a situação mais crítica sempre prevalece.

---

## Estrutura do Repositório

```
eden-gs2026/
├── eden_main.ino       # Firmware principal (ESP32/Arduino)
├── diagram.json        # Diagrama do circuito (Wokwi)
├── libraries.txt       # Bibliotecas utilizadas no Wokwi
├── RelatórioTecnico.pdf
└── README.md
```

---

## Bibliotecas

```
DHT sensor library
LiquidCrystal I2C
MPU6050
```

---

## Links

| Recurso | Link |
|---|---|
| [Simulação no Wokwi](wokwi.com/projects/465840600415942657](https://wokwi.com/projects/465840600415942657) |  
| [Vídeo no YouTube](https://youtu.be/-6Ov3c38Vsc) |  

---

## 🧪 Cenários Testados

Todos os 8 cenários de missão foram validados com sucesso na simulação:

| # | Condição | Fase Esperada | Status |
|---|---|---|---|
| 1 | Todos os sensores normais | Missão Nominal | 
| 2 | LDR > 3000 (escuro) | Zona de Eclipse | 
| 3 | Temperatura > 60°C | Alerta — TEMP+:RESFRIANDO | 
| 4 | Umidade < 40% | Alerta — UMID-: IRRIGANDO | 
| 5 | Vibração > 2,5g | Alerta — VIBR: ESTABILIZ. | 
| 6 | Temperatura < −5°C | Evacuação — FALHA TERMICA | 
| 7 | Vibração > 3,4g | Evacuação — DANO ESTRUTURAL | 
| 8 | Umidade > 90% | Evacuação — COLAPSO HIDRICO | 

---

*FIAP Global Solution 2026 · 1º Semestre*
