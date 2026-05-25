#pragma once
// ==================================================================
//  etensor.h  –  gemeinsame Typen, Konstanten und Deklarationen
// ==================================================================
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

// ------------------------------------------------------------------
//  Ethernet Event-Gruppe
// ------------------------------------------------------------------
#define ETH_GOT_IP_BIT      BIT0
extern EventGroupHandle_t eth_event_group;

// ------------------------------------------------------------------
//  Ausgabe-Buffer (wird von sensor.c beschrieben,
//  von httpkram.c gelesen)
// ------------------------------------------------------------------
#define OUTPUT_BUF_SIZE     4096
extern char output_buf[OUTPUT_BUF_SIZE];
extern int  output_len;

// ------------------------------------------------------------------
//  Mess-Status
// ------------------------------------------------------------------
typedef enum {
    RESULT_NONE,
    RESULT_POSITIVE,
    RESULT_VERY_POSITIVE,
    RESULT_NEGATIVE,
    RESULT_VERY_NEGATIVE
} result_status_t;

extern volatile result_status_t current_result;
extern volatile bool            doit_running;

// ------------------------------------------------------------------
//  Funktionen aus sensor.c
// ------------------------------------------------------------------
void init_buffer(void);
void Doit(void);
void doit_task(void *pvParameters);

// ------------------------------------------------------------------
//  Funktionen aus httpkram.c
// ------------------------------------------------------------------
void ethernet_init(void);
void start_webserver(void);
