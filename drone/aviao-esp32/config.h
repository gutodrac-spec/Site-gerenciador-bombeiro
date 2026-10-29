// ===================== CONFIGURAÇÃO DO AVIÃO =====================
// Mude só este arquivo. Valores pensados para a placa
// LilyGO TTGO LoRa32 V2.1 (T3 v1.6.1) com rádio SX1276 de 915 MHz.
#pragma once

// ---------- GoPro HERO7 Black ----------
// Na GoPro: Preferências > Conexões > Informações da câmera
#define GOPRO_SSID "GP12345678"
#define GOPRO_SENHA "senha-da-gopro"
#define GRAVAR_AO_ARMAR true        // começa a gravar ao armar o motor
#define PARAR_APOS_DESARMAR_S 10    // para de gravar X segundos depois de desarmar

// ---------- Rádio LoRa (telemetria até a base) ----------
#define LORA_FREQ 915E6             // faixa de 915 MHz (permitida no Brasil)
#define LORA_SF 9                   // 7 = mais rápido, 12 = mais alcance
#define LORA_SYNC 0xB0              // igual na base
#define LORA_POTENCIA_DBM 17
#define LORA_CHAVE "BMB"            // senha dos comandos da base (igual na base)
#define TELEMETRIA_INTERVALO_MS 2000

#define PINO_LORA_SCK 5
#define PINO_LORA_MISO 19
#define PINO_LORA_MOSI 27
#define PINO_LORA_SS 18
#define PINO_LORA_RST 23            // TTGO V1.0 usa 14
#define PINO_LORA_DIO0 26

// ---------- Controladora de voo (ArduPilot, MAVLink) ----------
#define FC_BAUD 115200
#define PINO_FC_RX 34               // liga no TX da controladora
#define PINO_FC_TX 4                // liga no RX da controladora

// ---------- LiDAR (TF-Luna, TFmini-S, TFmini Plus, TF02-Pro) ----------
#define LIDAR_BAUD 115200
#define PINO_LIDAR_RX 35            // liga no TX do LiDAR
#define PINO_LIDAR_TX 13            // liga no RX do LiDAR
#define LIDAR_MIN_CM 20
#define LIDAR_MAX_CM 800            // TF-Luna 800, TFmini-S 1200, TF02-Pro 4000
#define LIDAR_PARA_BAIXO true       // true = mede altura (pouso); false = aponta para frente
#define OBSTACULO_ALERTA_CM 600     // só no modo para frente: alerta na base

// ---------- Medidor de energia solar (INA226, opcional) ----------
#define USAR_INA226 true
#define INA226_ENDERECO 0x40
#define INA226_SHUNT_OHMS 0.01      // resistor do módulo: R010 = 0,01; R100 = 0,1
#define PINO_I2C_SDA 21
#define PINO_I2C_SCL 22

// ---------- Outros ----------
#define PINO_LED 25                 // aceso = GoPro gravando
