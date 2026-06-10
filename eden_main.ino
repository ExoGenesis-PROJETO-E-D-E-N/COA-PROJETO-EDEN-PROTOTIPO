/*
 * PROJETO E.D.E.N — Ecological Development in Exo-Environments
 * ExoGenesis · FIAP Global Solution 2026
 * 
 * Sistema IoT de Monitoramento de Biocápsula Botânica Orbital
 * Hardware: ESP32 + DHT22 + LDR + MPU-6050 + LCD I2C + LEDs + Buzzer
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <MPU6050.h>

// ─── PINAGEM ────────────────────────────────────────────────────
#define DHT_PIN       4
#define DHT_TYPE      DHT22
#define LDR_PIN       34
#define LED_GREEN     25
#define LED_YELLOW    26
#define LED_RED       27
#define BUZZER_PIN    23
#define LED_BLUE      32

// ─── OBJETOS ────────────────────────────────────────────────────
DHT dht(DHT_PIN, DHT_TYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);
MPU6050 mpu;

// ─── FASES DA MISSÃO ────────────────────────────────────────────
enum MissionPhase {
  PHASE_NOMINAL,    // Missão nominal — condições ideais de suporte à vida
  PHASE_ECLIPSE,    // Zona de eclipse — aguardando passagem
  PHASE_STRESS,     // Alerta — sistema corrigindo condições da cápsula
  PHASE_REENTRY     // Crítico — sistemas falharam, evacuação necessária
};

MissionPhase currentPhase = PHASE_NOMINAL;
String phaseNames[] = {"MISSAO NOMINAL", "ZONA ECLIPSE", "ALERTA CAPSULA", "EVACUANDO!!!"};

// ─── THRESHOLDS POR FASE ────────────────────────────────────────
// [NOMINAL, ECLIPSE, STRESS, REENTRY]
float tempMax[]   = {35.0, 25.0, 60.0, 80.0};  // °C — limite superior
float tempMin[]   = {10.0,  0.0,  0.0, -5.0};  // °C — limite inferior (estresse < 0, reentrada < -5)
float humMax[]    = {70.0, 75.0, 85.0, 90.0};  // % — umidade máxima (acima = risco de fungos)
float humMin[]    = {40.0, 35.0, 40.0, 10.0};  // % — umidade mínima (estresse < 40%, reentrada < 10%)
// LDR no Wokwi: valor BAIXO = muita luz | valor ALTO = escuro
// Range real: ~0 (luz máxima) a ~4095 (escuridão total)
// Eclipse só ocorre com luz bastante reduzida
int   ldrMax[]    = {1500, 3000, 3800, 4000};   // acima desse valor = escuro demais p/ a fase
float vibrMax[]   = {1.5,   1.0,  2.5,  3.4};  // g — aceleração máxima (Wokwi max ~3.46g)

// ─── VARIÁVEIS GLOBAIS ──────────────────────────────────────────
float temperature = 0;
float humidity    = 0;
int   ldrValue    = 0;
float vibration   = 0;
bool  alertActive = false;
String alertCause = "";   // causa do alerta atual
bool   stressBeepDone = false;  // garante bip único no estresse
bool   thrusterState  = false;  // estado atual do LED azul (pisca na reentrada)
unsigned long lastThruster = 0; // controle do pisca do propulsor
String lastAlertCause = "";     // detecta mudança de causa
int   displayPage = 0;       // alterna páginas do LCD
unsigned long lastPageSwitch  = 0;
unsigned long lastPhaseCheck  = 0;
unsigned long lastBuzzerBeep  = 0;
unsigned long missionStart    = 0;
int   readingCount = 0;

// ─── SETUP ──────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);

  // Inicializa sensores
  dht.begin();
  mpu.initialize();

  // Inicializa LCD
  lcd.init();
  lcd.backlight();

  // Inicializa pinos
  pinMode(LED_GREEN,  OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_RED,    OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_BLUE,   OUTPUT);
  pinMode(LDR_PIN,    INPUT);

  missionStart = millis();

  // Tela de boot
  bootScreen();

  // Log inicial no Serial
  Serial.println("╔══════════════════════════════════════╗");
  Serial.println("║   PROJETO E.D.E.N — MISSION CONTROL  ║");
  Serial.println("║   ExoGenesis · FIAP GS 2026           ║");
  Serial.println("╚══════════════════════════════════════╝");
  Serial.println("> [E.D.E.N OS v1.0] SISTEMA INICIALIZADO");
  Serial.println("> Biocapsula botanica em missao nominal.");
  Serial.println("> Aguardando telemetria...");
  Serial.println("─────────────────────────────────────────");
}

// ─── LOOP ───────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  // Lê sensores a cada 2s
  if (now - lastPhaseCheck >= 2000) {
    lastPhaseCheck = now;
    readingCount++;

    readSensors();
    evaluatePhase();
    updateLEDs();
    logToSerial();
  }

  // Alterna página do LCD a cada 3s
  if (now - lastPageSwitch >= 3000) {
    lastPageSwitch = now;
    displayPage = (displayPage + 1) % 3;
    updateLCD();
  }

  // Buzzer: 1 bip único no estresse, contínuo na reentrada
  if (currentPhase == PHASE_REENTRY) {
    // Bip contínuo na reentrada
    if (now - lastBuzzerBeep >= 400) {
      lastBuzzerBeep = now;
      tone(BUZZER_PIN, 1500, 300);
    }
    // LED azul piscando — propulsor ativado
    if (now - lastThruster >= 300) {
      lastThruster  = now;
      thrusterState = !thrusterState;
      digitalWrite(LED_BLUE, thrusterState);
    }
  } else {
    // Fora da reentrada — propulsor desligado
    digitalWrite(LED_BLUE, LOW);
    thrusterState = false;
    if (currentPhase == PHASE_STRESS && !stressBeepDone) {
      // 1 bip único ao entrar no estresse
      tone(BUZZER_PIN, 800, 200);
      stressBeepDone = true;
    }
  }
}

// ─── LÊ SENSORES ────────────────────────────────────────────────
void readSensors() {
  // DHT22
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) temperature = t;
  if (!isnan(h)) humidity    = h;

  // LDR
  ldrValue = analogRead(LDR_PIN);

  // MPU-6050 — calcula magnitude da aceleração
  int16_t ax, ay, az, gx, gy, gz;
  mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
  float ax_g = ax / 16384.0;
  float ay_g = ay / 16384.0;
  float az_g = az / 16384.0;
  vibration = sqrt(ax_g*ax_g + ay_g*ay_g + az_g*az_g);
}

// ─── AVALIA FASE DA MISSÃO ───────────────────────────────────────
void evaluatePhase() {
  alertActive = false;

  bool tempCritical = (temperature > tempMax[PHASE_REENTRY]) ||
                      (temperature < tempMin[PHASE_REENTRY]);
  bool vibrCritical = (vibration > vibrMax[PHASE_REENTRY]);
  bool humCritical  = (humidity  > humMax[PHASE_REENTRY])  ||
                      (humidity  < humMin[PHASE_REENTRY]);
  bool ldrCritical  = (ldrValue  > ldrMax[PHASE_REENTRY]);

  // EVACUAÇÃO: atuadores falharam — condições letais para a planta
  if (tempCritical || vibrCritical || humCritical || ldrCritical) {
    if (tempCritical) {
      alertCause = temperature > tempMax[PHASE_REENTRY] ? "FALHA TERMICA" : "CONGELAMENTO";
    } else if (vibrCritical) {
      alertCause = "DANO ESTRUTURAL";
    } else if (humCritical) {
      alertCause = "COLAPSO HIDRICO";
    } else {
      alertCause = "FALHA ENERGETICA";
    }
    if (currentPhase != PHASE_REENTRY) {
      currentPhase = PHASE_REENTRY;
      alertActive  = true;
      reentryAlert();
    }
    return;
  }

  // ALERTA: sistema de suporte falhou — IA ativa atuadores para corrigir
  bool tempHigh = temperature > tempMax[PHASE_STRESS];
  bool tempLow  = temperature < tempMin[PHASE_STRESS];
  bool vibrHigh = vibration   > vibrMax[PHASE_STRESS];
  bool humHigh  = humidity    > humMax[PHASE_STRESS];
  bool humLow   = humidity    < humMin[PHASE_STRESS];
  bool ldrHigh  = ldrValue    > ldrMax[PHASE_STRESS];

  bool inStress = tempHigh || tempLow || vibrHigh || humHigh || humLow || ldrHigh;

  if (inStress) {
    // Define mensagem do atuador correspondente
    if (tempHigh)       alertCause = "TEMP+:RESFRIANDO";
    else if (tempLow)   alertCause = "TEMP-: AQUECENDO";
    else if (humHigh)   alertCause = "UMID+:EXAUSTORES";
    else if (humLow)    alertCause = "UMID-: IRRIGANDO";
    else if (vibrHigh)  alertCause = "VIBR: ESTABILIZ.";
    else if (ldrHigh)   alertCause = "ECLIPSE: REPOUSO";
    // Reseta bip se a causa mudou
    if (alertCause != lastAlertCause) {
      stressBeepDone = false;
      lastAlertCause = alertCause;
    }
    currentPhase = PHASE_STRESS;
    alertActive  = true;
    return;
  }

  // ECLIPSE: luz reduzida — aguardando passagem
  if (ldrValue > ldrMax[PHASE_ECLIPSE]) {
    alertCause     = "ECLIPSE ORBITAL";
    stressBeepDone = false;
    lastAlertCause = "";
    currentPhase   = PHASE_ECLIPSE;
    return;
  }

  // NOMINAL: tudo dentro do esperado
  alertCause     = "";
  stressBeepDone = false;
  lastAlertCause = "";
  currentPhase   = PHASE_NOMINAL;
}

// ─── ATUALIZA LEDs ───────────────────────────────────────────────
void updateLEDs() {
  digitalWrite(LED_GREEN,  LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_RED,    LOW);

  switch (currentPhase) {
    case PHASE_NOMINAL:
      digitalWrite(LED_GREEN, HIGH);
      break;
    case PHASE_ECLIPSE:
      digitalWrite(LED_YELLOW, HIGH);
      break;
    case PHASE_STRESS:
      digitalWrite(LED_YELLOW, HIGH);
      digitalWrite(LED_RED,    HIGH);
      break;
    case PHASE_REENTRY:
      digitalWrite(LED_RED, HIGH);
      break;
  }
}

// ─── ATUALIZA LCD ───────────────────────────────────────────────
void updateLCD() {
  lcd.clear();
  switch (displayPage) {

    case 0: // Página 1: Temperatura e Umidade
      lcd.setCursor(0, 0);
      lcd.print("TEMP: ");
      lcd.print(temperature, 1);
      lcd.print((char)223); // símbolo °
      lcd.print("C");
      lcd.setCursor(0, 1);
      lcd.print("UMID: ");
      lcd.print(humidity, 1);
      lcd.print("%");
      break;

    case 1: // Página 2: Luminosidade e Vibração
      lcd.setCursor(0, 0);
      lcd.print("LUZ:  ");
      lcd.print(ldrValue);
      lcd.print(" lux");
      lcd.setCursor(0, 1);
      lcd.print("VIBR: ");
      lcd.print(vibration, 2);
      lcd.print("g");
      break;

    case 2: // Página 3: Fase da Missão + ação da IA
      lcd.setCursor(0, 0);
      lcd.print(phaseNames[currentPhase]);
      lcd.setCursor(0, 1);
      if (alertCause != "") {
        lcd.print(alertCause);
      } else {
        lcd.print("SISTEMAS OK");
      }
      break;
  }
}

// ─── ALERTA DE REENTRADA ─────────────────────────────────────────
void reentryAlert() {
  lcd.clear();
  lcd.setCursor(0, 0);
  // Linha 1: causa específica
  lcd.print(alertCause);
  lcd.setCursor(0, 1);
  // Linha 2: ação
  if (alertCause == "DANO ESTRUTURAL" || alertCause == "FALHA ENERGETICA") {
    lcd.print("EVACUANDO!");
  } else {
    lcd.print("REENTRADA INIC.");
  }

  // Buzzer contínuo gerenciado no loop principal

  Serial.println("╔══════════════════════════════════════╗");
  Serial.println("║  !! PROTOCOLO DE REENTRADA ATIVADO !! ║");
  Serial.print  ("║  CAUSA: ");
  Serial.println(alertCause);
  Serial.println("║  Capscula iniciando retorno a Terra.  ║");
  Serial.println("╚══════════════════════════════════════╝");
}

// ─── LOG SERIAL ──────────────────────────────────────────────────
void logToSerial() {
  unsigned long elapsed = (millis() - missionStart) / 1000;

  Serial.print("> [T+");
  if (elapsed < 3600) {
    Serial.print(elapsed / 60);
    Serial.print("m");
    Serial.print(elapsed % 60);
    Serial.print("s] ");
  } else {
    Serial.print(elapsed / 3600);
    Serial.print("h] ");
  }

  Serial.print("TEMP=");
  Serial.print(temperature, 1);
  Serial.print("C | UMID=");
  Serial.print(humidity, 1);
  Serial.print("% | LUZ=");
  Serial.print(ldrValue);
  Serial.print(" | VIBR=");
  Serial.print(vibration, 2);
  Serial.print("g | FASE=");
  Serial.print(phaseNames[currentPhase]);

  if (alertActive) {
    Serial.print(" [!!! ALERTA !!!");
    if (temperature > tempMax[PHASE_STRESS] || temperature < tempMin[PHASE_STRESS])
      Serial.print(" TEMP");
    if (humidity > humMax[PHASE_STRESS] || humidity < humMin[PHASE_STRESS])
      Serial.print(" UMID");
    if (vibration > vibrMax[PHASE_STRESS])
      Serial.print(" VIBR");
    Serial.print("]");
  }
  Serial.println();
}

// ─── TELA DE BOOT ────────────────────────────────────────────────
void bootScreen() {
  lcd.clear();
  lcd.setCursor(2, 0);
  lcd.print("PROJ. E.D.E.N");
  lcd.setCursor(1, 1);
  lcd.print("Inicializando...");
  delay(2000);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("ExoGenesis 2026");
  lcd.setCursor(0, 1);
  lcd.print("FIAP Global Sol.");
  delay(2000);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Sistema OK!");
  lcd.setCursor(0, 1);
  lcd.print("Missao iniciada!");
  delay(1500);

  // LED verde acende no boot
  digitalWrite(LED_GREEN, HIGH);
  delay(500);
  digitalWrite(LED_GREEN, LOW);
}
